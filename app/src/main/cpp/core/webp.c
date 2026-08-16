#include "webp.h"

#include <string.h>

bool sp_webp_has_signature(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < SP_WEBP_RIFF_HEADER_LEN)
        return false;
    return memcmp(buf, "RIFF", 4u) == 0 && memcmp(buf + 8u, "WEBP", 4u) == 0;
}

sp_status sp_webp_probe(sp_file *f)
{
    uint8_t hdr[SP_WEBP_RIFF_HEADER_LEN];
    size_t got = 0;
    sp_status st;

    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    st = sp_read_exact(f, hdr, sizeof hdr, &got);
    if (st == SP_ERR_IO)
        return SP_ERR_IO;
    /* A file shorter than the header cannot be a WebP. That is a "not a
     * WebP" answer, not a truncation complaint — there is no structure to
     * have been truncated yet. */
    if (st == SP_ERR_TRUNCATED || got < sizeof hdr)
        return SP_ERR_NOT_WEBP;

    return sp_webp_has_signature(hdr, got) ? SP_OK : SP_ERR_NOT_WEBP;
}

sp_status sp_webp_parser_init(sp_webp_parser *p, sp_file *f)
{
    if (p == NULL || f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    memset(p, 0, sizeof *p);
    p->f = f;
    return sp_seek(f, 0);
}

sp_status sp_webp_parser_read_prefix(sp_webp_parser *p, const sp_webp_chunk *chunk,
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
    /* A short read here means the chunk is truncated, which
     * sp_webp_parser_next would already have reported when it validated
     * data_off + length against the file size — reaching a truncation here
     * instead means the file changed underneath us, an I/O-layer concern
     * rather than a malformed one. */
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

static bool is_fourcc_byte(uint8_t c)
{
    /* Every FourCC the spec defines is printable ASCII, including the
     * spaces that pad "VP8" and "XMP" out to four characters. Anything
     * outside that range means the previous chunk's length lied and this is
     * the middle of something, not the start of a chunk. */
    return c >= 0x20u && c <= 0x7Eu;
}

sp_status sp_webp_parser_next(sp_webp_parser *p, sp_webp_chunk *chunk, bool *have)
{
    uint8_t fourcc[4];
    uint8_t lenbuf[4];
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
    if (p->count >= SP_WEBP_MAX_CHUNKS)
        return SP_ERR_MALFORMED;

    memset(chunk, 0, sizeof *chunk);

    /* The 12-byte RIFF/WEBP header is not chunk-shaped (no FourCC-then-
     * length the way the rest of the file is), so it is consumed silently
     * here rather than surfaced as a pseudo-chunk — exactly the role PNG's
     * signature plays in sp_png_parser_next. The first value this function
     * ever returns to a caller is the first real chunk. */
    if (p->count == 0u) {
        uint8_t hdr[SP_WEBP_RIFF_HEADER_LEN];
        uint32_t declared;

        st = sp_read_exact(p->f, hdr, sizeof hdr, &got);
        if (st != SP_OK) {
            p->finished = true;
            return (st == SP_ERR_TRUNCATED) ? SP_ERR_NOT_WEBP : st;
        }
        if (!sp_webp_has_signature(hdr, sizeof hdr)) {
            p->finished = true;
            return SP_ERR_NOT_WEBP;
        }

        /* Little-endian, per RIFF, and counts everything after itself —
         * "WEBP" and every chunk that follows, but not the "RIFF" tag or
         * the size field's own four bytes. */
        declared = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                   ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        if (declared < 4u) /* must at least cover the "WEBP" it contains */
            return SP_ERR_MALFORMED;

        p->riff_end = 8u + (uint64_t)declared;
        if (p->riff_end > p->f->size) {
            p->finished = true;
            return SP_ERR_TRUNCATED;
        }
    }

    chunk_off = sp_tell(p->f);
    if (chunk_off == UINT64_MAX)
        return SP_ERR_IO;

    /* The stream position, not the p->pos bookkeeping field, is what
     * decides this: right after the count==0 branch above it reflects
     * offset 12 (just past the header) whether or not p->pos has been
     * written yet, so checking it directly avoids a special case for the
     * very first call. Clean end of the RIFF payload is checked before the
     * simple-format rule below, so a well-formed one-chunk file reports a
     * normal *have=false ending rather than tripping over a violation that
     * only applies when there really is more data to look at. */
    if (chunk_off >= p->riff_end) {
        p->finished = true;
        return SP_OK; /* *have is already false */
    }

    if (p->count > 0u && p->simple_format) {
        /* A simple-format file's one and only chunk was already emitted on
         * a previous call, and the RIFF payload still has bytes left —
         * which is only possible if the file lied about being simple
         * format in the first place. */
        p->finished = true;
        return SP_ERR_MALFORMED;
    }

    st = sp_read_exact(p->f, fourcc, sizeof fourcc, &got);
    if (st == SP_ERR_TRUNCATED) {
        p->finished = true;
        return SP_ERR_TRUNCATED;
    }
    if (st != SP_OK)
        return st;

    for (i = 0; i < 4; i++) {
        if (!is_fourcc_byte(fourcc[i]))
            return SP_ERR_MALFORMED;
    }
    if (p->count == 0u &&
        memcmp(fourcc, "VP8 ", 4u) != 0 && memcmp(fourcc, "VP8L", 4u) != 0 &&
        memcmp(fourcc, "VP8X", 4u) != 0)
        return SP_ERR_MALFORMED;

    st = sp_read_exact(p->f, lenbuf, sizeof lenbuf, &got);
    if (st != SP_OK)
        return (st == SP_ERR_TRUNCATED) ? SP_ERR_TRUNCATED : st;

    length = (uint32_t)lenbuf[0] | ((uint32_t)lenbuf[1] << 8) |
             ((uint32_t)lenbuf[2] << 16) | ((uint32_t)lenbuf[3] << 24);
    if (length > SP_WEBP_MAX_CHUNK_LEN)
        return SP_ERR_MALFORMED;

    memcpy(chunk->fourcc, fourcc, 4u);
    chunk->fourcc[4] = '\0';
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

    chunk->padded = (length & 1u) != 0u;
    if (chunk->padded) {
        uint64_t padoff = sp_tell(p->f);
        if (padoff == UINT64_MAX)
            return SP_ERR_IO;
        if (padoff >= p->f->size) {
            p->finished = true;
            return SP_ERR_TRUNCATED;
        }
        st = sp_skip(p->f, 1u);
        if (st != SP_OK)
            return st;
    }

    if (p->count == 0u)
        p->simple_format = memcmp(chunk->fourcc, "VP8X", 4u) != 0;

    p->count++;
    p->pos = sp_tell(p->f);
    if (p->pos == UINT64_MAX)
        return SP_ERR_IO;

    *have = true;
    return SP_OK;
}
