/* Keep or drop, for HEIC items.
 *
 * The default here runs the opposite way to JPEG's and WebP's, and the
 * reason is structural, not a change of heart. In those formats an
 * unrecognised segment or chunk is metadata by elimination — the image data
 * is one specific, known marker, and everything the tool cannot identify is
 * some optional extra that is safe, and privacy-preserving, to drop. In HEIC
 * that reasoning inverts: the *image itself* is stored as one or more items,
 * and new item types (tiles, grids, overlays, alternates, future codecs) are
 * added to the format over time. An item this table does not recognise is far
 * more likely to be part of the picture than to be metadata, and dropping a
 * piece of the picture does not fail closed — it produces a broken image.
 *
 * So HEIC drops only the item types that are unambiguously metadata carriers:
 * `Exif` (a raw TIFF/EXIF blob, the same one JPEG's APP1 and PNG's eXIf hold)
 * and `mime` (a MIME-typed payload, in practice XMP as application/rdf+xml,
 * and never image data). Everything else — every image and image-derivation
 * type, and every type this table has never heard of — is kept.
 *
 * Orientation needs no special handling the way it does for the other three
 * formats. HEIC records rotation and mirroring in native `irot`/`imir`
 * property boxes inside iprp/ipco, which are rendering properties this tool
 * keeps untouched, so orientation survives on its own without a synthetic
 * EXIF block being written back. The colour profile likewise lives in a
 * `colr` property box, kept as-is; HEIC has no metadata *item* in the
 * "kept by default, dropped under --strict" tier the way the chunk formats'
 * ICC profiles are, so --strict currently changes nothing about a HEIC.
 */
#ifndef SP_HEIFPOLICY_H
#define SP_HEIFPOLICY_H

#include <stdbool.h>

#include "heif.h"
#include "policy.h"
#include "scrubpony.h"

typedef enum {
    SP_HEIF_KIND_IMAGE = 0, /* hvc1/hev1/av01/grid/iovl/... the picture     */
    SP_HEIF_KIND_EXIF,      /* Exif: raw EXIF, same TIFF blob as JPEG's     */
    SP_HEIF_KIND_XMP,       /* mime: MIME payload, in practice XMP          */
    SP_HEIF_KIND_UNKNOWN    /* an item type this table does not recognise   */
} sp_heif_kind;

typedef struct {
    sp_heif_kind kind;
    sp_action    action;
    const char  *label;  /* "EXIF", "image data" — never NULL             */
    const char  *reason; /* why, for -n output; "" when self-evident      */
} sp_heif_decision;

/* Classifies an item by its four-character type. */
sp_heif_kind sp_heifpolicy_classify(const sp_heif_item *item);

/* Applies the keep/drop rules. Reuses sp_policy from policy.h for signature
 * consistency with the other formats; `strict` has no effect on a HEIC (see
 * the header comment), but the parameter is kept so callers stay uniform. */
sp_heif_decision sp_heifpolicy_decide(const sp_policy *pol,
                                      const sp_heif_item *item);

/* Display name for a kind. Never NULL. */
const char *sp_heifkind_label(sp_heif_kind kind);

/* True when this kind is the EXIF item, worth scanning for orientation in
 * -n output (even though HEIC preserves orientation via irot, the EXIF block
 * is still worth reporting on, same as the other formats). */
bool sp_heifkind_is_exif(sp_heif_kind kind);

/* True for the image itself: kept, not identifying, suppressed from verbose
 * and dry-run listings the way IHDR/VP8/SOI are for the other formats. An
 * UNKNOWN item is deliberately NOT structural: it is kept, but shown, so the
 * user sees that something the tool could not classify was left in place. */
bool sp_heifkind_is_structural(sp_heif_kind kind);

#endif /* SP_HEIFPOLICY_H */
