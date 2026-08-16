/* EXIF reading, and only reading.
 *
 * This is the most hostile surface in the project: it is the one place where
 * we follow an offset that was stored inside the file we are parsing. Every
 * bound here is checked against the *segment* length, never the file length,
 * and the next-IFD pointer is read but never chased.
 *
 * Scope is deliberately tiny. We want three facts out of IFD0 — the
 * orientation, whether a GPS directory exists, and whether a thumbnail
 * directory exists — and nothing else. Anything more would be an EXIF
 * library, which is not what this project is.
 */
#ifndef SP_EXIF_H
#define SP_EXIF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scrubpony.h"

/* The EXIF APP1 identifier, including its two padding NULs. */
#define SP_EXIF_ID     "Exif\0\0"
#define SP_EXIF_ID_LEN 6u

/* Orientation values 1..8 as defined by TIFF. 0 means "no tag present",
 * which is not the same as 1 and must not be treated as it. */
#define SP_ORIENT_NONE   0u
#define SP_ORIENT_NORMAL 1u
#define SP_ORIENT_MAX    8u

/* A single IFD is capped at this many entries. Real IFD0s hold a few dozen. */
#define SP_EXIF_MAX_ENTRIES 1024u

/* How much of an EXIF payload is worth reading to find IFD0. Phone EXIF
 * blocks run to a few kilobytes plus the thumbnail; the interesting part is
 * always at the front. */
#define SP_EXIF_MAX_SCAN 65536u

typedef struct {
    bool     valid;         /* a well-formed TIFF header was found      */
    bool     big_endian;    /* "MM" rather than "II"                    */
    uint16_t orientation;   /* SP_ORIENT_NONE if the tag is absent      */
    bool     has_gps;       /* IFD0 carries a GPS directory pointer     */
    bool     has_thumbnail; /* a second IFD follows, i.e. a thumbnail   */
    uint16_t entry_count;   /* entries in IFD0                          */
} sp_exif_info;

/* Scans the payload of an APP1 segment, identifier bytes included.
 *   SP_OK             parsed; *out describes what was found
 *   SP_ERR_NOT_JPEG   payload does not begin with "Exif\0\0"
 *   SP_ERR_MALFORMED  the TIFF header or IFD0 is not self-consistent
 * A malformed EXIF block is never fatal to the caller: the whole segment is
 * being dropped anyway, and the only cost is not knowing the orientation. */
sp_status sp_exif_scan(const uint8_t *payload, size_t len, sp_exif_info *out);

/* "rotate 90 CW", "mirror horizontal", "normal". Never NULL. */
const char *sp_exif_orientation_name(uint16_t orientation);

/* True when dropping the EXIF block will visibly change how the image is
 * displayed — that is, orientation is present and is not 1. */
bool sp_exif_orientation_matters(uint16_t orientation);

/* ---------------------------------------------------------------------- *
 * Writing — the one narrow exception
 *
 * "We do not write metadata" is a design rule, and this is the single place
 * it is broken. The justification is that the block below is 36 bytes,
 * contains exactly one field, and that field is not identifying: it says
 * which way up the picture goes, which anyone can see by looking at it.
 *
 * Without it, every portrait photograph this tool touches comes out sideways
 * in every viewer, because phones do not rotate pixels — they record a tag
 * saying "turn this before displaying" and dropping EXIF drops the
 * instruction along with the GPS coordinates.
 *
 * The exact bytes, big-endian throughout:
 *
 *   FF E1 00 22                            APP1, length 34 (counts itself)
 *   "Exif\0\0"                             6 bytes, EXIF identifier
 *   4D 4D 00 2A                            "MM\0*", big-endian TIFF header
 *   00 00 00 08                            IFD0 is 8 bytes in
 *   00 01                                  IFD0 holds one entry
 *   01 12 00 03 00 00 00 01 00 0N 00 00    tag 0x0112, SHORT, count 1, value N
 *   00 00 00 00                            no IFD1, so no thumbnail
 *
 * 36 bytes on the wire: two for the marker plus the 34 the length field
 * accounts for. A SHORT sits left-justified in the four-byte value field,
 * which is why the value lands at 00 0N 00 00 rather than 00 00 00 0N.
 * ---------------------------------------------------------------------- */

#define SP_EXIF_ORIENT_SEG_LEN 36u

/* Writes the segment above into buf. Fails with SP_ERR_USAGE if the
 * orientation is out of range or buf is too small. */
sp_status sp_exif_build_orientation(uint16_t orientation, uint8_t *buf,
                                    size_t max, size_t *len);

/* True when payload is exactly the payload of the block above — that is, an
 * EXIF segment this tool wrote and nothing else.
 *
 * --check needs this. A photo scrubbed once still carries an EXIF APP1, and
 * policy still drops it, so counting dropped segments would report every
 * rotated photo as carrying identifying metadata forever. It does not: the
 * whole justification for writing those 36 bytes is that they identify
 * nobody. The comparison is against the bytes we would emit, so anything
 * with a second tag, a GPS pointer or a thumbnail fails it. */
bool sp_exif_is_minimal_orientation(const uint8_t *payload, size_t len);

#endif /* SP_EXIF_H */
