/* PNG chunk layer.
 *
 * A streaming iterator over the chunks of a PNG, the same shape as jpeg.h's
 * segment iterator and for the same reason: it reads forward only, never
 * loads the file into memory, and treats every length field as hostile
 * until proven to fit.
 *
 * PNG's container is simpler than JPEG's in one respect that matters here:
 * a chunk's 4-byte type already says what it is — there is no second,
 * payload-level identifier to peek at the way APP1 needs "Exif\0\0" versus
 * an XMP URI to tell EXIF and XMP apart. Classification (pngpolicy.h) is
 * therefore a lookup on the type alone.
 *
 * It differs from JPEG in one respect that also matters: every chunk ends
 * with a CRC-32 over its own type and data. This layer does not verify it —
 * see pngrewrite.h for where and why that happens instead.
 */
#ifndef SP_PNG_H
#define SP_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "scrubpony.h"

#define SP_PNG_SIGNATURE_LEN 8u
extern const uint8_t SP_PNG_SIGNATURE[SP_PNG_SIGNATURE_LEN];

/* PNG caps a chunk's declared length at 2^31 - 1 by specification (the
 * length field is defined as a non-negative 4-byte integer, so the top bit
 * is never legitimately set). Anything larger is a malformed file, not an
 * unusually large photo. */
#define SP_PNG_MAX_CHUNK_LEN 0x7FFFFFFFu

/* Belt-and-suspenders only: the byte-consumption bound (every chunk costs at
 * least 12 bytes) already makes an unbounded loop impossible on a bounded
 * sp_file. This exists so a hostile file reports "malformed" promptly
 * instead of walking a few million zero-length chunks first. */
#define SP_PNG_MAX_CHUNKS 1000000u

typedef struct {
    char     type[5];     /* 4-letter ASCII chunk type, NUL-terminated      */
    uint64_t offset;      /* file offset of the length field                */
    uint32_t length;      /* chunk data length, from the length field       */
    uint64_t data_off;    /* file offset of the first data byte             */
    uint64_t crc_off;     /* file offset of the trailing 4-byte CRC         */
} sp_png_chunk;

typedef struct {
    sp_file *f;
    uint64_t pos;         /* next byte to examine                           */
    uint32_t count;       /* chunks emitted so far                          */
    bool     saw_ihdr;
    bool     finished;    /* IEND reached, or stream ended                  */
} sp_png_parser;

/* True if buf begins with the 8-byte PNG signature.
 * len < 8 is false, never a read past the end. */
bool sp_png_has_signature(const uint8_t *buf, size_t len);

/* Reads the signature from an already-open file and reports whether it is a
 * PNG. Leaves the stream positioned immediately after the signature on
 * success; position is unspecified on failure — callers trying another
 * format's probe next must seek back to 0 first, same as sp_jpeg_probe.
 *   SP_OK          it is a PNG
 *   SP_ERR_NOT_PNG it is not
 *   SP_ERR_IO      the read failed
 * Deliberately does not consult the filename: the extension is a claim, not
 * evidence. */
sp_status sp_png_probe(sp_file *f);

/* Rewinds f and prepares to walk it. Does not read anything yet. */
sp_status sp_png_parser_init(sp_png_parser *p, sp_file *f);

/* Produces the next chunk.
 *   SP_OK with *have true    chunk is filled in, stream left just after its
 *                             CRC (i.e. positioned at the next chunk)
 *   SP_OK with *have false   IEND has been consumed; nothing left to walk
 *   SP_ERR_TRUNCATED         the file ended inside a chunk
 *   SP_ERR_MALFORMED         a bad length, too many chunks, or the first
 *                             chunk was not IHDR
 * The first call always yields IHDR. */
sp_status sp_png_parser_next(sp_png_parser *p, sp_png_chunk *chunk, bool *have);

/* Reads up to max bytes of a chunk's data into buf, for the cases where only
 * the start of it is needed — classifying an iTXt chunk by its keyword, or
 * reading an eXIf chunk's TIFF blob for the orientation scan. Mirrors
 * sp_parser_read_payload on the JPEG side exactly: *got receives
 * min(max, chunk->length), never an error just for being short. Seeks and
 * restores the stream position, so it is safe to call between
 * sp_png_parser_next() calls.
 *
 * A KEPT chunk is never read this way — see pngrewrite.c, which copies the
 * whole chunk (length, type, data and CRC together) as one untouched byte
 * range, so there is nothing here to get wrong about recomputing a CRC over
 * data nothing actually changed. */
sp_status sp_png_parser_read_prefix(sp_png_parser *p, const sp_png_chunk *chunk,
                                    uint8_t *buf, size_t max, size_t *got);

#endif /* SP_PNG_H */
