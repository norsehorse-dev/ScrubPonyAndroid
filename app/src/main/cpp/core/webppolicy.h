/* Keep or drop, for WebP.
 *
 * Unlike PNG's chunk types, a WebP FourCC carries no self-describing
 * "safe to skip" signal — RIFF has no equivalent of PNG's lowercase-first-
 * letter convention. Classification here is therefore a lookup against the
 * fixed table of FourCCs the WebP container spec actually defines, the same
 * shape as JPEG's APPn table in policy.c, and for the same reason an
 * unrecognised tag defaults to dropped rather than kept: a privacy tool
 * that fails open on the one case it cannot identify is not a privacy tool.
 * It is also spec-safe here in a way it is not for PNG's uppercase-first-
 * letter chunks: every WebP chunk beyond VP8/VP8L/VP8X is documented as
 * optional, so a decoder that does not recognise one is expected to skip
 * it, not refuse the file.
 */
#ifndef SP_WEBPPOLICY_H
#define SP_WEBPPOLICY_H

#include <stdbool.h>

#include "policy.h"
#include "scrubpony.h"
#include "webp.h"

typedef enum {
    SP_WEBP_KIND_IMAGE = 0, /* VP8, VP8L: the actual picture bits         */
    SP_WEBP_KIND_HEADER,    /* VP8X: extended-format container header    */
    SP_WEBP_KIND_ALPHA,     /* ALPH: separate alpha plane for lossy VP8   */
    SP_WEBP_KIND_ANIM,      /* ANIM, ANMF: animation control and frames  */
    SP_WEBP_KIND_ICC,       /* ICCP: embedded colour profile              */
    SP_WEBP_KIND_EXIF,      /* EXIF: raw EXIF, same TIFF blob as JPEG's   */
    SP_WEBP_KIND_XMP,       /* XMP : XMP metadata                         */
    SP_WEBP_KIND_UNKNOWN    /* a FourCC nobody at this table recognises   */
} sp_webp_kind;

typedef struct {
    sp_webp_kind kind;
    sp_action    action;
    const char  *label;  /* "EXIF", "ICC profile" — never NULL          */
    const char  *reason; /* why, for -n output; "" when self-evident    */
} sp_webp_decision;

/* Classifies a chunk by its FourCC alone — unlike PNG's iTXt, no WebP chunk
 * needs its payload inspected to tell two meanings apart. */
sp_webp_kind sp_webppolicy_classify(const sp_webp_chunk *chunk);

/* Applies the keep/drop rules to a classification. Reuses sp_policy from
 * policy.h — "strict" means the same thing it means for JPEG and PNG: also
 * drop the colour-profile chunk. */
sp_webp_decision sp_webppolicy_decide(const sp_policy *pol,
                                      const sp_webp_chunk *chunk);

/* Display name for a kind. Never NULL. */
const char *sp_webpkind_label(sp_webp_kind kind);

/* True when this kind is the EXIF chunk, worth scanning for orientation. */
bool sp_webpkind_is_exif(sp_webp_kind kind);

/* True for the four kinds that are always kept and never identifying: the
 * image data itself, the extended header, the alpha plane and animation
 * chunks. Used to keep verbose/dry-run listings focused on what might
 * actually change, the same way PNG's listings hide IHDR/PLTE/IDAT/IEND. */
bool sp_webpkind_is_structural(sp_webp_kind kind);

#endif /* SP_WEBPPOLICY_H */
