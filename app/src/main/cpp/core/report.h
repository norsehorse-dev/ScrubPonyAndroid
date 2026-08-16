/* Human-readable output for -v and -n.
 *
 * Kept apart from the parser so that the parser has no opinion about
 * presentation and no dependency on stdio beyond what io.h already needs.
 */
#ifndef SP_REPORT_H
#define SP_REPORT_H

#include <stdio.h>

#include "exif.h"
#include "io.h"
#include "jpeg.h"
#include "policy.h"
#include "scrubpony.h"

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

/* What the EXIF block was carrying, for the dry run. */
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
