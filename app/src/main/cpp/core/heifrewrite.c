#include "heifrewrite.h"

#include <string.h>

#include "heif.h"
#include "heifpolicy.h"

/* A meta box larger than this is refused rather than buffered. The image data
 * lives in mdat, not meta, so a meta box is normally a few hundred bytes to a
 * few kilobytes; a megabyte is already far past anything real. */
#define SP_HEIF_META_BUF_MAX (1024u * 1024u)

/* ------------------------------------------------------------------ *
 * EXIF adapter. A HEIC Exif item opens with a 4-byte
 * exif_tiff_header_offset, then that many bytes (the "Exif\0\0"
 * identifier in practice), then the TIFF data.
 * ------------------------------------------------------------------ */

static const uint8_t *heif_exif_tiff(const uint8_t *item_data, size_t len,
                                     size_t *tiff_len)
{
    uint32_t hdr_off;
    size_t skip;

    if (item_data == NULL || len < 4u)
        return NULL;
    hdr_off = ((uint32_t)item_data[0] << 24) | ((uint32_t)item_data[1] << 16) |
              ((uint32_t)item_data[2] << 8) | (uint32_t)item_data[3];
    skip = 4u + (size_t)hdr_off;
    if (skip > len)
        return NULL;
    *tiff_len = len - skip;
    return item_data + skip;
}

sp_status sp_heif_exif_scan(const uint8_t *item_data, size_t len,
                            sp_exif_info *out)
{
    static uint8_t combined[SP_EXIF_ID_LEN + SP_EXIF_MAX_SCAN];
    size_t tlen = 0;
    const uint8_t *tiff;

    if (out == NULL)
        return SP_ERR_USAGE;
    tiff = heif_exif_tiff(item_data, len, &tlen);
    if (tiff == NULL)
        return SP_ERR_MALFORMED;
    if (tlen > SP_EXIF_MAX_SCAN)
        tlen = SP_EXIF_MAX_SCAN;

    memcpy(combined, SP_EXIF_ID, SP_EXIF_ID_LEN);
    memcpy(combined + SP_EXIF_ID_LEN, tiff, tlen);
    return sp_exif_scan(combined, SP_EXIF_ID_LEN + tlen, out);
}

bool sp_heif_exif_is_minimal_orientation(const uint8_t *item_data, size_t len)
{
    static uint8_t combined[SP_EXIF_ID_LEN + SP_EXIF_MAX_SCAN];
    size_t tlen = 0;
    const uint8_t *tiff = heif_exif_tiff(item_data, len, &tlen);

    if (tiff == NULL || tlen > SP_EXIF_MAX_SCAN)
        return false;
    memcpy(combined, SP_EXIF_ID, SP_EXIF_ID_LEN);
    memcpy(combined + SP_EXIF_ID_LEN, tiff, tlen);
    return sp_exif_is_minimal_orientation(combined, SP_EXIF_ID_LEN + tlen);
}

/* ------------------------------------------------------------------ *
 * Byte-buffer builder for the rebuilt meta box.
 * ------------------------------------------------------------------ */

typedef struct {
    uint8_t *p;
    size_t   cap;
    size_t   len;
    bool     overflow;
} mbuf;

static void mb_bytes(mbuf *b, const uint8_t *src, size_t n)
{
    if (b->overflow || n > b->cap - b->len) {
        b->overflow = true;
        return;
    }
    memcpy(b->p + b->len, src, n);
    b->len += n;
}

static void mb_be(mbuf *b, uint64_t v, unsigned n)
{
    uint8_t tmp[8];
    unsigned i;
    for (i = 0; i < n; i++)
        tmp[n - 1u - i] = (uint8_t)(v & 0xFFu), v >>= 8;
    mb_bytes(b, tmp, n);
}

static void mb_tag(mbuf *b, const char *tag)
{
    mb_bytes(b, (const uint8_t *)tag, 4u);
}

/* Overwrites 4 big-endian bytes already in the buffer (a box size backpatch). */
static void mb_patch_be32(mbuf *b, size_t at, uint32_t v)
{
    if (at + 4u > b->len)
        return;
    b->p[at]     = (uint8_t)(v >> 24);
    b->p[at + 1u] = (uint8_t)(v >> 16);
    b->p[at + 2u] = (uint8_t)(v >> 8);
    b->p[at + 3u] = (uint8_t)v;
}

/* Reads len bytes at off from in straight into the meta buffer. */
static sp_status mb_copy_from_file(mbuf *b, sp_file *in, uint64_t off, uint64_t len)
{
    size_t got = 0;
    sp_status st;
    if (b->overflow || len > (uint64_t)(b->cap - b->len)) {
        b->overflow = true;
        return SP_OK; /* overflow is reported via b->overflow, checked by caller */
    }
    st = sp_seek(in, off);
    if (st != SP_OK)
        return st;
    st = sp_read_exact(in, b->p + b->len, (size_t)len, &got);
    if (st == SP_ERR_TRUNCATED || got < len)
        return SP_ERR_TRUNCATED;
    if (st != SP_OK)
        return st;
    b->len += (size_t)len;
    return SP_OK;
}

/* ------------------------------------------------------------------ *
 * Rewrite state.
 * ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    uint64_t new_base;   /* new absolute file offset of this item's data     */
    uint64_t total_len;  /* sum of the item's extent lengths                 */
    uint64_t src_start;  /* original absolute data start (for ordering)      */
} kept_item;

typedef struct {
    kept_item kept[64];
    uint32_t  kept_count;
    uint32_t  dropped_ids[64];
    uint32_t  dropped_count;
    unsigned  base_offset_size; /* 4 or 8, chosen from the file size         */
    unsigned  length_size;      /* 4 or 8, chosen from the largest item      */
} rw_plan;

static bool id_in(const uint32_t *arr, uint32_t n, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (arr[i] == id)
            return true;
    return false;
}

static bool is_kept(const rw_plan *pl, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < pl->kept_count; i++)
        if (pl->kept[i].id == id)
            return true;
    return false;
}

/* Emits one rebuilt meta child box, or copies it verbatim. */
static sp_status emit_iinf(mbuf *b, sp_file *in, const sp_heif_box *box,
                           const sp_heif_meta *m, const rw_plan *pl)
{
    uint8_t vf[4];
    size_t sz_at;
    size_t got = 0;
    uint8_t version;
    uint32_t i;
    sp_status st;

    st = sp_seek(in, box->body_off);
    if (st != SP_OK)
        return st;
    st = sp_read_exact(in, vf, 4u, &got);
    if (st != SP_OK || got < 4u)
        return SP_ERR_TRUNCATED;
    version = vf[0];

    sz_at = b->len;
    mb_be(b, 0u, 4u);      /* size, backpatched */
    mb_tag(b, "iinf");
    mb_bytes(b, vf, 4u);   /* version + flags, preserved */
    if (version == 0u)
        mb_be(b, pl->kept_count, 2u);
    else
        mb_be(b, pl->kept_count, 4u);

    /* Kept items' infe boxes, in the original iinf order. */
    for (i = 0; i < m->item_count; i++) {
        const sp_heif_item *it = &m->items[i];
        if (!is_kept(pl, it->id))
            continue;
        st = mb_copy_from_file(b, in, it->infe_offset, it->infe_size);
        if (st != SP_OK)
            return st;
        if (b->overflow)
            return SP_OK;
    }

    if (!b->overflow)
        mb_patch_be32(b, sz_at, (uint32_t)(b->len - sz_at));
    return SP_OK;
}

static void emit_iloc(mbuf *b, const rw_plan *pl)
{
    size_t sz_at;
    uint32_t i;

    sz_at = b->len;
    mb_be(b, 0u, 4u);        /* size, backpatched */
    mb_tag(b, "iloc");
    mb_be(b, 0u, 4u);        /* version 0, flags 0 */
    /* offset_size=4, length_size=pl->length_size */
    mb_be(b, (uint64_t)((4u << 4) | (pl->length_size & 0x0Fu)), 1u);
    /* base_offset_size=pl->base_offset_size, index_size=0 */
    mb_be(b, (uint64_t)((pl->base_offset_size << 4) | 0u), 1u);
    mb_be(b, pl->kept_count, 2u);   /* item_count (version 0: 16-bit) */

    for (i = 0; i < pl->kept_count; i++) {
        const kept_item *k = &pl->kept[i];
        mb_be(b, k->id, 2u);                        /* item_id */
        mb_be(b, 0u, 2u);                           /* data_reference_index */
        mb_be(b, k->new_base, pl->base_offset_size);/* base_offset */
        mb_be(b, 1u, 2u);                           /* extent_count */
        mb_be(b, 0u, 4u);                           /* extent_offset (offset_size=4) */
        mb_be(b, k->total_len, pl->length_size);    /* extent_length */
    }

    if (!b->overflow)
        mb_patch_be32(b, sz_at, (uint32_t)(b->len - sz_at));
}

/* Returns true if it wrote an iref (some references survived), false if the
 * box should be omitted entirely because none did. */
static bool emit_iref(mbuf *b, const sp_heif_meta *m, const rw_plan *pl)
{
    size_t sz_at;
    uint32_t i, k;
    bool any = false;

    /* Peek first: are there any survivors at all? */
    for (i = 0; i < m->ref_count && !any; i++) {
        const sp_heif_ref *r = &m->refs[i];
        if (!is_kept(pl, r->from_id))
            continue;
        for (k = 0; k < r->to_count; k++)
            if (is_kept(pl, r->to_ids[k])) { any = true; break; }
    }
    if (!any)
        return false;

    sz_at = b->len;
    mb_be(b, 0u, 4u);      /* size, backpatched */
    mb_tag(b, "iref");
    mb_be(b, 0u, 4u);      /* version 0, flags 0 */

    for (i = 0; i < m->ref_count; i++) {
        const sp_heif_ref *r = &m->refs[i];
        uint32_t survivors[16];
        uint32_t nsurv = 0;
        size_t rsz_at;

        if (!is_kept(pl, r->from_id))
            continue;
        for (k = 0; k < r->to_count; k++) {
            if (is_kept(pl, r->to_ids[k]) &&
                nsurv < (uint32_t)(sizeof survivors / sizeof survivors[0]))
                survivors[nsurv++] = r->to_ids[k];
        }
        if (nsurv == 0u)
            continue;

        rsz_at = b->len;
        mb_be(b, 0u, 4u);              /* single-reference box size */
        mb_tag(b, r->type);
        mb_be(b, r->from_id, 2u);      /* version 0: 16-bit ids */
        mb_be(b, nsurv, 2u);
        for (k = 0; k < nsurv; k++)
            mb_be(b, survivors[k], 2u);
        if (!b->overflow)
            mb_patch_be32(b, rsz_at, (uint32_t)(b->len - rsz_at));
    }

    if (!b->overflow)
        mb_patch_be32(b, sz_at, (uint32_t)(b->len - sz_at));
    return true;
}

/* Builds the whole rebuilt meta box into b. Field widths in pl are already
 * fixed, so the produced length does not depend on the new_base values — the
 * caller relies on that to build once for sizing and again with real offsets. */
static sp_status build_meta(mbuf *b, sp_file *in, const sp_heif_meta *m,
                            const rw_plan *pl)
{
    uint8_t vf[4];
    size_t got = 0;
    size_t sz_at;
    uint32_t i;
    sp_status st;

    b->len = 0;
    b->overflow = false;

    /* meta box header + its own version/flags. */
    st = sp_seek(in, m->meta_offset + 8u); /* body starts after size+type */
    if (st != SP_OK)
        return st;
    st = sp_read_exact(in, vf, 4u, &got);
    if (st != SP_OK || got < 4u)
        return SP_ERR_TRUNCATED;

    sz_at = b->len;
    mb_be(b, 0u, 4u);      /* size, backpatched */
    mb_tag(b, "meta");
    mb_bytes(b, vf, 4u);   /* meta's version + flags */

    for (i = 0; i < m->box_count; i++) {
        const sp_heif_box *c = &m->boxes[i];
        if (memcmp(c->type, "iinf", 4u) == 0) {
            st = emit_iinf(b, in, c, m, pl);
            if (st != SP_OK)
                return st;
        } else if (memcmp(c->type, "iloc", 4u) == 0) {
            emit_iloc(b, pl);
        } else if (memcmp(c->type, "iref", 4u) == 0) {
            (void)emit_iref(b, m, pl);
        } else {
            st = mb_copy_from_file(b, in, c->offset, c->size);
            if (st != SP_OK)
                return st;
        }
        if (b->overflow)
            return SP_OK; /* caller checks overflow */
    }

    if (!b->overflow)
        mb_patch_be32(b, sz_at, (uint32_t)(b->len - sz_at));
    return SP_OK;
}

/* ------------------------------------------------------------------ *
 * The rewrite.
 * ------------------------------------------------------------------ */

/* One shared meta build buffer, same non-reentrant convention as the other
 * rewriters' static EXIF scan buffers. */
static uint8_t g_meta_buf[SP_HEIF_META_BUF_MAX];

sp_status sp_heif_rewrite(sp_file *in, sp_out *out, const sp_policy *pol,
                          FILE *listing, sp_rewrite_stats *stats)
{
    sp_heif_box_iter it;
    sp_heif_box box;
    sp_heif_box top[64];
    uint32_t top_count = 0;
    int meta_idx = -1;
    int mdat_idx = -1;
    bool have;
    sp_heif_meta m;
    rw_plan pl;
    mbuf mb;
    uint64_t mdat_body_start, mdat_hdr_len;
    uint64_t new_mdat_offset, new_mdat_body_start, new_mdat_size;
    int64_t meta_delta;
    uint64_t new_meta_size, out_size;
    uint64_t max_item_len = 0;
    uint32_t i;
    uint32_t running;
    sp_status st;

    if (in == NULL || out == NULL || stats == NULL)
        return SP_ERR_USAGE;

    memset(stats, 0, sizeof *stats);
    stats->in_size = in->size;
    memset(&pl, 0, sizeof pl);

    /* --- walk the top-level boxes --- */
    st = sp_heif_box_iter_init(&it, in);
    if (st != SP_OK)
        return st;
    while ((st = sp_heif_box_next(&it, &box, &have)) == SP_OK && have) {
        if (top_count >= (uint32_t)(sizeof top / sizeof top[0]))
            return SP_ERR_UNSUPPORTED; /* absurdly many top-level boxes */
        top[top_count] = box;

        if (memcmp(box.type, "meta", 4u) == 0) {
            if (meta_idx >= 0)
                return SP_ERR_UNSUPPORTED; /* more than one meta */
            meta_idx = (int)top_count;
        } else if (memcmp(box.type, "moov", 4u) == 0) {
            /* An image sequence / movie box carries its own offset tables
             * (stco/co64) that this rewriter does not touch. Refuse rather
             * than relocate mdat out from under them. */
            return SP_ERR_UNSUPPORTED;
        }
        top_count++;
    }
    if (st != SP_OK)
        return st;
    if (meta_idx < 0)
        return SP_ERR_MALFORMED; /* a HEIC with no meta box is broken */

    /* --- parse the meta box --- */
    st = sp_heif_parse_meta(in, &top[meta_idx], &m);
    if (st != SP_OK)
        return st;
    if (m.too_many_boxes || m.too_many_items || m.too_many_refs)
        return SP_ERR_UNSUPPORTED;

    /* --- classify items, build the keep/drop plan --- */
    for (i = 0; i < m.item_count; i++) {
        const sp_heif_item *item = &m.items[i];
        sp_heif_decision d = sp_heifpolicy_decide(pol, item);
        uint64_t total = 0, e;

        if (listing != NULL && !sp_heifkind_is_structural(d.kind)) {
            fprintf(listing, "  %-6s  %-6s  %8s  %s",
                    (d.action == SP_DROP) ? "drop" : "keep",
                    item->type, "item", d.label);
            if (d.reason[0] != '\0')
                fprintf(listing, "  (%s)", d.reason);
            fputc('\n', listing);
        }

        /* Every located item must be a plain in-file, single-reference,
         * construction-0 item for this rewriter to be safe. */
        if (item->have_location) {
            if (item->construction != 0u || item->data_ref_index != 0u ||
                item->too_many_extents)
                return SP_ERR_UNSUPPORTED;
        }

        if (d.action == SP_DROP) {
            /* Scan the Exif item once for the run summary. */
            if (sp_heifkind_is_exif(d.kind) && !stats->have_exif &&
                item->have_location && item->extent_count == 1u) {
                static uint8_t ebuf[SP_EXIF_MAX_SCAN];
                uint64_t elen = item->extents[0].length;
                size_t got = 0;
                if (elen > 0u && elen <= sizeof ebuf) {
                    if (sp_seek(in, item->base_offset + item->extents[0].offset) == SP_OK &&
                        sp_read_exact(in, ebuf, (size_t)elen, &got) == SP_OK &&
                        sp_heif_exif_scan(ebuf, got, &stats->exif) == SP_OK)
                        stats->have_exif = true;
                }
            }
            if (pl.dropped_count < (uint32_t)(sizeof pl.dropped_ids / sizeof pl.dropped_ids[0]))
                pl.dropped_ids[pl.dropped_count++] = item->id;
            else
                return SP_ERR_UNSUPPORTED;
            continue;
        }

        /* Kept. A kept item must be locatable and its id must fit the
         * version-0 tables this rewriter emits. */
        if (!item->have_location)
            return SP_ERR_UNSUPPORTED;
        if (item->id > 0xFFFFu)
            return SP_ERR_UNSUPPORTED;
        for (e = 0; e < item->extent_count; e++)
            total += item->extents[e].length;
        if (total > max_item_len)
            max_item_len = total;

        if (pl.kept_count >= (uint32_t)(sizeof pl.kept / sizeof pl.kept[0]))
            return SP_ERR_UNSUPPORTED;
        pl.kept[pl.kept_count].id = item->id;
        pl.kept[pl.kept_count].total_len = total;
        pl.kept[pl.kept_count].src_start =
            item->base_offset + (item->extent_count > 0u ? item->extents[0].offset : 0u);
        pl.kept[pl.kept_count].new_base = 0u; /* filled after sizing */
        pl.kept_count++;
    }

    /* The primary item must survive; if pitm points at something we dropped,
     * the file's own designated image is being removed — refuse. */
    if (m.have_primary && id_in(pl.dropped_ids, pl.dropped_count, m.primary_item))
        return SP_ERR_UNSUPPORTED;

    /* --- nothing to drop: a true byte-for-byte no-op --- */
    if (pl.dropped_count == 0u) {
        stats->kept = m.item_count;
        stats->dropped = 0u;
        stats->out_size = in->size;
        return sp_copy_range(in, out, 0u, in->size);
    }

    /* Reference ids must also fit the 16-bit tables. */
    for (i = 0; i < m.ref_count; i++) {
        uint32_t k;
        if (m.refs[i].from_id > 0xFFFFu || m.refs[i].too_many)
            return SP_ERR_UNSUPPORTED;
        for (k = 0; k < m.refs[i].to_count; k++)
            if (m.refs[i].to_ids[k] > 0xFFFFu)
                return SP_ERR_UNSUPPORTED;
    }

    pl.base_offset_size = (in->size > 0xFFFFFFFFu) ? 8u : 4u;
    pl.length_size = (max_item_len > 0xFFFFFFFFu) ? 8u : 4u;

    /* --- find the single mdat that holds every kept item's data --- */
    for (i = 0; i < pl.kept_count; i++) {
        uint64_t s = pl.kept[i].src_start;
        uint64_t end = s + pl.kept[i].total_len;
        int found = -1;
        uint32_t j;
        for (j = 0; j < top_count; j++) {
            uint64_t bs = top[j].body_off;
            uint64_t be = top[j].offset + top[j].size;
            if (memcmp(top[j].type, "mdat", 4u) == 0 && s >= bs && end <= be) {
                found = (int)j;
                break;
            }
        }
        if (found < 0)
            return SP_ERR_UNSUPPORTED; /* item data not inside a top-level mdat */
        if (mdat_idx < 0)
            mdat_idx = found;
        else if (mdat_idx != found)
            return SP_ERR_UNSUPPORTED; /* kept items span more than one mdat */
    }
    if (mdat_idx < 0)
        return SP_ERR_UNSUPPORTED; /* no image data located; refuse */

    mdat_hdr_len = top[mdat_idx].body_off - top[mdat_idx].offset;
    mdat_body_start = top[mdat_idx].body_off;
    (void)mdat_body_start;

    /* --- order kept items by original data position (stable insertion) --- */
    for (i = 1; i < pl.kept_count; i++) {
        kept_item key = pl.kept[i];
        uint32_t j = i;
        while (j > 0u && pl.kept[j - 1u].src_start > key.src_start) {
            pl.kept[j] = pl.kept[j - 1u];
            j--;
        }
        pl.kept[j] = key;
    }

    /* --- size the rebuilt meta box (offsets still zero) --- */
    mb.p = g_meta_buf;
    mb.cap = sizeof g_meta_buf;
    mb.len = 0;
    mb.overflow = false;
    st = build_meta(&mb, in, &m, &pl);
    if (st != SP_OK)
        return st;
    if (mb.overflow)
        return SP_ERR_UNSUPPORTED; /* meta larger than we will buffer */
    new_meta_size = mb.len;

    /* --- lay out the new mdat and assign each kept item its new offset --- */
    meta_delta = (int64_t)new_meta_size - (int64_t)m.meta_size;
    if (top[meta_idx].offset < top[mdat_idx].offset)
        new_mdat_offset = top[mdat_idx].offset + (uint64_t)((int64_t)0 + meta_delta);
    else
        new_mdat_offset = top[mdat_idx].offset;
    new_mdat_body_start = new_mdat_offset + mdat_hdr_len;

    running = 0u; /* byte cursor within the new mdat body, as an offset delta */
    {
        uint64_t cursor = new_mdat_body_start;
        for (i = 0; i < pl.kept_count; i++) {
            pl.kept[i].new_base = cursor;
            cursor += pl.kept[i].total_len;
        }
        new_mdat_size = mdat_hdr_len + (cursor - new_mdat_body_start);
    }
    (void)running;

    /* --- rebuild meta again, now with real offsets; size must match --- */
    {
        size_t sized_len = (size_t)new_meta_size;
        mb.len = 0;
        mb.overflow = false;
        st = build_meta(&mb, in, &m, &pl);
        if (st != SP_OK)
            return st;
        if (mb.overflow || mb.len != sized_len)
            return SP_ERR_UNSUPPORTED; /* sizing drifted: refuse rather than emit */
    }

    out_size = in->size
             - (m.meta_size - new_meta_size)
             - (top[mdat_idx].size - new_mdat_size);
    if (out_size > in->size)
        return SP_ERR_OUTPUT_GREW; /* bug detector, same as the other rewriters */

    /* --- write: every top-level box in order, meta and mdat rebuilt --- */
    for (i = 0; i < top_count; i++) {
        if ((int)i == meta_idx) {
            st = sp_out_write(out, mb.p, mb.len);
            if (st != SP_OK)
                return st;
        } else if ((int)i == mdat_idx) {
            uint8_t hdr[16];
            uint32_t j;
            /* Reproduce the original mdat header length (8 or 16). */
            if (mdat_hdr_len == 16u) {
                hdr[0] = 0; hdr[1] = 0; hdr[2] = 0; hdr[3] = 1;
                memcpy(hdr + 4u, "mdat", 4u);
                hdr[8]  = (uint8_t)(new_mdat_size >> 56);
                hdr[9]  = (uint8_t)(new_mdat_size >> 48);
                hdr[10] = (uint8_t)(new_mdat_size >> 40);
                hdr[11] = (uint8_t)(new_mdat_size >> 32);
                hdr[12] = (uint8_t)(new_mdat_size >> 24);
                hdr[13] = (uint8_t)(new_mdat_size >> 16);
                hdr[14] = (uint8_t)(new_mdat_size >> 8);
                hdr[15] = (uint8_t)new_mdat_size;
                st = sp_out_write(out, hdr, 16u);
            } else {
                hdr[0] = (uint8_t)(new_mdat_size >> 24);
                hdr[1] = (uint8_t)(new_mdat_size >> 16);
                hdr[2] = (uint8_t)(new_mdat_size >> 8);
                hdr[3] = (uint8_t)new_mdat_size;
                memcpy(hdr + 4u, "mdat", 4u);
                st = sp_out_write(out, hdr, 8u);
            }
            if (st != SP_OK)
                return st;
            /* Kept items' data, packed contiguously in the ordered layout. */
            for (j = 0; j < pl.kept_count; j++) {
                /* Copy this item's extents from the input in order. Re-find
                 * the source item to walk its extents. */
                uint32_t q;
                for (q = 0; q < m.item_count; q++) {
                    const sp_heif_item *item = &m.items[q];
                    uint32_t e;
                    if (item->id != pl.kept[j].id)
                        continue;
                    for (e = 0; e < item->extent_count; e++) {
                        st = sp_copy_range(in, out,
                                           item->base_offset + item->extents[e].offset,
                                           item->extents[e].length);
                        if (st != SP_OK)
                            return st;
                    }
                    break;
                }
            }
        } else {
            st = sp_copy_range(in, out, top[i].offset, top[i].size);
            if (st != SP_OK)
                return st;
        }
    }

    stats->kept = pl.kept_count;
    stats->dropped = pl.dropped_count;
    stats->out_size = out_size;
    /* Orientation is preserved when the native irot/imir property is present
     * (it is copied through untouched). When it is absent, a dropped EXIF's
     * orientation really would be lost, and the caller's warning should
     * fire — same contract the other formats' orientation_kept carries. */
    stats->orientation_kept = m.has_rotation;
    return SP_OK;
}
