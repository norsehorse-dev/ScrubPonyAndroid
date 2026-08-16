/* The WebP rewriter — the counterpart to pngrewrite.h, for RIFF chunks.
 *
 * PNG's rewriter promises that every kept chunk is copied byte for byte,
 * never rebuilt. WebP's rewriter can only keep about three quarters of that
 * promise, for a reason specific to RIFF: the container's own size field, in
 * the 12-byte header at the very front of the file, states the byte count of
 * everything that follows it. Dropping a chunk changes that number, and the
 * field has to be written before any chunk that follows it — there is no
 * write-then-go-back-and-patch step available here, because sp_out is a
 * forward-only stream (see io.h). So the total output size has to be known
 * before the first byte of the file is written, which means walking the
 * input once to compute it before walking it again to actually write —
 * see the prescan in webprewrite.c.
 *
 * The other place this file departs from PNG's "just copy it" rule is the
 * VP8X extended-header chunk, when one is present: its one-byte flags field
 * declares which optional chunks the file carries (an ICC profile, EXIF,
 * XMP, alpha, animation), and dropping EXIF or XMP without also clearing
 * their bits would leave the file's own header lying about its contents.
 * VP8X's canvas width and height fields are untouched — nothing this tool
 * does changes what the image looks like.
 *
 * Everything else — the image data, the extended header's dimensions, an
 * alpha plane, animation chunks, a surviving ICC profile — is still copied
 * as one untouched byte range, the same as PNG.
 *
 * A "simple format" WebP (a bare VP8 or VP8L chunk, no VP8X) cannot carry
 * any of this by specification — no metadata chunk is legal without the
 * extended header — so scrubbing one is always a byte-for-byte no-op.
 */
#ifndef SP_WEBPREWRITE_H
#define SP_WEBPREWRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "exif.h"
#include "io.h"
#include "policy.h"
#include "rewrite.h" /* sp_rewrite_stats — shared with the JPEG/PNG sides */
#include "scrubpony.h"

/* On-the-wire size of the synthetic EXIF chunk: FourCC (4) + length field
 * (4) + the TIFF blob, the same 26-byte payload PNG's synthetic eXIf chunk
 * carries. Always even, so it is never padded. Exposed so main.c's
 * dry-run/verbose messaging can report the real number instead of JPEG's
 * 36-byte figure or PNG's 38-byte one. */
#define SP_WEBP_EXIF_ORIENT_CHUNK_LEN \
    (4u + 4u + (SP_EXIF_ORIENT_SEG_LEN - 4u - SP_EXIF_ID_LEN))

/* Writes a scrubbed copy of in to out, applying pol. Does not commit — same
 * contract as sp_rewrite()/sp_png_rewrite(): the caller decides whether to
 * publish the result, and any error return means it must not.
 *
 * Reuses sp_rewrite_stats from rewrite.h, same as the PNG side: every field
 * but scan_off/scan_len means the same thing for a WebP as it does for a
 * JPEG, and those two are left zero here. */
sp_status sp_webp_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                          FILE *listing, sp_rewrite_stats *stats);

/* WebP's EXIF chunk carries the same TIFF-structured data JPEG's APP1 and
 * PNG's eXIf do, without the six-byte "Exif\0\0" identifier JPEG needs to
 * tell APP1-as-EXIF apart from APP1-as-XMP. These adapt a WebP EXIF chunk's
 * raw data to the shape exif.c already knows, the same way
 * sp_png_exif_scan/sp_png_exif_is_minimal_orientation do for PNG, so exif.c
 * itself stays untouched. */
sp_status sp_webp_exif_scan(const uint8_t *tiff_blob, size_t len,
                            sp_exif_info *out);
bool sp_webp_exif_is_minimal_orientation(const uint8_t *tiff_blob, size_t len);

#endif /* SP_WEBPREWRITE_H */
