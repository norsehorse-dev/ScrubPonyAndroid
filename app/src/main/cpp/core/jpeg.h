/* JPEG segment layer.
 *
 * A streaming iterator over the segments of a JPEG. It reads forward only,
 * never loads the file into memory, and treats every length field in the
 * file as hostile until proven to fit.
 *
 * The iterator stops at SOS. Everything from there to end of file is
 * entropy-coded scan data with no length fields in it, so there is nothing
 * left to parse and the rewriter in phase 4 copies it verbatim.
 */
#ifndef SP_JPEG_H
#define SP_JPEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "scrubpony.h"

/* Marker bytes. A marker is 0xFF followed by a code; 0xFF00 is a stuffed
 * byte inside scan data and 0xFFFF is fill padding, neither is a marker. */
#define SP_MARK_PREFIX 0xFFu
#define SP_MARK_STUFF  0x00u
#define SP_MARK_SOI    0xD8u
#define SP_MARK_EOI    0xD9u
#define SP_MARK_SOS    0xDAu
#define SP_MARK_TEM    0x01u
#define SP_MARK_RST0   0xD0u
#define SP_MARK_RST7   0xD7u
#define SP_MARK_APP0   0xE0u
#define SP_MARK_APP15  0xEFu
#define SP_MARK_COM    0xFEu

/* How much of each payload is captured for identification. The longest
 * identifier in the policy table is the extended-XMP namespace URI at 35
 * bytes ("http://ns.adobe.com/xmp/extension/\0"), which is also the reason
 * this is 48 rather than 32: a needle longer than the haystack would have to
 * be matched by truncated comparison, and a policy table that silently
 * compares prefixes of prefixes is a policy table that will one day confuse
 * two namespaces that agree for their first 32 bytes. */
#define SP_SEG_PREFIX_MAX 48u

/* A file claiming more segments than this is broken or hostile. Real JPEGs
 * run to a few dozen. */
#define SP_MAX_SEGMENTS 4096u

/* Consecutive 0xFF fill bytes before a marker code. Legal, but a file that
 * is nothing but fill should not be walked one byte at a time forever. */
#define SP_MAX_FILL 4096u

typedef struct {
    uint8_t  marker;      /* the code byte following 0xFF                   */
    uint64_t offset;      /* file offset of that 0xFF                       */
    uint16_t fill;        /* extra 0xFF fill bytes skipped before the code  */
    bool     standalone;  /* no length field, no payload                    */
    uint16_t length;      /* raw length field as stored; 0 when standalone  */
    uint64_t payload_off; /* file offset of the first payload byte          */
    uint32_t payload_len; /* length - 2; 0 when standalone                  */

    /* First bytes of the payload, for identifying which flavour of APPn
     * this is. Captured during the same pass, so phase 3 needs no seek. */
    uint8_t  prefix[SP_SEG_PREFIX_MAX];
    uint8_t  prefix_len;
} sp_segment;

typedef struct {
    sp_file *f;
    uint64_t pos;       /* next byte to examine                            */
    uint32_t count;     /* segments emitted so far                         */
    bool     saw_soi;
    bool     finished;  /* SOS reached, or stream ended                    */
    uint64_t scan_off;  /* first byte of entropy-coded data; 0 if no SOS   */
    uint64_t scan_len;  /* bytes from scan_off to end of file              */
} sp_parser;

/* True if this marker code carries no length field and no payload.
 * Reading a length for one of these desynchronises the parser, which is
 * fact 4 in the planning document and the reason this is its own function. */
bool sp_marker_is_standalone(uint8_t code);

/* True if this is an APPn marker, APP0 through APP15. */
bool sp_marker_is_app(uint8_t code);

/* True if buf begins with the two-byte SOI marker FF D8.
 * len < 2 is false, never a read past the end. */
bool sp_jpeg_has_soi(const uint8_t *buf, size_t len);

/* Reads the first two bytes of an already-open file and reports whether it
 * is a JPEG. Leaves the stream positioned immediately after the SOI on
 * success; position is unspecified on failure.
 *   SP_OK            it is a JPEG
 *   SP_ERR_NOT_JPEG  it is not, including the empty and one-byte cases
 *   SP_ERR_IO        the read failed
 * Deliberately does not consult the filename: the extension is a claim, not
 * evidence. */
sp_status sp_jpeg_probe(sp_file *f);

/* Rewinds f and prepares to walk it. Does not read anything yet. */
sp_status sp_parser_init(sp_parser *p, sp_file *f);

/* Produces the next segment.
 *   SP_OK with *have true    seg is filled in
 *   SP_OK with *have false   clean end of the segment stream
 *   SP_ERR_NOT_JPEG          the first segment was not SOI
 *   SP_ERR_TRUNCATED         the file ended inside a segment
 *   SP_ERR_MALFORMED         desync, bad length, or too many segments
 * The first call always yields SOI. The last yields SOS, after which
 * p->scan_off and p->scan_len describe the entropy-coded remainder. */
sp_status sp_parser_next(sp_parser *p, sp_segment *seg, bool *have);

/* Reads up to max bytes of a segment's payload into buf, for the cases where
 * the captured prefix is not enough — currently only the EXIF orientation
 * read, which needs the TIFF header and IFD0.
 *
 * Seeks and restores the stream position, so it is safe to call between
 * sp_parser_next() calls. *got receives the number of bytes actually read,
 * which is min(max, seg->payload_len). */
sp_status sp_parser_read_payload(sp_parser *p, const sp_segment *seg,
                                 uint8_t *buf, size_t max, size_t *got);

#endif /* SP_JPEG_H */
