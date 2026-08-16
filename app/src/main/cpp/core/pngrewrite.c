#include "pngrewrite.h"

#include <string.h>

#include "crc32.h"
#include "png.h"
#include "pngpolicy.h"
#include "report.h"

sp_status sp_png_exif_scan(const uint8_t *tiff_blob, size_t len,
                           sp_exif_info *out)
{
    /* static, not on-stack, matching how the JPEG side avoids putting a
     * 64KB+ buffer on the stack for the same scan (see dry_run_file() and
     * prescan_orientation() in main.c/rewrite.c). */
    static uint8_t combined[SP_EXIF_ID_LEN + SP_EXIF_MAX_SCAN];
    size_t n = len;

    if (out == NULL)
        return SP_ERR_USAGE;
    if (tiff_blob == NULL)
        return SP_ERR_USAGE;
    if (n > SP_EXIF_MAX_SCAN)
        n = SP_EXIF_MAX_SCAN;

    memcpy(combined, SP_EXIF_ID, SP_EXIF_ID_LEN);
    memcpy(combined + SP_EXIF_ID_LEN, tiff_blob, n);
    return sp_exif_scan(combined, SP_EXIF_ID_LEN + n, out);
}

bool sp_png_exif_is_minimal_orientation(const uint8_t *tiff_blob, size_t len)
{
    static uint8_t combined[SP_EXIF_ID_LEN + SP_EXIF_MAX_SCAN];

    if (tiff_blob == NULL || len > SP_EXIF_MAX_SCAN)
        return false;

    memcpy(combined, SP_EXIF_ID, SP_EXIF_ID_LEN);
    memcpy(combined + SP_EXIF_ID_LEN, tiff_blob, len);
    return sp_exif_is_minimal_orientation(combined, SP_EXIF_ID_LEN + len);
}

/* Walks the whole file once looking for the first eXIf chunk and reading its
 * orientation — the PNG counterpart of prescan_orientation() in rewrite.c.
 * PNG has no equivalent of "nothing meaningful follows SOS," so this walks
 * to the end rather than stopping early; that costs nothing extra, because
 * chunks this pass is not reading (IDAT above all) are skipped by their
 * length field, never touched. */
static void prescan_orientation(sp_file *in, sp_exif_info *info, bool *have)
{
    sp_png_parser p;
    sp_png_chunk chunk;
    bool got = false;
    sp_status st;

    *have = false;

    if (sp_png_parser_init(&p, in) != SP_OK)
        return;

    for (;;) {
        st = sp_png_parser_next(&p, &chunk, &got);
        if (st != SP_OK || !got)
            break;

        if (strcmp(chunk.type, "eXIf") == 0) {
            static uint8_t buf[SP_EXIF_MAX_SCAN];
            size_t n = 0;
            if (sp_png_parser_read_prefix(&p, &chunk, buf, sizeof buf, &n)
                    == SP_OK &&
                sp_png_exif_scan(buf, n, info) == SP_OK)
                *have = true;
            break;
        }
    }
}

static sp_status write_u32_be(sp_out *out, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v & 0xFFu);
    return sp_out_write(out, b, sizeof b);
}

/* The synthetic eXIf chunk's data: the same 26-byte TIFF blob
 * sp_exif_build_orientation() writes for JPEG, minus the six-byte
 * "Exif\0\0" identifier that only APP1 needs (see pngrewrite.h) and the
 * four-byte JPEG marker+length header that a PNG chunk supplies itself. */
#define SP_PNG_EXIF_ORIENT_DATA_LEN \
    (SP_EXIF_ORIENT_SEG_LEN - 4u - SP_EXIF_ID_LEN)

static sp_status write_synthetic_exif_chunk(sp_out *out, uint16_t orientation)
{
    uint8_t full[SP_EXIF_ORIENT_SEG_LEN];
    uint8_t data[SP_PNG_EXIF_ORIENT_DATA_LEN];
    size_t flen = 0;
    uint32_t crc;
    sp_status st;

    st = sp_exif_build_orientation(orientation, full, sizeof full, &flen);
    if (st != SP_OK)
        return st;
    memcpy(data, full + 4u + SP_EXIF_ID_LEN, sizeof data);

    st = write_u32_be(out, (uint32_t)sizeof data);
    if (st != SP_OK)
        return st;
    st = sp_out_write(out, "eXIf", 4u);
    if (st != SP_OK)
        return st;
    st = sp_out_write(out, data, sizeof data);
    if (st != SP_OK)
        return st;

    crc = sp_crc32((const uint8_t *)"eXIf", 4u);
    crc = sp_crc32_update(crc, data, sizeof data);
    return write_u32_be(out, crc);
}

sp_status sp_png_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                         FILE *listing, sp_rewrite_stats *stats)
{
    sp_png_parser p;
    sp_png_chunk chunk;
    bool have = false;
    bool emit_orientation;
    sp_status st;

    if (in == NULL || out == NULL || stats == NULL)
        return SP_ERR_USAGE;

    memset(stats, 0, sizeof *stats);
    stats->in_size = in->size;

    prescan_orientation(in, &stats->exif, &stats->have_exif);
    emit_orientation =
        stats->have_exif &&
        (pol == NULL || !pol->no_orientation) &&
        sp_exif_orientation_matters(stats->exif.orientation);

    st = sp_png_parser_init(&p, in);
    if (st != SP_OK)
        return st;

    /* The signature is not a chunk sp_png_parser_next() ever hands back
     * (see png.h) — it silently consumes those same 8 bytes itself on its
     * own first call below. Copying them from `in` here, instead of writing
     * the known-good constant, would advance the shared input stream past
     * the bytes that first sp_png_parser_next() call still expects to read
     * for itself, and it would fail the file as SP_ERR_NOT_PNG for finding
     * IHDR's bytes where the signature should be. */
    st = sp_out_write(out, SP_PNG_SIGNATURE, SP_PNG_SIGNATURE_LEN);
    if (st != SP_OK)
        return st;

    for (;;) {
        sp_png_decision d;
        uint8_t keyword_buf[SP_PNGPOLICY_KEYWORD_MAX];
        size_t keyword_len = 0;
        bool is_itxt;

        st = sp_png_parser_next(&p, &chunk, &have);
        if (st != SP_OK)
            return st;
        if (!have)
            break;

        is_itxt = (strcmp(chunk.type, "iTXt") == 0);
        if (is_itxt && chunk.length > 0u) {
            if (sp_png_parser_read_prefix(&p, &chunk, keyword_buf,
                                          sizeof keyword_buf, &keyword_len)
                    != SP_OK)
                keyword_len = 0;
        }

        d = sp_pngpolicy_decide(pol, &chunk, is_itxt ? keyword_buf : NULL,
                                keyword_len);

        if (listing != NULL && d.kind != SP_PNG_KIND_CRITICAL)
            sp_report_png_decision(listing, &chunk, &d);

        if (d.action == SP_DROP) {
            stats->dropped++;
        } else {
            stats->kept++;
            /* Nothing about a kept chunk changes, so nothing about it is
             * rebuilt: length, type, data and CRC are copied as one
             * untouched range. */
            st = sp_copy_range(in, out, chunk.offset,
                               (chunk.crc_off + 4u) - chunk.offset);
            if (st != SP_OK)
                return st;
        }

        /* IHDR is always first (the parser enforces it) and always kept (it
         * is critical). Immediately after it, before anything the original
         * file carried, is where the synthetic orientation chunk belongs —
         * the same "right after the structural start" placement rewrite.c
         * uses for JPEG's synthetic APP1 after SOI. */
        if (strcmp(chunk.type, "IHDR") == 0 && emit_orientation) {
            st = write_synthetic_exif_chunk(out, stats->exif.orientation);
            if (st != SP_OK)
                return st;
            stats->orientation_kept = true;
        }
    }

    stats->out_size = out->written;

    /* Removal cannot add bytes. The one exception is the synthetic eXIf
     * chunk, and the allowance is exactly its size on the wire (see
     * SP_PNG_EXIF_ORIENT_CHUNK_LEN in pngrewrite.h — the same figure main.c
     * reports in its verbose/dry-run output, so the two cannot drift
     * apart). */
    if (stats->out_size >
        stats->in_size +
            (stats->orientation_kept
                 ? (uint64_t)SP_PNG_EXIF_ORIENT_CHUNK_LEN
                 : 0u))
        return SP_ERR_OUTPUT_GREW;

    return SP_OK;
}
