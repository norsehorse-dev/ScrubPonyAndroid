#include "policy.h"

#include <string.h>

/* Payload identifiers, with their exact lengths including trailing NULs
 * where the specification includes them. Written out rather than derived
 * with strlen so that the embedded NUL in the EXIF identifier is not a
 * special case. */
static const char ID_JFIF[]    = "JFIF\0";
static const char ID_JFXX[]    = "JFXX\0";
static const char ID_EXIF[]    = "Exif\0\0";
static const char ID_XMP[]     = "http://ns.adobe.com/xap/1.0/\0";
static const char ID_XMP_EXT[] = "http://ns.adobe.com/xmp/extension/\0";
static const char ID_ICC[]     = "ICC_PROFILE\0";
static const char ID_MPF[]     = "MPF\0";
static const char ID_FPXR[]    = "FPXR\0";
static const char ID_DUCKY[]   = "Ducky";
static const char ID_PHOTOSHOP[] = "Photoshop 3.0\0";
static const char ID_ADOBE[]   = "Adobe";

static bool prefix_is(const sp_segment *seg, const char *id, size_t idlen)
{
    if (seg->prefix_len < idlen)
        return false;
    return memcmp(seg->prefix, id, idlen) == 0;
}

/* sizeof includes the compiler's own terminating NUL, which is never part of
 * the identifier, so every comparison length is sizeof - 1. Identifiers that
 * the format defines as NUL-terminated carry that NUL explicitly in the
 * literal above; "Ducky" and "Adobe", which do not, simply have none. */
#define IS(seg, id) prefix_is((seg), (id), sizeof(id) - 1u)

sp_kind sp_policy_classify(const sp_segment *seg)
{
    if (seg == NULL)
        return SP_KIND_STRUCTURAL;

    if (seg->marker == SP_MARK_COM)
        return SP_KIND_COMMENT;

    if (!sp_marker_is_app(seg->marker))
        return SP_KIND_STRUCTURAL;

    switch (seg->marker) {
    case 0xE0u: /* APP0 */
        if (IS(seg, ID_JFIF))
            return SP_KIND_JFIF;
        if (IS(seg, ID_JFXX))
            return SP_KIND_JFXX;
        break;

    case 0xE1u: /* APP1 */
        /* Order matters only in that the two XMP URIs share a long prefix;
         * they diverge at "xap" versus "xmp", well inside both. */
        if (IS(seg, ID_EXIF))
            return SP_KIND_EXIF;
        if (IS(seg, ID_XMP))
            return SP_KIND_XMP;
        if (IS(seg, ID_XMP_EXT))
            return SP_KIND_XMP_EXT;
        break;

    case 0xE2u: /* APP2 */
        if (IS(seg, ID_ICC))
            return SP_KIND_ICC;
        if (IS(seg, ID_MPF))
            return SP_KIND_MPF;
        if (IS(seg, ID_FPXR))
            return SP_KIND_FLASHPIX;
        break;

    case 0xECu: /* APP12 */
        if (IS(seg, ID_DUCKY))
            return SP_KIND_DUCKY;
        break;

    case 0xEDu: /* APP13 */
        if (IS(seg, ID_PHOTOSHOP))
            return SP_KIND_PHOTOSHOP;
        break;

    case 0xEEu: /* APP14 */
        if (IS(seg, ID_ADOBE))
            return SP_KIND_ADOBE;
        break;

    default:
        break;
    }

    return SP_KIND_UNKNOWN_APP;
}

const char *sp_kind_label(sp_kind kind)
{
    switch (kind) {
    case SP_KIND_STRUCTURAL:  return "structural";
    case SP_KIND_JFIF:        return "JFIF";
    case SP_KIND_JFXX:        return "JFXX";
    case SP_KIND_EXIF:        return "EXIF";
    case SP_KIND_XMP:         return "XMP";
    case SP_KIND_XMP_EXT:     return "XMP (extended)";
    case SP_KIND_ICC:         return "ICC profile";
    case SP_KIND_MPF:         return "MPF";
    case SP_KIND_FLASHPIX:    return "FlashPix";
    case SP_KIND_DUCKY:       return "Ducky";
    case SP_KIND_PHOTOSHOP:   return "Photoshop/IPTC";
    case SP_KIND_ADOBE:       return "Adobe";
    case SP_KIND_COMMENT:     return "comment";
    case SP_KIND_UNKNOWN_APP: return "unrecognised";
    }
    return "?";
}

bool sp_kind_is_exif(sp_kind kind)
{
    return kind == SP_KIND_EXIF;
}

sp_decision sp_policy_decide(const sp_policy *pol, const sp_segment *seg)
{
    sp_decision d;
    bool strict = (pol != NULL) && pol->strict;

    d.kind = sp_policy_classify(seg);
    d.label = sp_kind_label(d.kind);
    d.action = SP_DROP;
    d.reason = "";

    switch (d.kind) {
    case SP_KIND_STRUCTURAL:
        d.action = SP_KEEP;
        break;

    case SP_KIND_JFIF:
        /* Density fields are weakly fingerprintable but harmless, and some
         * decoders expect an APP0. Kept unless the user asked for strict. */
        d.action = strict ? SP_DROP : SP_KEEP;
        d.reason = strict ? "density is weakly fingerprintable" : "";
        break;

    case SP_KIND_JFXX:
        /* Not the same as JFIF despite the shared marker: the extension
         * block exists to carry a thumbnail. */
        d.reason = "embedded thumbnail";
        break;

    case SP_KIND_EXIF:
        d.reason = "GPS, timestamps, device serial, thumbnail";
        break;

    case SP_KIND_XMP:
    case SP_KIND_XMP_EXT:
        d.reason = "editing history, creator, sometimes GPS again";
        break;

    case SP_KIND_ICC:
        /* Dropping this visibly shifts colour, so it survives by default. */
        d.action = strict ? SP_DROP : SP_KEEP;
        d.reason = strict ? "uncommon profiles narrow the source" : "";
        break;

    case SP_KIND_MPF:
        d.reason = "can embed a second, unscrubbed image";
        break;

    case SP_KIND_FLASHPIX:
        d.reason = "camera-specific, can embed a thumbnail";
        break;

    case SP_KIND_DUCKY:
        d.reason = "camera and editor junk";
        break;

    case SP_KIND_PHOTOSHOP:
        d.reason = "captions, credits, location names";
        break;

    case SP_KIND_ADOBE:
        /* Dropping this can break CMYK and YCCK colour interpretation. */
        d.action = strict ? SP_DROP : SP_KEEP;
        d.reason = strict ? "colour transform flag" : "";
        break;

    case SP_KIND_COMMENT:
        d.reason = "free text";
        break;

    case SP_KIND_UNKNOWN_APP:
        /* Default deny. An unrecognised vendor segment is exactly where a
         * manufacturer hides a serial number, and a privacy tool that fails
         * open is not a privacy tool. If this breaks a decoder somewhere,
         * that is a bug report worth receiving and a new row in this table. */
        d.reason = "unrecognised application segment";
        break;
    }

    return d;
}
