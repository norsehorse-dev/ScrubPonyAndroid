#include "webprewrite.h"

#include <string.h>

#include "report.h"
#include "webp.h"
#include "webppolicy.h"

/* VP8X's one-byte flags field, MSB to LSB: 2 reserved bits, ICC, Alpha,
 * EXIF, XMP, Animation, 1 reserved bit. Alpha and Animation are never
 * touched here — nothing this tool drops changes whether an ALPH chunk or
 * an animation exists — so only the other four bits are ever computed
 * rather than copied through. */
#define SP_WEBP_VP8X_FLAG_ICC   0x20u
#define SP_WEBP_VP8X_FLAG_ALPHA 0x10u
#define SP_WEBP_VP8X_FLAG_EXIF  0x08u
#define SP_WEBP_VP8X_FLAG_XMP   0x04u
#define SP_WEBP_VP8X_FLAG_ANIM  0x02u

sp_status sp_webp_exif_scan(const uint8_t *tiff_blob, size_t len,
                            sp_exif_info *out)
{
    /* static, not on-stack, matching how the JPEG and PNG sides avoid
     * putting a 64KB+ buffer on the stack for the same scan. */
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

bool sp_webp_exif_is_minimal_orientation(const uint8_t *tiff_blob, size_t len)
{
    static uint8_t combined[SP_EXIF_ID_LEN + SP_EXIF_MAX_SCAN];

    if (tiff_blob == NULL || len > SP_EXIF_MAX_SCAN)
        return false;

    memcpy(combined, SP_EXIF_ID, SP_EXIF_ID_LEN);
    memcpy(combined + SP_EXIF_ID_LEN, tiff_blob, len);
    return sp_exif_is_minimal_orientation(combined, SP_EXIF_ID_LEN + len);
}

/* Everything the write pass needs to know before it writes its first byte:
 * the total output size (for the RIFF header's size field), the original
 * VP8X payload if there is one (for its width/height, and the bits of its
 * flags byte this tool never touches), and whatever the JPEG/PNG sides also
 * compute up front — the EXIF orientation and whether it will be
 * re-emitted. One walk of the input computes all of it, the same file
 * webp_rewrite's own walk visits again afterwards; nothing here mutates
 * `in`, so seeing it twice is safe. */
typedef struct {
    sp_exif_info exif;
    bool         have_exif;
    bool         emit_orientation;
    bool         had_vp8x;
    uint8_t      vp8x_payload[10];
    uint64_t     payload_size; /* bytes after "WEBP" in the rewritten file */
} sp_webp_prescan;

static void prescan(sp_file *in, const sp_policy *pol, sp_webp_prescan *out)
{
    sp_webp_parser p;
    sp_webp_chunk chunk;
    bool have = false;
    sp_status st;

    memset(out, 0, sizeof *out);
    out->payload_size = 4u; /* "WEBP" itself */

    if (sp_webp_parser_init(&p, in) != SP_OK)
        return;

    for (;;) {
        sp_webp_decision d;
        uint64_t on_wire;

        st = sp_webp_parser_next(&p, &chunk, &have);
        if (st != SP_OK || !have)
            break;

        on_wire = 8u + (uint64_t)chunk.length + (chunk.padded ? 1u : 0u);

        if (p.count == 1u && strcmp(chunk.fourcc, "VP8X") == 0) {
            size_t got = 0;
            out->had_vp8x = true;
            if (chunk.length == sizeof out->vp8x_payload)
                (void)sp_webp_parser_read_prefix(&p, &chunk, out->vp8x_payload,
                                                 sizeof out->vp8x_payload, &got);
            /* Always kept and always rebuilt, so its size is accounted for
             * here rather than through the generic keep/drop path below. */
            out->payload_size += on_wire;
            continue;
        }

        d = sp_webppolicy_decide(pol, &chunk);

        if (sp_webpkind_is_exif(d.kind) && !out->have_exif) {
            static uint8_t buf[SP_EXIF_MAX_SCAN];
            size_t got = 0;
            if (sp_webp_parser_read_prefix(&p, &chunk, buf, sizeof buf, &got)
                    == SP_OK) {
                if (sp_webp_exif_scan(buf, got, &out->exif) == SP_OK)
                    out->have_exif = true;
            }
        }

        if (d.action == SP_KEEP)
            out->payload_size += on_wire;
    }

    out->emit_orientation =
        out->have_exif &&
        (pol == NULL || !pol->no_orientation) &&
        sp_exif_orientation_matters(out->exif.orientation);

    if (out->emit_orientation)
        out->payload_size += (uint64_t)SP_WEBP_EXIF_ORIENT_CHUNK_LEN;
}

static sp_status write_u32_le(sp_out *out, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
    return sp_out_write(out, b, sizeof b);
}

/* Rebuilds the 10-byte VP8X payload: canvas width and height (the last six
 * bytes) are copied through from the original exactly, because nothing this
 * tool does changes what the image looks like. The flags byte (the first)
 * and the three reserved bytes after it (spec-mandated zero) are the only
 * part actually rebuilt — Alpha and Animation keep whatever the original
 * declared, since ALPH/ANIM/ANMF are never dropped; ICC follows whether the
 * profile survives strict mode; EXIF follows whether the orientation-only
 * chunk is about to be emitted; XMP is always cleared, because XMP is always
 * dropped. */
static sp_status write_rebuilt_vp8x(sp_out *out, const uint8_t orig[10],
                                    const sp_policy *pol, bool emit_orientation)
{
    uint8_t data[10];
    bool strict = (pol != NULL) && pol->strict;
    uint8_t flags;
    sp_status st;

    flags = (uint8_t)(orig[0] & (SP_WEBP_VP8X_FLAG_ALPHA | SP_WEBP_VP8X_FLAG_ANIM));
    if (!strict)
        flags = (uint8_t)(flags | (orig[0] & SP_WEBP_VP8X_FLAG_ICC));
    if (emit_orientation)
        flags = (uint8_t)(flags | SP_WEBP_VP8X_FLAG_EXIF);

    memcpy(data, orig, sizeof data);
    data[0] = flags;
    data[1] = 0u;
    data[2] = 0u;
    data[3] = 0u;

    st = sp_out_write(out, "VP8X", 4u);
    if (st != SP_OK)
        return st;
    st = write_u32_le(out, (uint32_t)sizeof data);
    if (st != SP_OK)
        return st;
    return sp_out_write(out, data, sizeof data);
}

/* The synthetic EXIF chunk's data: the same 26-byte TIFF blob PNG's
 * synthetic eXIf chunk carries — see pngrewrite.c's identical helper for
 * where those bytes come from. */
#define SP_WEBP_EXIF_ORIENT_DATA_LEN \
    (SP_EXIF_ORIENT_SEG_LEN - 4u - SP_EXIF_ID_LEN)

static sp_status write_synthetic_exif_chunk(sp_out *out, uint16_t orientation)
{
    uint8_t full[SP_EXIF_ORIENT_SEG_LEN];
    uint8_t data[SP_WEBP_EXIF_ORIENT_DATA_LEN];
    size_t flen = 0;
    sp_status st;

    st = sp_exif_build_orientation(orientation, full, sizeof full, &flen);
    if (st != SP_OK)
        return st;
    memcpy(data, full + 4u + SP_EXIF_ID_LEN, sizeof data);

    st = sp_out_write(out, "EXIF", 4u);
    if (st != SP_OK)
        return st;
    st = write_u32_le(out, (uint32_t)sizeof data);
    if (st != SP_OK)
        return st;
    /* Always even, so RIFF's padding rule never applies to it. */
    return sp_out_write(out, data, sizeof data);
}

sp_status sp_webp_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                          FILE *listing, sp_rewrite_stats *stats)
{
    sp_webp_parser p;
    sp_webp_chunk chunk;
    sp_webp_prescan pre;
    bool have = false;
    sp_status st;

    if (in == NULL || out == NULL || stats == NULL)
        return SP_ERR_USAGE;

    memset(stats, 0, sizeof *stats);
    stats->in_size = in->size;

    prescan(in, pol, &pre);
    stats->exif = pre.exif;
    stats->have_exif = pre.have_exif;

    /* The size field genuinely is a 32-bit quantity on the wire; this is
     * cheap insurance against a pathological input, not a bound this tool
     * could ever approach in practice, since removal only ever shrinks the
     * total besides the one small synthetic chunk. */
    if (pre.payload_size > 0xFFFFFFFFu)
        return SP_ERR_MALFORMED;

    st = sp_webp_parser_init(&p, in);
    if (st != SP_OK)
        return st;

    /* This is the one place WebP support cannot follow PNG's "only ever
     * append, never patch" rule: RIFF's size field describes everything
     * that follows it, so it has to be written correctly before the first
     * chunk is, which is exactly what prescan() above made possible. */
    st = sp_out_write(out, "RIFF", 4u);
    if (st != SP_OK)
        return st;
    st = write_u32_le(out, (uint32_t)pre.payload_size);
    if (st != SP_OK)
        return st;
    st = sp_out_write(out, "WEBP", 4u);
    if (st != SP_OK)
        return st;

    for (;;) {
        sp_webp_decision d;
        bool is_vp8x;

        st = sp_webp_parser_next(&p, &chunk, &have);
        if (st != SP_OK)
            return st;
        if (!have)
            break;

        is_vp8x = (p.count == 1u && strcmp(chunk.fourcc, "VP8X") == 0);

        if (is_vp8x) {
            if (chunk.length != sizeof pre.vp8x_payload)
                return SP_ERR_MALFORMED;
            st = write_rebuilt_vp8x(out, pre.vp8x_payload, pol,
                                    pre.emit_orientation);
            if (st != SP_OK)
                return st;
            stats->kept++;

            /* The extended header is always first and always kept; right
             * after it, before anything the original file carried, is
             * where the synthetic orientation chunk belongs — the same
             * "right after the structural start" placement rewrite.c uses
             * for JPEG and pngrewrite.c uses for PNG. */
            if (pre.emit_orientation) {
                st = write_synthetic_exif_chunk(out, pre.exif.orientation);
                if (st != SP_OK)
                    return st;
                stats->orientation_kept = true;
            }
            continue;
        }

        d = sp_webppolicy_decide(pol, &chunk);

        if (listing != NULL && !sp_webpkind_is_structural(d.kind))
            sp_report_webp_decision(listing, &chunk, &d);

        if (d.action == SP_DROP) {
            stats->dropped++;
        } else {
            stats->kept++;
            /* Nothing about a kept chunk changes, so nothing about it is
             * rebuilt: FourCC, length, data and pad byte are copied as one
             * untouched range, the same promise PNG's rewriter keeps for
             * every chunk. */
            st = sp_copy_range(in, out, chunk.offset,
                               (chunk.data_off + (uint64_t)chunk.length +
                                (chunk.padded ? 1u : 0u)) - chunk.offset);
            if (st != SP_OK)
                return st;
        }
    }

    stats->out_size = out->written;

    /* Removal cannot add bytes. The one exception is the synthetic EXIF
     * chunk, and the allowance is exactly its size on the wire — see
     * SP_WEBP_EXIF_ORIENT_CHUNK_LEN in webprewrite.h, the same figure
     * main.c reports in its verbose/dry-run output, so the two cannot drift
     * apart. */
    if (stats->out_size >
        stats->in_size +
            (stats->orientation_kept ? (uint64_t)SP_WEBP_EXIF_ORIENT_CHUNK_LEN
                                     : 0u))
        return SP_ERR_OUTPUT_GREW;

    return SP_OK;
}
