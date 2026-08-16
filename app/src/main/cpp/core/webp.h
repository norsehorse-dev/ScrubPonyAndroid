/* WebP RIFF chunk layer.
 *
 * A streaming iterator over the chunks of a WebP file, the same shape as
 * png.h's chunk iterator and jpeg.h's segment iterator: it reads forward
 * only, never loads the file into memory, and treats every length field as
 * hostile until proven to fit.
 *
 * WebP's container is RIFF: an 8-byte "RIFF" + little-endian file-size
 * header, a 4-byte "WEBP" form type, and then a sequence of chunks, each a
 * 4-character FourCC, a little-endian 4-byte length, that many bytes of
 * data, and a single zero pad byte if the length was odd. Two things follow
 * from that shape that do not apply to PNG:
 *
 *   - There is no per-chunk CRC. There is instead a file-level size field
 *     covering everything after it, which is why webprewrite.h has to
 *     compute the whole output's size before writing a single byte of it —
 *     see the comment there.
 *   - A FourCC carries no "safe to skip" signal the way PNG's lowercase
 *     first letter does. See webppolicy.h for how classification handles
 *     that gap.
 *
 * The container also enforces WebP's own structural rule: a "simple format"
 * file (no VP8X header) may contain exactly one chunk, VP8 or VP8L, and
 * nothing else — the extended header is what the format requires before any
 * metadata, alpha or animation chunk is allowed to exist at all. A second
 * chunk after a bare VP8/VP8L is therefore malformed, not merely unusual,
 * and is rejected here rather than left for a caller to notice.
 */
#ifndef SP_WEBP_H
#define SP_WEBP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "scrubpony.h"

#define SP_WEBP_RIFF_HEADER_LEN 12u /* "RIFF" + size(4) + "WEBP" */

/* Same ceiling PNG applies to its length field, and for the same reason: the
 * value is a non-negative 4-byte integer by construction, so the top bit is
 * never legitimately set. */
#define SP_WEBP_MAX_CHUNK_LEN 0x7FFFFFFFu

/* Belt-and-suspenders only, same reasoning as PNG's cap: the byte-consumption
 * bound (every chunk costs at least 8 bytes) already makes an unbounded loop
 * impossible on a bounded sp_file. */
#define SP_WEBP_MAX_CHUNKS 1000000u

typedef struct {
    char     fourcc[5];  /* 4-character tag, NUL-terminated                 */
    uint64_t offset;     /* file offset of the FourCC                       */
    uint32_t length;     /* chunk data length, from the chunk's size field  */
    uint64_t data_off;   /* file offset of the first data byte              */
    bool     padded;     /* a zero pad byte follows the data (length odd)   */
} sp_webp_chunk;

typedef struct {
    sp_file *f;
    uint64_t pos;           /* next byte to examine                        */
    uint64_t riff_end;      /* file offset where the RIFF payload ends,
                              * from the header's declared size field       */
    uint32_t count;         /* chunks emitted so far                       */
    bool     finished;
    bool     simple_format; /* first chunk was VP8/VP8L, not VP8X: the spec
                              * allows exactly one chunk in that case       */
} sp_webp_parser;

/* True if buf begins with "RIFF", any 4 bytes, then "WEBP".
 * len < 12 is false, never a read past the end. */
bool sp_webp_has_signature(const uint8_t *buf, size_t len);

/* Reads the 12-byte RIFF/WEBP header from an already-open file and reports
 * whether it is a WebP. Leaves the stream positioned immediately after the
 * header on success; position is unspecified on failure — callers trying
 * another format's probe next must seek back to 0 first, same as
 * sp_jpeg_probe/sp_png_probe.
 *   SP_OK            it is a WebP
 *   SP_ERR_NOT_WEBP  it is not
 *   SP_ERR_IO        the read failed
 * Deliberately does not consult the filename, and does not validate the
 * declared size field against the file's actual size — that happens in the
 * parser, where a mismatch is reported as the more specific
 * SP_ERR_MALFORMED/SP_ERR_TRUNCATED rather than folded into "not a WebP". */
sp_status sp_webp_probe(sp_file *f);

/* Rewinds f and prepares to walk it. Does not read anything yet. */
sp_status sp_webp_parser_init(sp_webp_parser *p, sp_file *f);

/* Produces the next chunk.
 *   SP_OK with *have true    chunk is filled in, stream left just after its
 *                             data (and pad byte, if any)
 *   SP_OK with *have false   the RIFF payload has been fully walked
 *   SP_ERR_NOT_WEBP          the file does not open with RIFF....WEBP
 *   SP_ERR_TRUNCATED         the file ended inside a chunk, or the header's
 *                             declared size reaches past the file's actual
 *                             size
 *   SP_ERR_MALFORMED         a bad length, too many chunks, a non-ASCII
 *                             FourCC, the first chunk was not VP8, VP8L or
 *                             VP8X, or a second chunk followed a simple-
 *                             format VP8/VP8L
 * The first call always yields the first real chunk: VP8X in an extended-
 * format file, VP8 or VP8L in a simple-format one. */
sp_status sp_webp_parser_next(sp_webp_parser *p, sp_webp_chunk *chunk, bool *have);

/* Reads up to max bytes of a chunk's data into buf. Mirrors
 * sp_png_parser_read_prefix exactly, including the seek-and-restore, so it
 * is safe to call between sp_webp_parser_next() calls. */
sp_status sp_webp_parser_read_prefix(sp_webp_parser *p, const sp_webp_chunk *chunk,
                                     uint8_t *buf, size_t max, size_t *got);

#endif /* SP_WEBP_H */
