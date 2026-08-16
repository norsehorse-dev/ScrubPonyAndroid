/* HEIC / ISOBMFF box layer.
 *
 * HEIC is not a chunk format the way JPEG, PNG and WebP are, and this header
 * is shaped differently as a result. Where those three store metadata in
 * self-contained, position-independent chunks that a scrubber can drop with a
 * byte-range copy, HEIC is an ISO Base Media File (the MP4/MOV box format):
 * a tree of length-prefixed boxes in which the metadata lives as *items*
 * whose bytes sit in a shared `mdat` payload, pointed at by absolute file
 * offsets recorded in an `iloc` box inside `meta`. Nothing about that can be
 * dropped by copying a byte range and leaving everything else alone: removing
 * an item's bytes from `mdat` moves every later byte, and shrinking `meta`
 * moves `mdat` itself, so the offsets of the items that survive have to be
 * recomputed. heifrewrite.h is where that happens; this header only reads.
 *
 * Two layers are exposed:
 *
 *   1. A top-level box iterator (sp_heif_box_next), the same read-forward,
 *      length-is-hostile shape as the chunk iterators in webp.h and png.h.
 *      It walks ftyp / meta / mdat / free / ... at the outermost level.
 *
 *   2. A meta-box reader (sp_heif_parse_meta) that loads the item tables the
 *      rewriter needs all at once: the ordered list of meta's child boxes,
 *      the item-info table (item id -> four-character item type), the item-
 *      location table (item id -> base offset + extents), and the item
 *      references. A `meta` box is small and bounded, so loading it whole is
 *      not the memory hazard that loading a whole file would be, and the
 *      rewriter genuinely needs the entire table before it can rewrite a
 *      single offset.
 *
 * Box shape (ISO/IEC 14496-12): a 4-byte big-endian size, a 4-byte type,
 * then the body. size == 1 means a 64-bit largesize follows the type; size
 * == 0 means the box runs to end of file. A "FullBox" additionally carries a
 * 1-byte version and 3-byte flags at the front of its body; `meta`, `iinf`,
 * `infe`, `iloc` and `iref` are all FullBoxes.
 */
#ifndef SP_HEIF_H
#define SP_HEIF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "scrubpony.h"

/* Smallest thing that could be a box: size(4) + type(4). */
#define SP_HEIF_BOX_HEADER_LEN 8u

/* A box's declared size is a 4-byte (or 8-byte largesize) value. We refuse
 * anything whose 64-bit size does not fit the file, so no fixed numeric cap
 * is needed the way the chunk formats cap their 32-bit length fields; the
 * file size is the ceiling. This bound is the belt-and-suspenders loop guard
 * only, matching the other formats' MAX_CHUNKS. */
#define SP_HEIF_MAX_BOXES 1000000u

/* A meta box with more items than this is refused rather than parsed. Real
 * files have a handful; a value this large can only be an attempt to make the
 * item tables enormous. */
#define SP_HEIF_MAX_ITEMS 100000u

/* Likewise for extents within one item and references within iref. */
#define SP_HEIF_MAX_EXTENTS 100000u
#define SP_HEIF_MAX_REFS 100000u

/* One box, as produced by the top-level iterator. */
typedef struct {
    char     type[5];   /* 4-character box type, NUL-terminated             */
    uint64_t offset;    /* file offset of the box's first byte (its size)   */
    uint64_t size;      /* total box size in bytes, header included         */
    uint64_t body_off;  /* file offset of the first body byte (after the
                          * size/type, and after largesize if present)      */
    uint64_t body_len;  /* size - (body_off - offset)                       */
} sp_heif_box;

typedef struct {
    sp_file *f;
    uint64_t pos;       /* next byte to examine                             */
    uint64_t end;       /* one past the last byte this iterator may read    */
    uint32_t count;     /* boxes emitted so far                             */
    bool     finished;
} sp_heif_box_iter;

/* One extent of an item's data: a run of bytes somewhere in the file. Almost
 * every real item has exactly one. */
typedef struct {
    uint64_t offset;    /* extent offset, as stored (relative to base)      */
    uint64_t length;
} sp_heif_extent;

/* One item: its id, its four-character type from iinf, and where its bytes
 * live from iloc. The two are matched up by id when the meta box is parsed. */
typedef struct {
    uint32_t       id;
    char           type[5];        /* "Exif", "mime", "hvc1", "grid", ...   */
    uint64_t       infe_offset;    /* file offset of this item's infe box    */
    uint64_t       infe_size;      /* size of the infe box, header included  */
    uint8_t        construction;   /* iloc construction_method (0 = file)   */
    uint16_t       data_ref_index; /* iloc data_reference_index             */
    uint64_t       base_offset;    /* iloc base_offset                      */
    uint32_t       extent_count;
    sp_heif_extent extents[8];     /* inline; overflow sets too_many_extents */
    bool           too_many_extents;
    bool           have_location;  /* an iloc entry matched this id         */
} sp_heif_item;

/* One entry from iref: "from_id describes/derives-from these to_ids", the
 * four-character reference type telling which relationship it is (cdsc =
 * content description, dimg = derived image, thmb = thumbnail, ...). */
typedef struct {
    char     type[5];
    uint32_t from_id;
    uint32_t to_count;
    uint32_t to_ids[16];   /* inline; overflow sets too_many */
    bool     too_many;
} sp_heif_ref;

/* The whole parsed meta box, loaded at once. */
typedef struct {
    sp_heif_box  boxes[64];    /* meta's direct children, in file order     */
    uint32_t     box_count;
    bool         too_many_boxes;

    sp_heif_item items[64];
    uint32_t     item_count;
    bool         too_many_items;

    sp_heif_ref  refs[64];
    uint32_t     ref_count;
    bool         too_many_refs;

    uint32_t     primary_item;  /* from pitm, 0 if absent                   */
    bool         have_primary;

    /* True if an irot or imir property is present in iprp/ipco: the file
     * carries orientation natively, so it survives scrubbing untouched and no
     * synthetic block is needed. When false and the dropped EXIF carried an
     * orientation, that orientation would be lost — the one case the HEIC
     * path warns about, same as the other formats warn when they cannot put
     * a rotation back. */
    bool         has_rotation;

    /* Offsets of the meta box itself, for the rewriter. */
    uint64_t     meta_offset;
    uint64_t     meta_size;
    uint64_t     meta_body_off; /* after size/type/version/flags            */
} sp_heif_meta;

/* True if buf looks like the start of an ISOBMFF file with a HEIC-family
 * brand: a `ftyp` box whose major brand or one of whose compatible brands is
 * one this tool recognises (heic, heix, mif1, msf1, heim, hevc, miaf, ...).
 * len shorter than a minimal ftyp is false, never a read past the end. */
bool sp_heif_has_signature(const uint8_t *buf, size_t len);

/* Reads and validates the leading `ftyp` box from an already-open file.
 *   SP_OK            a HEIC-family ftyp
 *   SP_ERR_NOT_HEIF  not an ISOBMFF file, or an ISOBMFF file whose brands are
 *                     none this tool handles (a plain .mp4, say)
 *   SP_ERR_IO        the read failed
 * Position is unspecified afterward; a caller trying another probe next must
 * seek back to 0 first, same rule as the other probes. Does not consult the
 * filename. */
sp_status sp_heif_probe(sp_file *f);

/* Prepares to walk the top-level boxes of f (offset 0 to end of file). Reads
 * nothing yet. */
sp_status sp_heif_box_iter_init(sp_heif_box_iter *it, sp_file *f);

/* Prepares to walk the child boxes contained in [body_off, body_off + len).
 * Used to descend into a container box (meta's children, iprp's ipco, ...).
 * The caller is responsible for having skipped any FullBox version/flags. */
sp_status sp_heif_box_iter_init_range(sp_heif_box_iter *it, sp_file *f,
                                      uint64_t body_off, uint64_t len);

/* Produces the next box.
 *   SP_OK  *have true    box filled in; stream left at the box's end
 *   SP_OK  *have false   the range has been fully walked
 *   SP_ERR_TRUNCATED     a box's declared size runs past the range/file end,
 *                         or the range ends inside a box header
 *   SP_ERR_MALFORMED     a box smaller than its own header, a largesize that
 *                         overflows, or too many boxes
 */
sp_status sp_heif_box_next(sp_heif_box_iter *it, sp_heif_box *box, bool *have);

/* Parses the meta box whose header the caller has located (via the top-level
 * iterator) into the fully-loaded sp_heif_meta. Reads iinf, iloc, iref and
 * pitm; leaves other child boxes recorded in `boxes` but otherwise opaque.
 *   SP_OK              parsed; note the too_many_* flags for refusals
 *   SP_ERR_TRUNCATED   a child box or table runs past the meta box
 *   SP_ERR_MALFORMED   an unparseable table, or a count over a MAX_ bound
 * A too_many_* flag being set is not an error return on its own — the parse
 * succeeds and records what it could — but the rewriter treats any of them as
 * "too complex to touch safely" and refuses to scrub, which is the safe
 * response to a file more elaborate than this reader models. */
sp_status sp_heif_parse_meta(sp_file *f, const sp_heif_box *meta_box,
                             sp_heif_meta *out);

#endif /* SP_HEIF_H */
