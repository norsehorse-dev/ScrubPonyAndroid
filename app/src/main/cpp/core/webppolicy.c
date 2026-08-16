#include "webppolicy.h"

#include <string.h>

sp_webp_kind sp_webppolicy_classify(const sp_webp_chunk *chunk)
{
    const char *t;

    if (chunk == NULL)
        return SP_WEBP_KIND_UNKNOWN;
    t = chunk->fourcc;

    if (strcmp(t, "VP8 ") == 0 || strcmp(t, "VP8L") == 0)
        return SP_WEBP_KIND_IMAGE;
    if (strcmp(t, "VP8X") == 0)
        return SP_WEBP_KIND_HEADER;
    if (strcmp(t, "ALPH") == 0)
        return SP_WEBP_KIND_ALPHA;
    if (strcmp(t, "ANIM") == 0 || strcmp(t, "ANMF") == 0)
        return SP_WEBP_KIND_ANIM;
    if (strcmp(t, "ICCP") == 0)
        return SP_WEBP_KIND_ICC;
    if (strcmp(t, "EXIF") == 0)
        return SP_WEBP_KIND_EXIF;
    if (strcmp(t, "XMP ") == 0)
        return SP_WEBP_KIND_XMP;

    return SP_WEBP_KIND_UNKNOWN;
}

const char *sp_webpkind_label(sp_webp_kind kind)
{
    switch (kind) {
    case SP_WEBP_KIND_IMAGE:   return "image data";
    case SP_WEBP_KIND_HEADER:  return "extended header";
    case SP_WEBP_KIND_ALPHA:   return "alpha channel";
    case SP_WEBP_KIND_ANIM:    return "animation";
    case SP_WEBP_KIND_ICC:     return "ICC profile";
    case SP_WEBP_KIND_EXIF:    return "EXIF";
    case SP_WEBP_KIND_XMP:     return "XMP";
    case SP_WEBP_KIND_UNKNOWN: return "unrecognised";
    }
    return "?";
}

bool sp_webpkind_is_exif(sp_webp_kind kind)
{
    return kind == SP_WEBP_KIND_EXIF;
}

bool sp_webpkind_is_structural(sp_webp_kind kind)
{
    return kind == SP_WEBP_KIND_IMAGE || kind == SP_WEBP_KIND_HEADER ||
           kind == SP_WEBP_KIND_ALPHA || kind == SP_WEBP_KIND_ANIM;
}

sp_webp_decision sp_webppolicy_decide(const sp_policy *pol,
                                      const sp_webp_chunk *chunk)
{
    sp_webp_decision d;
    bool strict = (pol != NULL) && pol->strict;

    d.kind = sp_webppolicy_classify(chunk);
    d.label = sp_webpkind_label(d.kind);
    d.action = SP_DROP;
    d.reason = "";

    switch (d.kind) {
    case SP_WEBP_KIND_IMAGE:
    case SP_WEBP_KIND_HEADER:
    case SP_WEBP_KIND_ALPHA:
    case SP_WEBP_KIND_ANIM:
        /* Pixels, the container's own extended header, and the alpha and
         * animation data that go with them: never identifying, and each is
         * load-bearing for how the image renders. Kept regardless of
         * strict, same as PNG's rendering-correctness chunks. */
        d.action = SP_KEEP;
        break;

    case SP_WEBP_KIND_ICC:
        /* Same call as JPEG's ICC_PROFILE and PNG's iCCP: dropping this
         * visibly shifts colour, so it survives by default. */
        d.action = strict ? SP_DROP : SP_KEEP;
        d.reason = strict ? "uncommon profiles narrow the source" : "";
        break;

    case SP_WEBP_KIND_EXIF:
        d.reason = "GPS, timestamps, device serial, thumbnail";
        break;

    case SP_WEBP_KIND_XMP:
        d.reason = "editing history, creator, sometimes GPS";
        break;

    case SP_WEBP_KIND_UNKNOWN:
        /* Default deny, same reasoning as JPEG's unknown APPn and PNG's
         * unknown ancillary chunk: an unrecognised chunk is exactly where a
         * tool parks something identifying, and the WebP spec guarantees
         * every decoder can safely skip a chunk type it does not
         * recognise. */
        d.reason = "unrecognised chunk";
        break;
    }

    return d;
}
