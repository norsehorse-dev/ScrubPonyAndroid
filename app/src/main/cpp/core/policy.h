/* Keep or drop.
 *
 * The interesting fact about JPEG metadata is that the marker code is not
 * enough to decide. APP1 is EXIF or XMP; APP2 is an ICC colour profile or a
 * multi-picture block that can contain an entire second photograph. One of
 * each pair is kept and the other dropped, and the only way to tell them
 * apart is the identifier string at the front of the payload.
 *
 * Getting this wrong is how naive scrubbers either wreck people's colours or
 * quietly leave the GPS coordinates they promised to remove.
 */
#ifndef SP_POLICY_H
#define SP_POLICY_H

#include <stdbool.h>

#include "jpeg.h"
#include "scrubpony.h"

typedef enum {
    SP_KEEP = 0,
    SP_DROP
} sp_action;

typedef enum {
    SP_KIND_STRUCTURAL = 0, /* tables, frame headers, scan, SOI/EOI      */
    SP_KIND_JFIF,           /* APP0 JFIF density header                  */
    SP_KIND_JFXX,           /* APP0 extension: carries a thumbnail       */
    SP_KIND_EXIF,           /* APP1 Exif\0\0                             */
    SP_KIND_XMP,            /* APP1 Adobe XMP                            */
    SP_KIND_XMP_EXT,        /* APP1 extended XMP                         */
    SP_KIND_ICC,            /* APP2 ICC_PROFILE                          */
    SP_KIND_MPF,            /* APP2 multi-picture format                 */
    SP_KIND_FLASHPIX,       /* APP2 FPXR                                 */
    SP_KIND_DUCKY,          /* APP12 Ducky                               */
    SP_KIND_PHOTOSHOP,      /* APP13 Photoshop IRB, wrapping IPTC        */
    SP_KIND_ADOBE,          /* APP14 colour transform flag               */
    SP_KIND_COMMENT,        /* COM                                       */
    SP_KIND_UNKNOWN_APP     /* an APPn nobody at this table recognises   */
} sp_kind;

typedef struct {
    bool strict; /* also drop APP0, ICC and APP14 (see plan section 5) */

    /* Re-emit a minimal EXIF block carrying nothing but the orientation, so
     * that portrait photographs do not come out sideways. Independent of
     * strict on purpose: strict is about which segments are dropped, and this
     * is about whether one 36-byte fact is put back. Someone who wants
     * maximum anonymity and someone who wants correct colour are not
     * necessarily the same person. */
    bool no_orientation;
} sp_policy;

typedef struct {
    sp_kind     kind;
    sp_action   action;
    const char *label;  /* "EXIF", "ICC profile" — never NULL          */
    const char *reason; /* why, for -n output; "" when self-evident    */
} sp_decision;

/* Classifies a segment by marker and payload identifier. Pure: depends on
 * nothing but the segment's captured prefix. */
sp_kind sp_policy_classify(const sp_segment *seg);

/* Applies the keep/drop rules to a classification. */
sp_decision sp_policy_decide(const sp_policy *pol, const sp_segment *seg);

/* Display name for a kind. Never NULL. */
const char *sp_kind_label(sp_kind kind);

/* True when this kind is an EXIF block worth scanning for orientation. */
bool sp_kind_is_exif(sp_kind kind);

#endif /* SP_POLICY_H */
