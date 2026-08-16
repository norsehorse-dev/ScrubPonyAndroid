/* Human-readable output for -v and -n.
 *
 * Kept apart from the parser so that the parser has no opinion about
 * presentation and no dependency on stdio beyond what io.h already needs.
 */
#ifndef SP_REPORT_H
#define SP_REPORT_H

#include <stdio.h>

#include "exif.h"
#include "heif.h"
#include "heifpolicy.h"
#include "io.h"
#include "jpeg.h"
#include "png.h"
#include "policy.h"
#include "pngpolicy.h"
#include "scrubpony.h"
#include "webp.h"
#include "webppolicy.h"

/* Canonical short name for a marker code: "SOI", "APP1", "SOF2", "DQT",
 * "RST3", "COM". Never NULL; unknown codes come back as "?". */
const char *sp_marker_name(uint8_t code);

/* Renders the leading printable run of a segment's captured payload prefix
 * into buf as a quoted string, or an empty string if there is nothing
 * printable to show. Always NUL-terminates. */
void sp_prefix_label(const sp_segment *seg, char *buf, size_t buflen);

void sp_report_file_header(FILE *out, const sp_file *f);
void sp_report_segment(FILE *out, const sp_segment *seg);
void sp_report_scan(FILE *out, const sp_parser *p);

/* Dry-run output: one line per segment saying what would happen to it. */
void sp_report_decision_header(FILE *out);
void sp_report_decision(FILE *out, const sp_segment *seg,
                        const sp_decision *d);

/* PNG counterparts. Deliberately separate functions rather than a shared one
 * templated on some union of sp_segment and sp_png_chunk: the two shapes
 * only coincidentally have similar fields (a marker/type and a length), and
 * forcing them through one signature would be the same mistake the project
 * already avoids elsewhere — see policy.h on the marker code never being
 * enough to decide anything by itself. sp_report_decision_header()'s column
 * layout is generic enough to head either table, so it is shared as-is. */
void sp_report_png_chunk(FILE *out, const sp_png_chunk *chunk);
void sp_report_png_decision(FILE *out, const sp_png_chunk *chunk,
                            const sp_png_decision *d);

/* WebP counterparts, same reasoning as the PNG ones above. */
void sp_report_webp_chunk(FILE *out, const sp_webp_chunk *chunk);
void sp_report_webp_decision(FILE *out, const sp_webp_chunk *chunk,
                             const sp_webp_decision *d);

/* HEIC counterparts. A HEIC's unit is the item, not a chunk, so these take an
 * sp_heif_item; the "length" column shows the item's total data length summed
 * across its extents. */
void sp_report_heif_item(FILE *out, const sp_heif_item *item);
void sp_report_heif_decision(FILE *out, const sp_heif_item *item,
                             const sp_heif_decision *d);

/* What the EXIF block was carrying, for the dry run. Format-agnostic: PNG's
 * eXIf chunk, WebP's EXIF chunk and JPEG's APP1 EXIF all scan into the same
 * sp_exif_info. */
void sp_report_exif(FILE *out, const sp_exif_info *info);

/* The orientation warning from section 5 of the plan. Goes to stderr with a
 * path prefix, because it is a caveat about the output rather than a listing
 * of the input, and people pipe the listing.
 *
 * Silent when kept is true: the synthetic block put the rotation back, so
 * there is nothing to warn about. Warning anyway across a thousand-file run
 * would train people to ignore the one case that matters. */
void sp_warn_orientation(FILE *out, const char *path, uint16_t orientation,
                         bool kept);

#endif /* SP_REPORT_H */
