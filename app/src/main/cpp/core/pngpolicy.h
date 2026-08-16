/* Keep or drop, for PNG.
 *
 * PNG's chunk type already says what a chunk is — unlike JPEG, where APP1 is
 * EXIF or XMP and APP2 is an ICC profile or an MPF block, and the marker
 * alone cannot tell them apart. Classification here is a lookup on the
 * 4-letter type, nothing more.
 *
 * The chunk naming convention itself carries real information this policy
 * leans on: a lowercase first letter means "ancillary" — the PNG spec
 * requires every decoder to be able to skip a chunk type it does not
 * recognise, which is exactly what makes "drop unknown ancillary chunks by
 * default" spec-safe rather than merely convenient. An uppercase first
 * letter means "critical" — a decoder that does not recognise it is
 * supposed to refuse the whole file, so an unrecognised critical chunk is
 * kept unconditionally here rather than guessed at.
 */
#ifndef SP_PNGPOLICY_H
#define SP_PNGPOLICY_H

#include <stdbool.h>

#include "policy.h"
#include "png.h"
#include "scrubpony.h"

typedef enum {
    SP_PNG_KIND_CRITICAL = 0, /* IHDR, PLTE, IDAT, IEND                    */
    SP_PNG_KIND_RENDER,       /* pHYs, gAMA, cHRM, sRGB, tRNS, sBIT, bKGD, */
                              /* hIST, sPLT, acTL/fcTL/fdAT (APNG)         */
    SP_PNG_KIND_ICC,          /* iCCP: embedded colour profile             */
    SP_PNG_KIND_TEXT,         /* tEXt, zTXt: keyword/value text            */
    SP_PNG_KIND_ITXT,         /* iTXt: international text, not XMP         */
    SP_PNG_KIND_XMP,          /* iTXt keyword "XML:com.adobe.xmp"          */
    SP_PNG_KIND_EXIF,         /* eXIf: raw EXIF, same TIFF blob as JPEG's  */
    SP_PNG_KIND_TIME,         /* tIME: last-modification timestamp         */
    SP_PNG_KIND_UNKNOWN_CRITICAL, /* uppercase first letter, unrecognised  */
    SP_PNG_KIND_UNKNOWN_ANCILLARY /* lowercase first letter, unrecognised  */
} sp_png_kind;

typedef struct {
    sp_png_kind kind;
    sp_action   action;
    const char *label;  /* "EXIF", "ICC profile" — never NULL          */
    const char *reason; /* why, for -n output; "" when self-evident    */
} sp_png_decision;

/* Classifies a chunk by its type. For iTXt this needs the keyword, which is
 * the leading NUL-terminated field of the chunk's data — pass the data (or
 * as much of its start as was read; SP_PNGPOLICY_KEYWORD_MAX bytes is always
 * enough to find the terminator or prove the chunk is malformed) so the
 * XMP-vs-plain-iTXt distinction can be made without a second read. data may
 * be NULL/empty for any other type, where it is unused. */
#define SP_PNGPOLICY_KEYWORD_MAX 32u
sp_png_kind sp_pngpolicy_classify(const sp_png_chunk *chunk,
                                  const uint8_t *data, size_t data_len);

/* Applies the keep/drop rules to a classification. Reuses sp_policy from
 * policy.h — "strict" means the same thing it means for JPEG: also drop the
 * colour-profile chunk. */
sp_png_decision sp_pngpolicy_decide(const sp_policy *pol,
                                    const sp_png_chunk *chunk,
                                    const uint8_t *data, size_t data_len);

/* Display name for a kind. Never NULL. */
const char *sp_pngkind_label(sp_png_kind kind);

/* True when this kind is the eXIf chunk, worth scanning for orientation. */
bool sp_pngkind_is_exif(sp_png_kind kind);

#endif /* SP_PNGPOLICY_H */
