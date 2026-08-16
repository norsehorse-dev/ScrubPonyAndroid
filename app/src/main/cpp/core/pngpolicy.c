#include "pngpolicy.h"

#include <string.h>

static bool keyword_is(const uint8_t *data, size_t data_len, const char *kw)
{
    size_t kwlen = strlen(kw);

    if (data == NULL || data_len < kwlen + 1u)
        return false;
    if (memcmp(data, kw, kwlen) != 0)
        return false;
    /* The keyword field is itself NUL-terminated inside the chunk; without
     * checking that byte, "XML:com.adobe.xmp-extra" would match as a prefix
     * of the real thing. */
    return data[kwlen] == 0u;
}

sp_png_kind sp_pngpolicy_classify(const sp_png_chunk *chunk,
                                  const uint8_t *data, size_t data_len)
{
    const char *t;

    if (chunk == NULL)
        return SP_PNG_KIND_UNKNOWN_ANCILLARY;
    t = chunk->type;

    if (strcmp(t, "IHDR") == 0 || strcmp(t, "PLTE") == 0 ||
        strcmp(t, "IDAT") == 0 || strcmp(t, "IEND") == 0)
        return SP_PNG_KIND_CRITICAL;

    if (strcmp(t, "pHYs") == 0 || strcmp(t, "gAMA") == 0 ||
        strcmp(t, "cHRM") == 0 || strcmp(t, "sRGB") == 0 ||
        strcmp(t, "tRNS") == 0 || strcmp(t, "sBIT") == 0 ||
        strcmp(t, "bKGD") == 0 || strcmp(t, "hIST") == 0 ||
        strcmp(t, "sPLT") == 0 || strcmp(t, "acTL") == 0 ||
        strcmp(t, "fcTL") == 0 || strcmp(t, "fdAT") == 0)
        return SP_PNG_KIND_RENDER;

    if (strcmp(t, "iCCP") == 0)
        return SP_PNG_KIND_ICC;

    if (strcmp(t, "tEXt") == 0 || strcmp(t, "zTXt") == 0)
        return SP_PNG_KIND_TEXT;

    if (strcmp(t, "iTXt") == 0) {
        /* Same shared identity as JPEG's two XMP URIs living inside APP1:
         * one ancillary-text chunk type carries either ordinary translated
         * text or Adobe XMP, and only the payload says which. */
        if (keyword_is(data, data_len, "XML:com.adobe.xmp"))
            return SP_PNG_KIND_XMP;
        return SP_PNG_KIND_ITXT;
    }

    if (strcmp(t, "eXIf") == 0)
        return SP_PNG_KIND_EXIF;

    if (strcmp(t, "tIME") == 0)
        return SP_PNG_KIND_TIME;

    /* Not one of the above. The type's own first letter says whether an
     * unrecognised chunk is safe to drop (ancillary, lowercase) or not
     * (critical, uppercase) — see pngpolicy.h. */
    return (t[0] >= 'A' && t[0] <= 'Z') ? SP_PNG_KIND_UNKNOWN_CRITICAL
                                        : SP_PNG_KIND_UNKNOWN_ANCILLARY;
}

const char *sp_pngkind_label(sp_png_kind kind)
{
    switch (kind) {
    case SP_PNG_KIND_CRITICAL:          return "critical";
    case SP_PNG_KIND_RENDER:            return "rendering";
    case SP_PNG_KIND_ICC:               return "ICC profile";
    case SP_PNG_KIND_TEXT:              return "text";
    case SP_PNG_KIND_ITXT:              return "international text";
    case SP_PNG_KIND_XMP:               return "XMP";
    case SP_PNG_KIND_EXIF:              return "EXIF";
    case SP_PNG_KIND_TIME:              return "timestamp";
    case SP_PNG_KIND_UNKNOWN_CRITICAL:  return "unrecognised critical";
    case SP_PNG_KIND_UNKNOWN_ANCILLARY: return "unrecognised";
    }
    return "?";
}

bool sp_pngkind_is_exif(sp_png_kind kind)
{
    return kind == SP_PNG_KIND_EXIF;
}

sp_png_decision sp_pngpolicy_decide(const sp_policy *pol,
                                    const sp_png_chunk *chunk,
                                    const uint8_t *data, size_t data_len)
{
    sp_png_decision d;
    bool strict = (pol != NULL) && pol->strict;

    d.kind = sp_pngpolicy_classify(chunk, data, data_len);
    d.label = sp_pngkind_label(d.kind);
    d.action = SP_DROP;
    d.reason = "";

    switch (d.kind) {
    case SP_PNG_KIND_CRITICAL:
        d.action = SP_KEEP;
        break;

    case SP_PNG_KIND_RENDER:
        /* Rendering-correctness chunks: not identifying, and several are
         * load-bearing (dropping tRNS changes how transparency displays;
         * dropping the APNG control chunks breaks the animation). Kept
         * regardless of strict — unlike JFIF on the JPEG side, none of
         * these carry even weak fingerprinting signal. */
        d.action = SP_KEEP;
        break;

    case SP_PNG_KIND_ICC:
        /* Same call as JPEG's ICC_PROFILE: dropping this visibly shifts
         * colour, so it survives by default. */
        d.action = strict ? SP_DROP : SP_KEEP;
        d.reason = strict ? "uncommon profiles narrow the source" : "";
        break;

    case SP_PNG_KIND_TEXT:
        d.reason = "author, comments, tool info, sometimes more";
        break;

    case SP_PNG_KIND_ITXT:
        d.reason = "same risk as tEXt/zTXt, international text";
        break;

    case SP_PNG_KIND_XMP:
        d.reason = "editing history, creator, sometimes GPS";
        break;

    case SP_PNG_KIND_EXIF:
        d.reason = "GPS, timestamps, device serial, thumbnail";
        break;

    case SP_PNG_KIND_TIME:
        d.reason = "last-modification timestamp";
        break;

    case SP_PNG_KIND_UNKNOWN_CRITICAL:
        /* The PNG spec requires a decoder that does not understand a
         * critical chunk to refuse the whole file. We are not deciding
         * whether to render it; dropping something the spec calls
         * essential is a worse bet than keeping it. */
        d.action = SP_KEEP;
        d.reason = "unrecognised critical chunk, kept rather than guessed at";
        break;

    case SP_PNG_KIND_UNKNOWN_ANCILLARY:
        /* Default deny, same reasoning as JPEG's unknown APPn: an
         * unrecognised ancillary chunk is exactly where a tool parks
         * something identifying, and the PNG spec guarantees every decoder
         * can safely skip an ancillary chunk it does not recognise. */
        d.reason = "unrecognised ancillary chunk";
        break;
    }

    return d;
}
