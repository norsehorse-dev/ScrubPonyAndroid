#include "heifpolicy.h"

#include <string.h>

sp_heif_kind sp_heifpolicy_classify(const sp_heif_item *item)
{
    const char *t;

    if (item == NULL)
        return SP_HEIF_KIND_UNKNOWN;
    t = item->type;

    if (strcmp(t, "Exif") == 0)
        return SP_HEIF_KIND_EXIF;
    if (strcmp(t, "mime") == 0)
        return SP_HEIF_KIND_XMP;

    /* The known image and image-derivation item types. This list does not
     * need to be exhaustive: anything not on it is classified UNKNOWN and
     * kept anyway (see the header), so the only cost of an omission here is
     * that a genuine image item shows up in -n listings as "unrecognised"
     * rather than being suppressed as structural. Nothing is ever dropped
     * for being missing from this list. */
    if (strcmp(t, "hvc1") == 0 || strcmp(t, "hev1") == 0 || /* HEVC image     */
        strcmp(t, "av01") == 0 ||                           /* AV1 image      */
        strcmp(t, "avc1") == 0 ||                           /* AVC image      */
        strcmp(t, "jpeg") == 0 ||                           /* JPEG-in-HEIF   */
        strcmp(t, "grid") == 0 || strcmp(t, "iovl") == 0 || /* derived images */
        strcmp(t, "iden") == 0 || strcmp(t, "tmap") == 0)   /* identity, tone */
        return SP_HEIF_KIND_IMAGE;

    return SP_HEIF_KIND_UNKNOWN;
}

const char *sp_heifkind_label(sp_heif_kind kind)
{
    switch (kind) {
    case SP_HEIF_KIND_IMAGE:   return "image data";
    case SP_HEIF_KIND_EXIF:    return "EXIF";
    case SP_HEIF_KIND_XMP:     return "XMP";
    case SP_HEIF_KIND_UNKNOWN: return "unrecognised item";
    }
    return "?";
}

bool sp_heifkind_is_exif(sp_heif_kind kind)
{
    return kind == SP_HEIF_KIND_EXIF;
}

bool sp_heifkind_is_structural(sp_heif_kind kind)
{
    return kind == SP_HEIF_KIND_IMAGE;
}

sp_heif_decision sp_heifpolicy_decide(const sp_policy *pol,
                                      const sp_heif_item *item)
{
    sp_heif_decision d;

    (void)pol; /* strict has no HEIC-specific effect yet; see the header */

    d.kind = sp_heifpolicy_classify(item);
    d.label = sp_heifkind_label(d.kind);
    d.action = SP_KEEP;
    d.reason = "";

    switch (d.kind) {
    case SP_HEIF_KIND_IMAGE:
        /* The picture, or a derivation of it. Never touched. */
        d.action = SP_KEEP;
        break;

    case SP_HEIF_KIND_EXIF:
        d.action = SP_DROP;
        d.reason = "GPS, timestamps, device serial, thumbnail";
        break;

    case SP_HEIF_KIND_XMP:
        d.action = SP_DROP;
        d.reason = "editing history, creator, sometimes GPS";
        break;

    case SP_HEIF_KIND_UNKNOWN:
        /* Kept, not dropped — the reverse of the other formats' default. In
         * HEIC an item the tool cannot name is more likely a piece of the
         * image than a piece of metadata, and dropping image data does not
         * fail safe. Shown in listings (not suppressed as structural) so the
         * decision is visible rather than silent. */
        d.action = SP_KEEP;
        d.reason = "kept: unrecognised items may be image data";
        break;
    }

    return d;
}
