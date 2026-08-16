#include "rewrite.h"

#include <string.h>

#include "report.h"

/* Emits the two marker bytes. Fill padding before the code is legal but
 * meaningless, and is deliberately not reproduced: it is bytes with no
 * information in them, and the output is a rewrite, not a transcription. */
static sp_status write_marker(sp_out *out, uint8_t code)
{
    uint8_t m[2];
    m[0] = SP_MARK_PREFIX;
    m[1] = code;
    return sp_out_write(out, m, sizeof m);
}

static sp_status write_length(sp_out *out, uint16_t length)
{
    uint8_t l[2];
    /* Big-endian, and counting itself, exactly as it was read. Rebuilt from
     * the parsed value rather than copied from the input so that a length we
     * validated is the length we emit. */
    l[0] = (uint8_t)(length >> 8);
    l[1] = (uint8_t)(length & 0xFFu);
    return sp_out_write(out, l, sizeof l);
}

/* Walks the header far enough to find the EXIF block and read its
 * orientation, then stops. Costs one extra pass over the segments before SOS,
 * which is a few kilobytes; it never touches the scan.
 *
 * The alternative is to emit the synthetic block late, once the original EXIF
 * has been seen. That is simpler and it is what the plan originally proposed,
 * but it puts APP1 after whatever other application segments the file
 * carried. EXIF says APP1 belongs immediately after SOI, and the cost of
 * honouring that is this function. */
static void prescan_orientation(sp_file *in, sp_exif_info *info, bool *have)
{
    sp_parser p;
    sp_segment seg;
    bool got = false;
    sp_status st;

    *have = false;

    if (sp_parser_init(&p, in) != SP_OK)
        return;

    for (;;) {
        sp_decision d;

        st = sp_parser_next(&p, &seg, &got);
        if (st != SP_OK || !got)
            break;

        d = sp_policy_decide(NULL, &seg);
        if (sp_kind_is_exif(d.kind)) {
            static uint8_t buf[SP_EXIF_MAX_SCAN];
            size_t n = 0;
            if (sp_parser_read_payload(&p, &seg, buf, sizeof buf, &n)
                    == SP_OK &&
                sp_exif_scan(buf, n, info) == SP_OK)
                *have = true;
            break;
        }

        if (seg.marker == SP_MARK_SOS)
            break;
    }
}

sp_status sp_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                     FILE *listing, sp_rewrite_stats *stats)
{
    sp_parser p;
    sp_segment seg;
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

    st = sp_parser_init(&p, in);
    if (st != SP_OK)
        return st;

    for (;;) {
        sp_decision d;

        st = sp_parser_next(&p, &seg, &have);
        if (st != SP_OK)
            return st;
        if (!have)
            break;

        d = sp_policy_decide(pol, &seg);

        if (listing != NULL && d.kind != SP_KIND_STRUCTURAL)
            sp_report_decision(listing, &seg, &d);

        if (d.action == SP_DROP) {
            stats->dropped++;
            continue;
        }

        stats->kept++;

        st = write_marker(out, seg.marker);
        if (st != SP_OK)
            return st;

        /* Immediately after SOI, before anything the original carried: this
         * is where EXIF says an APP1 belongs. */
        if (seg.marker == SP_MARK_SOI && emit_orientation) {
            uint8_t block[SP_EXIF_ORIENT_SEG_LEN];
            size_t blen = 0;

            st = sp_exif_build_orientation(stats->exif.orientation, block,
                                           sizeof block, &blen);
            if (st != SP_OK)
                return st;
            st = sp_out_write(out, block, blen);
            if (st != SP_OK)
                return st;
            stats->orientation_kept = true;
        }

        if (!seg.standalone) {
            st = write_length(out, seg.length);
            if (st != SP_OK)
                return st;
            st = sp_copy_range(in, out, seg.payload_off, seg.payload_len);
            if (st != SP_OK)
                return st;
        }

        /* Fact 2: past SOS there are no length fields and nothing to decide.
         * Copy the rest of the file exactly as it is and stop. This single
         * call is the entire lossless guarantee. */
        if (seg.marker == SP_MARK_SOS) {
            stats->scan_off = p.scan_off;
            stats->scan_len = p.scan_len;
            st = sp_copy_range(in, out, p.scan_off, p.scan_len);
            if (st != SP_OK)
                return st;
            break;
        }
    }

    stats->out_size = out->written;

    /* Removal cannot add bytes. The one exception is the synthetic
     * orientation block, and the allowance is exactly its size — hardcoded
     * here next to the only thing that can spend it, so the two cannot drift
     * apart. Anything beyond that means the loop above is wrong, and the
     * right response is to refuse to publish a file built by code that has
     * just proved it does not understand the format. */
    if (stats->out_size >
        stats->in_size + (stats->orientation_kept ? SP_EXIF_ORIENT_SEG_LEN : 0u))
        return SP_ERR_OUTPUT_GREW;

    return SP_OK;
}
