/* The HEIC rewriter — the counterpart to webprewrite.h, for ISOBMFF boxes.
 *
 * This is the most invasive rewriter in the project, and the reason is the
 * one spelled out in heif.h: HEIC does not store metadata in droppable,
 * position-independent chunks. The EXIF and XMP payloads live as items inside
 * the shared `mdat`, located by absolute file offsets recorded in `iloc`.
 * Removing them means three linked edits, none of which the other formats
 * ever need:
 *
 *   1. The item is removed from `iinf` (its `infe` entry), from `iloc` (its
 *      location entry), and from `iref` (any reference to or from it).
 *   2. Its bytes are excised from `mdat`, so every later byte in `mdat` moves.
 *   3. Shrinking `meta` by (1) moves `mdat` itself earlier in the file, so
 *      the *surviving* items' `iloc` offsets no longer point at their data.
 *      Every kept offset is recomputed and `iloc` is rewritten.
 *
 * The picture is never touched: the image item's bytes are copied verbatim,
 * only relocated, and its offset is rewritten to match. That is the same
 * "provably did not alter the pixels" guarantee the other three formats make,
 * kept across a much larger amount of structural surgery.
 *
 * Orientation needs no synthetic block here. HEIC records rotation in an
 * `irot` (and mirroring in `imir`) property box inside iprp/ipco, which this
 * rewriter copies through untouched, so orientation survives on its own.
 *
 * A HEIC with nothing to drop is copied byte for byte, the same no-op a
 * simple-format WebP gets — no structural surgery happens unless something is
 * actually being removed.
 *
 * Layouts this version does not rewrite (item data outside a single `mdat`,
 * `idat`/construction-method item data, external data references, or item
 * tables larger than the reader models) are refused with SP_ERR_UNSUPPORTED
 * and the file is left untouched. That is the safe answer: a partial or
 * mis-offset HEIC is far worse than an unscrubbed one.
 */
#ifndef SP_HEIFREWRITE_H
#define SP_HEIFREWRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "exif.h"
#include "io.h"
#include "policy.h"
#include "rewrite.h" /* sp_rewrite_stats — shared across all formats */
#include "scrubpony.h"

/* Writes a scrubbed copy of in to out, applying pol. Does not commit — same
 * contract as the other rewriters: any error return means the output is
 * incomplete and the caller must abort rather than publish it.
 *
 * SP_ERR_UNSUPPORTED means the file is a valid HEIC whose layout this version
 * will not rewrite (see the header comment). The caller should leave the
 * original untouched and report it, not treat it as a corrupt file.
 *
 * Reuses sp_rewrite_stats: kept/dropped count items rather than segments,
 * orientation_kept is always false (HEIC preserves orientation via irot,
 * not a synthetic block), and scan_off/scan_len are left zero. */
sp_status sp_heif_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                          FILE *listing, sp_rewrite_stats *stats);

/* A HEIC `Exif` item's payload is not a bare TIFF blob: it opens with a
 * 4-byte big-endian exif_tiff_header_offset, then that many bytes (usually
 * the "Exif\0\0" identifier), then the TIFF data. These adapt it to the shape
 * exif.c expects, the same role sp_png_exif_scan / sp_webp_exif_scan play for
 * those formats, so exif.c itself stays untouched. */
sp_status sp_heif_exif_scan(const uint8_t *item_data, size_t len,
                            sp_exif_info *out);
bool sp_heif_exif_is_minimal_orientation(const uint8_t *item_data, size_t len);

#endif /* SP_HEIFREWRITE_H */
