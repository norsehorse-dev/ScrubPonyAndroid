#include "exif.h"

#include <string.h>

/* TIFF tags we care about. */
#define TAG_ORIENTATION 0x0112u
#define TAG_GPS_IFD     0x8825u

/* TIFF field types. */
#define TYPE_SHORT 3u
#define TYPE_LONG  4u

#define TIFF_HEADER_LEN 8u
#define IFD_ENTRY_LEN   12u

/* One helper for both byte orders, routed through a flag, rather than two
 * parallel parsers that can drift apart. Byte at a time: casting the buffer
 * to a uint16_t* would be an alignment bug as well as an endianness one. */
static uint16_t read_u16(const uint8_t *p, bool big)
{
    if (big)
        return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static uint32_t read_u32(const uint8_t *p, bool big)
{
    if (big)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

sp_status sp_exif_scan(const uint8_t *payload, size_t len, sp_exif_info *out)
{
    const uint8_t *tiff;
    size_t tlen;
    bool big;
    uint32_t ifd0;
    uint16_t count;
    uint16_t i;

    if (out == NULL)
        return SP_ERR_USAGE;
    memset(out, 0, sizeof *out);
    if (payload == NULL)
        return SP_ERR_USAGE;

    if (len < SP_EXIF_ID_LEN ||
        memcmp(payload, SP_EXIF_ID, SP_EXIF_ID_LEN) != 0)
        return SP_ERR_NOT_JPEG;

    /* Offsets inside EXIF are relative to the start of the TIFF header, not
     * the start of the segment. Getting this wrong by six bytes is the
     * classic EXIF parsing bug. */
    tiff = payload + SP_EXIF_ID_LEN;
    tlen = len - SP_EXIF_ID_LEN;

    if (tlen < TIFF_HEADER_LEN)
        return SP_ERR_MALFORMED;

    if (tiff[0] == 'I' && tiff[1] == 'I')
        big = false;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
        big = true;
    else
        return SP_ERR_MALFORMED;

    if (read_u16(tiff + 2, big) != 42u)
        return SP_ERR_MALFORMED;

    out->big_endian = big;

    /* The first attacker-controlled offset. Everything after this point is
     * bounded against tlen, which is the segment, not the file. */
    ifd0 = read_u32(tiff + 4, big);
    if (ifd0 < TIFF_HEADER_LEN)
        return SP_ERR_MALFORMED;
    if ((uint64_t)ifd0 + 2u > (uint64_t)tlen)
        return SP_ERR_MALFORMED;

    count = read_u16(tiff + ifd0, big);
    if (count > SP_EXIF_MAX_ENTRIES)
        return SP_ERR_MALFORMED;

    /* count entries plus the four-byte next-IFD pointer must all fit. */
    if ((uint64_t)ifd0 + 2u + (uint64_t)count * IFD_ENTRY_LEN + 4u >
        (uint64_t)tlen)
        return SP_ERR_MALFORMED;

    out->valid = true;
    out->entry_count = count;

    for (i = 0; i < count; i++) {
        const uint8_t *e = tiff + ifd0 + 2u + (size_t)i * IFD_ENTRY_LEN;
        uint16_t tag = read_u16(e, big);
        uint16_t type = read_u16(e + 2, big);
        uint32_t n = read_u32(e + 4, big);

        if (tag == TAG_GPS_IFD) {
            /* The pointer's existence is the interesting fact. We do not
             * follow it: knowing a GPS directory is there is enough to warn,
             * and the whole segment is about to be discarded anyway. */
            out->has_gps = true;
            continue;
        }

        if (tag != TAG_ORIENTATION || n != 1u)
            continue;

        /* Values of four bytes or fewer live inline in the entry's value
         * field, left-justified. A SHORT therefore sits at e+8 in both byte
         * orders, which is why one read_u16 serves both. */
        if (type == TYPE_SHORT) {
            uint16_t v = read_u16(e + 8, big);
            if (v >= SP_ORIENT_NORMAL && v <= SP_ORIENT_MAX)
                out->orientation = v;
        } else if (type == TYPE_LONG) {
            /* Out of spec, but written by some tools. Accept it rather than
             * silently losing the orientation over a type mismatch. */
            uint32_t v = read_u32(e + 8, big);
            if (v >= SP_ORIENT_NORMAL && v <= SP_ORIENT_MAX)
                out->orientation = (uint16_t)v;
        }
    }

    /* IFD1, when present, is the thumbnail directory. Read the pointer to
     * learn that it exists; do not go there.
     *
     * Deliberately not bounds-checked, for the same reason the GPS pointer
     * above is not: a pointer we never dereference needs no bound, and the
     * fact being reported is "a thumbnail directory is declared", not "a
     * thumbnail directory is reachable". Bounds-checking here would also
     * produce a false negative on any EXIF block whose thumbnail sits past
     * SP_EXIF_MAX_SCAN, since tlen is then the truncated read, not the real
     * segment. */
    {
        const uint8_t *next = tiff + ifd0 + 2u + (size_t)count * IFD_ENTRY_LEN;
        out->has_thumbnail = (read_u32(next, big) != 0u);
    }

    return SP_OK;
}

const char *sp_exif_orientation_name(uint16_t orientation)
{
    switch (orientation) {
    case 1u: return "normal";
    case 2u: return "mirrored horizontally";
    case 3u: return "rotated 180";
    case 4u: return "mirrored vertically";
    case 5u: return "mirrored and rotated 270 CW";
    case 6u: return "rotated 90 CW";
    case 7u: return "mirrored and rotated 90 CW";
    case 8u: return "rotated 270 CW";
    default: break;
    }
    return "absent";
}

bool sp_exif_orientation_matters(uint16_t orientation)
{
    return orientation > SP_ORIENT_NORMAL && orientation <= SP_ORIENT_MAX;
}

bool sp_exif_is_minimal_orientation(const uint8_t *payload, size_t len)
{
    uint8_t block[SP_EXIF_ORIENT_SEG_LEN];
    size_t blen = 0;
    sp_exif_info info;

    /* The payload is the segment without its four leading marker and length
     * bytes, so that is the size to expect and the region to compare. */
    if (payload == NULL || len != SP_EXIF_ORIENT_SEG_LEN - 4u)
        return false;

    /* Read it first, so the block we build for comparison carries the same
     * orientation value. Any file that fails to parse cannot be ours. */
    if (sp_exif_scan(payload, len, &info) != SP_OK || !info.valid)
        return false;
    if (info.orientation == SP_ORIENT_NONE)
        return false;

    if (sp_exif_build_orientation(info.orientation, block, sizeof block,
                                  &blen) != SP_OK)
        return false;

    return memcmp(payload, block + 4, len) == 0;
}

sp_status sp_exif_build_orientation(uint16_t orientation, uint8_t *buf,
                                    size_t max, size_t *len)
{
    /* Written out as a table rather than assembled with helper calls, because
     * the whole point of this block is that every byte in it is known and
     * accounted for. The one variable byte is patched in below. */
    static const uint8_t TEMPLATE[SP_EXIF_ORIENT_SEG_LEN] = {
        0xFF, 0xE1,             /* APP1                                     */
        0x00, 0x22,             /* length 34, counting these two bytes      */
        'E', 'x', 'i', 'f', 0x00, 0x00,
        'M', 'M', 0x00, 0x2A,   /* big-endian TIFF, magic 42                */
        0x00, 0x00, 0x00, 0x08, /* IFD0 begins 8 bytes into the TIFF block  */
        0x00, 0x01,             /* one entry                                */
        0x01, 0x12,             /* tag 0x0112, Orientation                  */
        0x00, 0x03,             /* type SHORT                               */
        0x00, 0x00, 0x00, 0x01, /* count 1                                  */
        0x00, 0x00,             /* value at 28..29, patched below           */
        0x00, 0x00,             /* padding: a SHORT is left-justified       */
        0x00, 0x00, 0x00, 0x00  /* next IFD offset 0: no thumbnail          */
    };

    if (len != NULL)
        *len = 0;
    if (buf == NULL || max < SP_EXIF_ORIENT_SEG_LEN)
        return SP_ERR_USAGE;
    if (orientation < SP_ORIENT_NORMAL || orientation > SP_ORIENT_MAX)
        return SP_ERR_USAGE;

    memcpy(buf, TEMPLATE, SP_EXIF_ORIENT_SEG_LEN);

    /* The four-byte value field starts at offset 28:
     *
     *   0..1   FF E1          marker
     *   2..3   00 22          length
     *   4..9   Exif\0\0       identifier
     *  10..13  MM 00 2A       TIFF header
     *  14..17  00 00 00 08    offset to IFD0
     *  18..19  00 01          entry count
     *  20..21  01 12          tag
     *  22..23  00 03          type SHORT
     *  24..27  00 00 00 01    count
     *  28..31  value field    <- here
     *  32..35  00 00 00 00    next IFD
     *
     * Left-justified means the SHORT occupies 28..29 and 30..31 stay zero.
     * Writing it at 30..31 instead puts the value in the padding, where a
     * reader looking at 28..29 finds zero and reports no orientation at all.
     * The template is already zero there, so only the value bytes are set. */
    buf[28] = (uint8_t)(orientation >> 8);
    buf[29] = (uint8_t)(orientation & 0xFFu);

    if (len != NULL)
        *len = SP_EXIF_ORIENT_SEG_LEN;
    return SP_OK;
}
