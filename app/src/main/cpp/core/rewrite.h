/* The rewriter.
 *
 * Reads with jpeg.c, decides with policy.c, writes with io.c. It lives in its
 * own translation unit rather than inside jpeg.c because the parser has no
 * business knowing what a policy is, and policy.h already includes jpeg.h.
 *
 * The one promise this file has to keep: everything from the SOS marker to
 * the end of the file is copied byte for byte. That is the entropy-coded
 * image data, and not touching it is what makes "lossless" a claim rather
 * than a hope.
 */
#ifndef SP_REWRITE_H
#define SP_REWRITE_H

#include <stdbool.h>
#include <stdint.h>

#include "exif.h"
#include "io.h"
#include "jpeg.h"
#include "policy.h"
#include "scrubpony.h"

typedef struct {
    unsigned long kept;
    unsigned long dropped;
    uint64_t      in_size;
    uint64_t      out_size;
    uint64_t      scan_off; /* in the input */
    uint64_t      scan_len;
    sp_exif_info  exif;
    bool          have_exif;
    bool          orientation_kept; /* the synthetic block was emitted */
} sp_rewrite_stats;

/* Writes a scrubbed copy of in to out, applying pol.
 *
 * Does not commit: the caller does that, so that a caller which decides the
 * result is unacceptable can abort and leave nothing behind. Any error return
 * means the output is incomplete and must be aborted rather than committed.
 *
 * SP_ERR_OUTPUT_GREW means the result is larger than the input, which cannot
 * happen when the only operation is removal. It is a bug detector, not a
 * user-facing condition, and it fires before anything is published. */
/* listing, when non-NULL, receives one keep/drop line per segment as the
 * rewrite happens. Passing it here rather than making the caller walk the
 * file a second time keeps the whole operation single-pass. */
sp_status sp_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                     FILE *listing, sp_rewrite_stats *stats);

#endif /* SP_REWRITE_H */
