#include "jpeg.h"

#include <string.h>

bool sp_marker_is_standalone(uint8_t code)
{
    if (code >= SP_MARK_RST0 && code <= SP_MARK_RST7)
        return true;
    return code == SP_MARK_SOI || code == SP_MARK_EOI || code == SP_MARK_TEM;
}

bool sp_marker_is_app(uint8_t code)
{
    return code >= SP_MARK_APP0 && code <= SP_MARK_APP15;
}

bool sp_jpeg_has_soi(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < 2u)
        return false;
    return buf[0] == SP_MARK_PREFIX && buf[1] == SP_MARK_SOI;
}

sp_status sp_jpeg_probe(sp_file *f)
{
    uint8_t head[2];
    size_t got = 0;
    sp_status st;

    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    st = sp_read_exact(f, head, sizeof head, &got);
    if (st == SP_ERR_IO)
        return SP_ERR_IO;
    /* A file shorter than two bytes cannot be a JPEG. That is a "not a JPEG"
     * answer, not a truncation complaint — there is no structure to have been
     * truncated yet. */
    if (st == SP_ERR_TRUNCATED || got < sizeof head)
        return SP_ERR_NOT_JPEG;

    return sp_jpeg_has_soi(head, got) ? SP_OK : SP_ERR_NOT_JPEG;
}

sp_status sp_parser_read_payload(sp_parser *p, const sp_segment *seg,
                                 uint8_t *buf, size_t max, size_t *got)
{
    uint64_t saved;
    size_t want;
    size_t n = 0;
    sp_status st;

    if (got != NULL)
        *got = 0;
    if (p == NULL || p->f == NULL || seg == NULL || buf == NULL || max == 0u)
        return SP_ERR_USAGE;
    if (seg->standalone || seg->payload_len == 0u)
        return SP_OK;

    saved = sp_tell(p->f);
    if (saved == UINT64_MAX)
        return SP_ERR_IO;

    want = (seg->payload_len < max) ? (size_t)seg->payload_len : max;

    st = sp_seek(p->f, seg->payload_off);
    if (st == SP_OK)
        st = sp_read_exact(p->f, buf, want, &n);

    /* Restore position whatever happened, so a failed read here cannot
     * desynchronise the walk that is still in progress. */
    {
        sp_status rs = sp_seek(p->f, saved);
        if (st == SP_OK && rs != SP_OK)
            st = rs;
    }

    if (got != NULL)
        *got = n;
    return st;
}

sp_status sp_parser_init(sp_parser *p, sp_file *f)
{
    if (p == NULL || f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    memset(p, 0, sizeof *p);
    p->f = f;
    return sp_seek(f, 0);
}

/* Reads one byte. SP_ERR_TRUNCATED at end of file. */
static sp_status read_byte(sp_file *f, uint8_t *out)
{
    size_t got = 0;
    return sp_read_exact(f, out, 1u, &got);
}

sp_status sp_parser_next(sp_parser *p, sp_segment *seg, bool *have)
{
    uint8_t b;
    uint8_t code;
    uint8_t lenbuf[2];
    uint16_t length;
    uint32_t fill = 0;
    uint64_t marker_off;
    size_t got = 0;
    sp_status st;

    if (have != NULL)
        *have = false;
    if (p == NULL || p->f == NULL || seg == NULL || have == NULL)
        return SP_ERR_USAGE;
    if (p->finished)
        return SP_OK;

    if (p->count >= SP_MAX_SEGMENTS)
        return SP_ERR_MALFORMED;

    memset(seg, 0, sizeof *seg);
    marker_off = sp_tell(p->f);
    if (marker_off == UINT64_MAX)
        return SP_ERR_IO;

    /* Every marker begins with 0xFF. Anything else here means the previous
     * segment's length lied and we are standing in the middle of something. */
    st = read_byte(p->f, &b);
    if (st == SP_ERR_TRUNCATED) {
        /* Ran out of file where a marker should start. If we never reached
         * SOS the file is incomplete; say so rather than pretending it ended
         * cleanly. */
        p->finished = true;
        return p->saw_soi ? SP_ERR_TRUNCATED : SP_ERR_NOT_JPEG;
    }
    if (st != SP_OK)
        return st;
    if (b != SP_MARK_PREFIX)
        return SP_ERR_MALFORMED;

    /* Fact 3: any number of extra 0xFF bytes may precede the code. */
    for (;;) {
        st = read_byte(p->f, &code);
        if (st == SP_ERR_TRUNCATED)
            return SP_ERR_TRUNCATED;
        if (st != SP_OK)
            return st;
        if (code != SP_MARK_PREFIX)
            break;
        fill++;
        if (fill > SP_MAX_FILL)
            return SP_ERR_MALFORMED;
    }

    /* 0xFF00 is a stuffed data byte. It is legal inside the entropy-coded
     * scan, and the parser never walks there, so seeing one here means the
     * stream is desynchronised. */
    if (code == SP_MARK_STUFF)
        return SP_ERR_MALFORMED;

    if (p->count == 0 && code != SP_MARK_SOI)
        return SP_ERR_NOT_JPEG;
    if (code == SP_MARK_SOI)
        p->saw_soi = true;

    seg->marker = code;
    seg->offset = marker_off;
    seg->fill = (uint16_t)fill;

    if (sp_marker_is_standalone(code)) {
        seg->standalone = true;
        seg->payload_off = sp_tell(p->f);
        p->count++;
        p->pos = seg->payload_off;
        /* EOI ends the image. Without this, a file that stops cleanly at EOI
         * — which a JPEG carrying no scan legitimately does — would come back
         * on the next call as a truncation. */
        if (code == SP_MARK_EOI)
            p->finished = true;
        *have = true;
        return SP_OK;
    }

    st = sp_read_exact(p->f, lenbuf, 2u, &got);
    if (st != SP_OK)
        return (st == SP_ERR_TRUNCATED) ? SP_ERR_TRUNCATED : st;

    /* Fact 1: big-endian, and the two length bytes count themselves. Built
     * one byte at a time on purpose — casting a byte pointer to uint16_t*
     * would be both an endianness and an alignment bug. */
    length = (uint16_t)(((uint16_t)lenbuf[0] << 8) | (uint16_t)lenbuf[1]);
    if (length < 2u)
        return SP_ERR_MALFORMED;

    seg->length = length;
    seg->payload_off = sp_tell(p->f);
    seg->payload_len = (uint32_t)(length - 2u);

    if (seg->payload_off == UINT64_MAX)
        return SP_ERR_IO;
    if (seg->payload_off + seg->payload_len > p->f->size)
        return SP_ERR_TRUNCATED;

    /* Capture the identifying prefix in the same forward pass. */
    if (seg->payload_len > 0u) {
        size_t want = seg->payload_len < SP_SEG_PREFIX_MAX
                          ? (size_t)seg->payload_len
                          : (size_t)SP_SEG_PREFIX_MAX;
        st = sp_read_exact(p->f, seg->prefix, want, &got);
        if (st != SP_OK)
            return (st == SP_ERR_TRUNCATED) ? SP_ERR_TRUNCATED : st;
        seg->prefix_len = (uint8_t)got;

        st = sp_skip(p->f, (uint64_t)seg->payload_len - (uint64_t)got);
        if (st != SP_OK)
            return st;
    }

    p->count++;
    p->pos = seg->payload_off + seg->payload_len;

    /* Fact 2: past SOS there are no more length fields, so there is nothing
     * further to parse. Hand the remainder to the caller as a byte range. */
    if (code == SP_MARK_SOS) {
        p->scan_off = p->pos;
        p->scan_len = (p->f->size > p->pos) ? (p->f->size - p->pos) : 0u;
        p->finished = true;
    }

    *have = true;
    return SP_OK;
}
