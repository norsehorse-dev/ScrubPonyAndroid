#include "report.h"

#include <string.h>

const char *sp_marker_name(uint8_t code)
{
    switch (code) {
    case 0x01u: return "TEM";

    case 0xC0u: return "SOF0";
    case 0xC1u: return "SOF1";
    case 0xC2u: return "SOF2";
    case 0xC3u: return "SOF3";
    case 0xC4u: return "DHT";
    case 0xC5u: return "SOF5";
    case 0xC6u: return "SOF6";
    case 0xC7u: return "SOF7";
    case 0xC8u: return "JPG";
    case 0xC9u: return "SOF9";
    case 0xCAu: return "SOF10";
    case 0xCBu: return "SOF11";
    case 0xCCu: return "DAC";
    case 0xCDu: return "SOF13";
    case 0xCEu: return "SOF14";
    case 0xCFu: return "SOF15";

    case 0xD0u: return "RST0";
    case 0xD1u: return "RST1";
    case 0xD2u: return "RST2";
    case 0xD3u: return "RST3";
    case 0xD4u: return "RST4";
    case 0xD5u: return "RST5";
    case 0xD6u: return "RST6";
    case 0xD7u: return "RST7";
    case 0xD8u: return "SOI";
    case 0xD9u: return "EOI";
    case 0xDAu: return "SOS";
    case 0xDBu: return "DQT";
    case 0xDCu: return "DNL";
    case 0xDDu: return "DRI";
    case 0xDEu: return "DHP";
    case 0xDFu: return "EXP";

    case 0xE0u: return "APP0";
    case 0xE1u: return "APP1";
    case 0xE2u: return "APP2";
    case 0xE3u: return "APP3";
    case 0xE4u: return "APP4";
    case 0xE5u: return "APP5";
    case 0xE6u: return "APP6";
    case 0xE7u: return "APP7";
    case 0xE8u: return "APP8";
    case 0xE9u: return "APP9";
    case 0xEAu: return "APP10";
    case 0xEBu: return "APP11";
    case 0xECu: return "APP12";
    case 0xEDu: return "APP13";
    case 0xEEu: return "APP14";
    case 0xEFu: return "APP15";

    case 0xFEu: return "COM";
    default:    break;
    }
    return "?";
}

void sp_prefix_label(const sp_segment *seg, char *buf, size_t buflen)
{
    size_t i;
    size_t out = 0;
    size_t limit;

    if (buf == NULL || buflen == 0u)
        return;
    buf[0] = '\0';
    if (seg == NULL || seg->prefix_len == 0u || buflen < 4u)
        return;

    /* Leave room for the two quotes and the terminator. */
    limit = buflen - 3u;
    if (limit > 16u)
        limit = 16u;

    buf[out++] = '"';
    for (i = 0; i < seg->prefix_len && out < limit + 1u; i++) {
        uint8_t c = seg->prefix[i];
        if (c < 0x20u || c > 0x7Eu)
            break;
        buf[out++] = (char)c;
    }

    /* Nothing printable at all: report no label rather than a pair of empty
     * quotes, which would read as "there is an empty identifier here". */
    if (out == 1u) {
        buf[0] = '\0';
        return;
    }
    buf[out++] = '"';
    buf[out] = '\0';
}

void sp_report_file_header(FILE *out, const sp_file *f)
{
    if (out == NULL || f == NULL)
        return;
    fprintf(out, "%s: %llu bytes\n",
            (f->path != NULL) ? f->path : "(unnamed)",
            (unsigned long long)f->size);
}

void sp_report_segment(FILE *out, const sp_segment *seg)
{
    char label[24];

    if (out == NULL || seg == NULL)
        return;

    sp_prefix_label(seg, label, sizeof label);

    fprintf(out, "  0x%08llx  %-6s", (unsigned long long)seg->offset,
            sp_marker_name(seg->marker));

    if (seg->standalone)
        fprintf(out, "  %8s", "-");
    else
        fprintf(out, "  %8lu", (unsigned long)seg->payload_len);

    if (label[0] != '\0')
        fprintf(out, "  %s", label);
    if (seg->fill > 0u)
        fprintf(out, "  [%u fill byte%s]", (unsigned)seg->fill,
                (seg->fill == 1u) ? "" : "s");

    fputc('\n', out);
}

void sp_report_scan(FILE *out, const sp_parser *p)
{
    if (out == NULL || p == NULL || p->scan_off == 0u)
        return;
    fprintf(out, "  0x%08llx  %-6s  %8llu  entropy-coded, copied verbatim\n",
            (unsigned long long)p->scan_off, "scan",
            (unsigned long long)p->scan_len);
}

void sp_report_decision_header(FILE *out)
{
    if (out == NULL)
        return;
    fprintf(out, "  action  marker   payload  contents\n");
}

void sp_report_decision(FILE *out, const sp_segment *seg, const sp_decision *d)
{
    if (out == NULL || seg == NULL || d == NULL)
        return;

    fprintf(out, "  %-6s  %-6s  %8lu  %s",
            (d->action == SP_DROP) ? "drop" : "keep",
            sp_marker_name(seg->marker),
            (unsigned long)seg->payload_len,
            d->label);

    if (d->reason != NULL && d->reason[0] != '\0')
        fprintf(out, "  (%s)", d->reason);

    fputc('\n', out);
}

void sp_report_exif(FILE *out, const sp_exif_info *info)
{
    if (out == NULL || info == NULL || !info->valid)
        return;

    fprintf(out, "  EXIF: %s byte order, %u entries in IFD0",
            info->big_endian ? "big-endian" : "little-endian",
            (unsigned)info->entry_count);
    if (info->has_gps)
        fprintf(out, ", GPS present");
    if (info->has_thumbnail)
        fprintf(out, ", thumbnail present");
    /* The number as well as the name: exiftool reports the number, and a
     * golden comparison against a prose string would be a comparison against
     * this file's wording rather than against the file's contents. */
    if (info->orientation != SP_ORIENT_NONE)
        fprintf(out, ", orientation %u (%s)", (unsigned)info->orientation,
                sp_exif_orientation_name(info->orientation));
    fputc('\n', out);
}

void sp_warn_orientation(FILE *out, const char *path, uint16_t orientation,
                         bool kept)
{
    if (out == NULL || kept || !sp_exif_orientation_matters(orientation))
        return;
    /* The pixels are untouched, exactly as promised, and the image will still
     * appear turned on its side. Saying so is the difference between a caveat
     * and a bug report. */
    fprintf(out,
            SP_NAME ": %s: EXIF says this image is %s; dropping EXIF removes "
            "that instruction and viewers will show it unrotated\n",
            (path != NULL) ? path : "(unnamed)",
            sp_exif_orientation_name(orientation));
}
