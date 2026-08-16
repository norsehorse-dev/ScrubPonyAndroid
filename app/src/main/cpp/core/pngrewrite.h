/* The PNG rewriter — the counterpart to rewrite.c, for chunks instead of
 * segments.
 *
 * The one promise this file has to keep is the same one rewrite.c keeps:
 * every chunk that survives policy is copied byte for byte, never rebuilt.
 * For PNG that promise is easier to state, because a kept chunk's length,
 * type, data and CRC are copied together as a single untouched range — there
 * is no JPEG-style "rebuild the length field, drop the fill bytes" step,
 * because PNG has neither fill bytes nor any reason to distrust a length
 * this same pass already validated.
 */
#ifndef SP_PNGREWRITE_H
#define SP_PNGREWRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "exif.h"
#include "io.h"
#include "policy.h"
#include "rewrite.h" /* sp_rewrite_stats — shared with the JPEG side */
#include "scrubpony.h"

/* Total on-the-wire size of the synthetic eXIf chunk: length field (4) +
 * type (4) + the TIFF blob + CRC (4). Exposed so main.c's dry-run/verbose
 * messaging can report the real number instead of JPEG's 36-byte figure. */
#define SP_PNG_EXIF_ORIENT_CHUNK_LEN \
    (4u + 4u + (SP_EXIF_ORIENT_SEG_LEN - 4u - SP_EXIF_ID_LEN) + 4u)

/* Writes a scrubbed copy of in to out, applying pol. Does not commit — same
 * contract as sp_rewrite(): the caller decides whether to publish the
 * result, and any error return means it must not.
 *
 * Reuses sp_rewrite_stats from rewrite.h rather than defining a parallel
 * struct: every field but scan_off/scan_len means the same thing for a PNG
 * as it does for a JPEG, and main.c's reporting code already only reads the
 * fields that do. scan_off/scan_len are JPEG's entropy-scan bookkeeping and
 * are left zero here. */
sp_status sp_png_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                         FILE *listing, sp_rewrite_stats *stats);

/* PNG's eXIf chunk carries the same TIFF-structured EXIF data JPEG's APP1
 * does, with one difference: the chunk type already says "this is EXIF," so
 * the data skips the six-byte "Exif\0\0" identifier JPEG needs to
 * distinguish APP1-as-EXIF from APP1-as-XMP. exif.c's reader expects that
 * identifier. Rather than teach exif.c two payload shapes, these two
 * functions adapt a PNG eXIf chunk's raw data to the shape exif.c already
 * knows, so exif.c itself — the most heavily fuzzed file in the project —
 * stays untouched. */
sp_status sp_png_exif_scan(const uint8_t *tiff_blob, size_t len,
                           sp_exif_info *out);
bool sp_png_exif_is_minimal_orientation(const uint8_t *tiff_blob, size_t len);

#endif /* SP_PNGREWRITE_H */
