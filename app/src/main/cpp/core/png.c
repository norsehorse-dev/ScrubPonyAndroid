#include "png.h"

#include <string.h>

const uint8_t SP_PNG_SIGNATURE[SP_PNG_SIGNATURE_LEN] = {
    0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au
};

bool sp_png_has_signature(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < SP_PNG_SIGNATURE_LEN)
        return false;
    return memcmp(buf, SP_PNG_SIGNATURE, SP_PNG_SIGNATURE_LEN) == 0;
}

sp_status sp_png_probe(sp_file *f)
{
    uint8_t sig[SP_PNG_SIGNATURE_LEN];
    size_t got = 0;
    sp_status st;

    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    st = sp_read_exact(f, sig, sizeof sig, &got);
    if (st == SP_ERR_IO)
        return SP_ERR_IO;
    /* A file shorter than the signature cannot be a PNG. That is a "not a
     * PNG" answer, not a truncation complaint — there is no structure to
     * have been truncated yet. */
    if (st == SP_ERR_TRUNCATED || got < sizeof sig)
        return SP_ERR_NOT_PNG;

    return sp_png_has_signature(sig, got) ? SP_OK : SP_ERR_NOT_PNG;
}

sp_status sp_png_parser_init(sp_png_parser *p, sp_file *f)
{
    if (p == NULL || f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    memset(p, 0, sizeof *p);
    p->f = f;
    return sp_seek(f, 0);
}

sp_status sp_png_parser_read_prefix(sp_png_parser *p, const sp_png_chunk *chunk,
                                    uint8_t *buf, size_t max, size_t *got)
{
    uint64_t saved;
    size_t want;
    size_t n = 0;
    sp_status st;

    if (got != NULL)
        *got = 0;
    if (p == NULL || p->f == NULL || chunk == NULL || buf == NULL || max == 0u)
        return SP_ERR_USAGE;
    if (chunk->length == 0u)
        return SP_OK;

    want = ((uint64_t)chunk->length < (uint64_t)max) ? (size_t)chunk->length : max;

    saved = sp_tell(p->f);
    if (saved == UINT64_MAX)
        return SP_ERR_IO;

    st = sp_seek(p->f, chunk->data_off);
    if (st == SP_OK)
        st = sp_read_exact(p->f, buf, want, &n);
    /* A short read here means the chunk is truncated, which sp_png_parser_next
     * would already have reported when it validated data_off + length against
     * the file size — reaching a truncation here instead means the file
     * changed underneath us, an I/O-layer concern rather than a malformed
     * one. */
    if (st == SP_ERR_TRUNCATED)
        st = SP_ERR_IO;

    /* Restore position whatever happened, so a failed read here cannot
     * desynchronise a walk still in progress. */
    {
        sp_status rs = sp_seek(p->f, saved);
        if (st == SP_OK && rs != SP_OK)
            st = rs;
    }

    if (got != NULL)
        *got = n;
    return st;
}

static bool is_type_letter(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

sp_status sp_png_parser_next(sp_png_parser *p, sp_png_chunk *chunk, bool *have)
{
    uint8_t lenbuf[4];
    uint8_t typebuf[4];
    uint32_t length;
    uint64_t chunk_off;
    size_t got = 0;
    sp_status st;
    int i;

    if (have != NULL)
        *have = false;
    if (p == NULL || p->f == NULL || chunk == NULL || have == NULL)
        return SP_ERR_USAGE;
    if (p->finished)
        return SP_OK;
    if (p->count >= SP_PNG_MAX_CHUNKS)
        return SP_ERR_MALFORMED;

    memset(chunk, 0, sizeof *chunk);

    /* The signature is not chunk-shaped (no length, no CRC), so it is
     * consumed silently here rather than surfaced as a pseudo-chunk. The
     * first value this function ever returns to a caller is IHDR. */
    if (p->count == 0u) {
        uint8_t sig[SP_PNG_SIGNATURE_LEN];
        st = sp_read_exact(p->f, sig, sizeof sig, &got);
        if (st != SP_OK) {
            p->finished = true;
            return (st == SP_ERR_TRUNCATED) ? SP_ERR_NOT_PNG : st;
        }
        if (!sp_png_has_signature(sig, sizeof sig)) {
            p->finished = true;
            return SP_ERR_NOT_PNG;
        }
    }

    chunk_off = sp_tell(p->f);
    if (chunk_off == UINT64_MAX)
        return SP_ERR_IO;

    st = sp_read_exact(p->f, lenbuf, sizeof lenbuf, &got);
    if (st == SP_ERR_TRUNCATED) {
        p->finished = true;
        /* Ran out of file where a chunk header should start. If IHDR was
         * never reached the file is not a PNG at all; past that point it is
         * a PNG that stops short. */
        return p->saw_ihdr ? SP_ERR_TRUNCATED : SP_ERR_NOT_PNG;
    }
    if (st != SP_OK)
        return st;

    /* Big-endian, per the PNG spec, and never counts itself (unlike JPEG's
     * segment length). Built one byte at a time on purpose — casting a byte
     * pointer to uint32_t* would be both an endianness and an alignment
     * bug. */
    length = ((uint32_t)lenbuf[0] << 24) | ((uint32_t)lenbuf[1] << 16) |
             ((uint32_t)lenbuf[2] << 8) | (uint32_t)lenbuf[3];
    if (length > SP_PNG_MAX_CHUNK_LEN)
        return SP_ERR_MALFORMED;

    st = sp_read_exact(p->f, typebuf, sizeof typebuf, &got);
    if (st != SP_OK)
        return (st == SP_ERR_TRUNCATED) ? SP_ERR_TRUNCATED : st;

    /* Chunk type bytes are defined as ASCII letters only. Anything else here
     * means the previous chunk's length lied and we are standing in the
     * middle of something. */
    for (i = 0; i < 4; i++) {
        if (!is_type_letter(typebuf[i]))
            return SP_ERR_MALFORMED;
    }

    if (p->count == 0u && memcmp(typebuf, "IHDR", 4u) != 0)
        return SP_ERR_MALFORMED;
    if (memcmp(typebuf, "IHDR", 4u) == 0)
        p->saw_ihdr = true;

    memcpy(chunk->type, typebuf, 4u);
    chunk->type[4] = '\0';
    chunk->offset = chunk_off;
    chunk->length = length;
    chunk->data_off = sp_tell(p->f);
    if (chunk->data_off == UINT64_MAX)
        return SP_ERR_IO;
    if (chunk->data_off + (uint64_t)length > p->f->size)
        return SP_ERR_TRUNCATED;

    st = sp_skip(p->f, (uint64_t)length);
    if (st != SP_OK)
        return st;

    chunk->crc_off = sp_tell(p->f);
    if (chunk->crc_off == UINT64_MAX)
        return SP_ERR_IO;
    if (chunk->crc_off + 4u > p->f->size)
        return SP_ERR_TRUNCATED;

    st = sp_skip(p->f, 4u);
    if (st != SP_OK)
        return st;

    p->count++;
    p->pos = chunk->crc_off + 4u;

    if (memcmp(chunk->type, "IEND", 4u) == 0)
        p->finished = true;

    *have = true;
    return SP_OK;
}
