#include "heif.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Small readers. Every multi-byte value in ISOBMFF is big-endian.
 * ------------------------------------------------------------------ */

static uint64_t be_read(const uint8_t *p, unsigned n)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        v = (v << 8) | (uint64_t)p[i];
    return v;
}

/* Reads n bytes at the current position into a scratch buffer and advances.
 * n is bounded by callers to small header sizes, never attacker-scaled. */
static sp_status read_at(sp_file *f, uint64_t off, uint8_t *buf, size_t n)
{
    size_t got = 0;
    sp_status st = sp_seek(f, off);
    if (st != SP_OK)
        return st;
    st = sp_read_exact(f, buf, n, &got);
    if (st == SP_ERR_TRUNCATED || got < n)
        return SP_ERR_TRUNCATED;
    return st;
}

static bool is_heif_brand(const uint8_t *b)
{
    /* The brands that mean "this ISOBMFF file is a still image this tool
     * understands". Deliberately excludes plain video brands (isom, mp42,
     * avc1, qt): those are movies, out of scope, and must probe as "not
     * HEIF" so detect_format falls through cleanly rather than half-handling
     * a video container. */
    static const char *brands[] = {
        "heic", "heix", "heim", "heis", /* HEVC still and image sequence   */
        "hevc", "hevx", "hevm", "hevs", /* HEVC variants                   */
        "mif1", "msf1", "miaf",         /* generic MIAF image (family)     */
        "avif", "avis",                 /* AV1 image, same container shape */
    };
    size_t i;
    for (i = 0; i < sizeof brands / sizeof brands[0]; i++) {
        if (memcmp(b, brands[i], 4u) == 0)
            return true;
    }
    return false;
}

bool sp_heif_has_signature(const uint8_t *buf, size_t len)
{
    uint32_t size;
    uint32_t p;

    /* Need at least size(4) + "ftyp" + major_brand(4) + minor(4). */
    if (buf == NULL || len < 16u)
        return false;
    if (memcmp(buf + 4u, "ftyp", 4u) != 0)
        return false;

    if (is_heif_brand(buf + 8u)) /* major brand */
        return true;

    /* Otherwise scan the compatible-brands list, bounded by the ftyp size and
     * by however many bytes the caller actually gave us. */
    size = (uint32_t)be_read(buf, 4u);
    if (size > len)
        size = (uint32_t)len;
    for (p = 16u; p + 4u <= size; p += 4u) {
        if (is_heif_brand(buf + p))
            return true;
    }
    return false;
}

sp_status sp_heif_probe(sp_file *f)
{
    uint8_t hdr[512];
    uint32_t size;
    size_t want;
    size_t got = 0;
    sp_status st;

    if (f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;

    st = read_at(f, 0, hdr, 16u);
    if (st == SP_ERR_IO)
        return SP_ERR_IO;
    if (st == SP_ERR_TRUNCATED)
        return SP_ERR_NOT_HEIF; /* too short to be one; not a truncation gripe */

    if (memcmp(hdr + 4u, "ftyp", 4u) != 0)
        return SP_ERR_NOT_HEIF;

    /* Read the whole ftyp (capped) so the compatible-brands list is covered,
     * not just the major brand. */
    size = (uint32_t)be_read(hdr, 4u);
    if (size < 16u)
        return SP_ERR_NOT_HEIF;
    want = (size < sizeof hdr) ? size : sizeof hdr;
    st = sp_seek(f, 0);
    if (st != SP_OK)
        return st;
    st = sp_read_exact(f, hdr, want, &got);
    if (st == SP_ERR_IO)
        return SP_ERR_IO;
    /* A short read just means the ftyp claimed more than the file holds; we
     * can still answer from the bytes we got. */

    return sp_heif_has_signature(hdr, got) ? SP_OK : SP_ERR_NOT_HEIF;
}

/* ------------------------------------------------------------------ *
 * Top-level and range box iteration.
 * ------------------------------------------------------------------ */

sp_status sp_heif_box_iter_init(sp_heif_box_iter *it, sp_file *f)
{
    if (it == NULL || f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;
    memset(it, 0, sizeof *it);
    it->f = f;
    it->pos = 0;
    it->end = f->size;
    return SP_OK;
}

sp_status sp_heif_box_iter_init_range(sp_heif_box_iter *it, sp_file *f,
                                      uint64_t body_off, uint64_t len)
{
    if (it == NULL || f == NULL || f->fp == NULL)
        return SP_ERR_USAGE;
    if (body_off > f->size || len > f->size - body_off)
        return SP_ERR_TRUNCATED;
    memset(it, 0, sizeof *it);
    it->f = f;
    it->pos = body_off;
    it->end = body_off + len;
    return SP_OK;
}

sp_status sp_heif_box_next(sp_heif_box_iter *it, sp_heif_box *box, bool *have)
{
    uint8_t hdr[16];
    uint64_t size;
    uint64_t hdrlen = 8u;
    sp_status st;

    if (have != NULL)
        *have = false;
    if (it == NULL || it->f == NULL || box == NULL || have == NULL)
        return SP_ERR_USAGE;
    if (it->finished)
        return SP_OK;
    if (it->count >= SP_HEIF_MAX_BOXES)
        return SP_ERR_MALFORMED;

    /* A clean end: the range boundary reached exactly, or too few bytes left
     * for even a box header (trailing slack shorter than 8 bytes is treated
     * as clean end rather than a truncation, matching how the chunk parsers
     * treat a spent stream). */
    if (it->pos >= it->end || it->end - it->pos < SP_HEIF_BOX_HEADER_LEN) {
        it->finished = true;
        return SP_OK;
    }

    st = read_at(it->f, it->pos, hdr, 8u);
    if (st != SP_OK) {
        it->finished = true;
        return st;
    }

    size = be_read(hdr, 4u);
    if (size == 1u) {
        /* 64-bit largesize follows the type. */
        if (it->end - it->pos < 16u) {
            it->finished = true;
            return SP_ERR_TRUNCATED;
        }
        st = read_at(it->f, it->pos + 8u, hdr + 8u, 8u);
        if (st != SP_OK) {
            it->finished = true;
            return st;
        }
        size = be_read(hdr + 8u, 8u);
        hdrlen = 16u;
    } else if (size == 0u) {
        /* Box runs to the end of the enclosing range. */
        size = it->end - it->pos;
    }

    if (size < hdrlen) {
        it->finished = true;
        return SP_ERR_MALFORMED; /* smaller than its own header */
    }
    if (size > it->end - it->pos) {
        it->finished = true;
        return SP_ERR_TRUNCATED; /* declared past the range/file end */
    }

    memset(box, 0, sizeof *box);
    memcpy(box->type, hdr + 4u, 4u);
    box->type[4] = '\0';
    box->offset = it->pos;
    box->size = size;
    box->body_off = it->pos + hdrlen;
    box->body_len = size - hdrlen;

    it->pos += size;
    it->count++;
    *have = true;
    return SP_OK;
}

/* ------------------------------------------------------------------ *
 * Meta box parsing: iinf, iloc, iref, pitm.
 * ------------------------------------------------------------------ */

/* Reads a FullBox's version+flags at body_off, returning the version and the
 * offset of the byte after them. */
static sp_status read_fullbox_head(sp_file *f, uint64_t body_off, uint64_t end,
                                   uint8_t *version, uint64_t *after)
{
    uint8_t vf[4];
    sp_status st;
    if (end - body_off < 4u)
        return SP_ERR_TRUNCATED;
    st = read_at(f, body_off, vf, 4u);
    if (st != SP_OK)
        return st;
    *version = vf[0];
    *after = body_off + 4u;
    return SP_OK;
}

/* iinf: item id -> item type. Fills out->items[].id/.type; locations are
 * filled later by the iloc pass. */
static sp_status parse_iinf(sp_file *f, const sp_heif_box *box, sp_heif_meta *out)
{
    uint8_t ver;
    uint64_t p, end = box->offset + box->size;
    uint8_t buf[8];
    uint64_t entry_count;
    uint64_t i;
    sp_status st;

    st = read_fullbox_head(f, box->body_off, end, &ver, &p);
    if (st != SP_OK)
        return st;

    if (ver == 0u) {
        if (end - p < 2u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 2u);
        if (st != SP_OK)
            return st;
        entry_count = be_read(buf, 2u);
        p += 2u;
    } else {
        if (end - p < 4u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 4u);
        if (st != SP_OK)
            return st;
        entry_count = be_read(buf, 4u);
        p += 4u;
    }
    if (entry_count > SP_HEIF_MAX_ITEMS)
        return SP_ERR_MALFORMED;

    for (i = 0; i < entry_count; i++) {
        sp_heif_box infe;
        uint8_t ev;
        uint64_t ip;
        uint32_t item_id;
        uint8_t idbuf[8];

        /* Each entry is an `infe` box; read its header manually so the walk
         * stays inside this iinf's byte range. */
        {
            uint8_t bh[8];
            uint64_t bsize;
            if (end - p < 8u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, p, bh, 8u);
            if (st != SP_OK)
                return st;
            bsize = be_read(bh, 4u);
            if (bsize < 8u)
                return SP_ERR_MALFORMED;
            if (bsize > end - p)
                return SP_ERR_TRUNCATED;
            memset(&infe, 0, sizeof infe);
            memcpy(infe.type, bh + 4u, 4u);
            infe.type[4] = '\0';
            infe.offset = p;
            infe.size = bsize;
            infe.body_off = p + 8u;
            infe.body_len = bsize - 8u;
        }
        if (memcmp(infe.type, "infe", 4u) != 0) {
            /* Something other than infe where an item entry was promised. */
            return SP_ERR_MALFORMED;
        }

        st = read_fullbox_head(f, infe.body_off, infe.offset + infe.size,
                               &ev, &ip);
        if (st != SP_OK)
            return st;

        /* version 2: 16-bit id; version 3: 32-bit id. Older versions (0/1)
         * used a different layout without item_type; refuse them rather than
         * misread — real HEIC uses version >= 2. */
        if (ev == 2u) {
            if ((infe.offset + infe.size) - ip < 2u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, ip, idbuf, 2u);
            if (st != SP_OK)
                return st;
            item_id = (uint32_t)be_read(idbuf, 2u);
            ip += 2u;
        } else if (ev == 3u) {
            if ((infe.offset + infe.size) - ip < 4u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, ip, idbuf, 4u);
            if (st != SP_OK)
                return st;
            item_id = (uint32_t)be_read(idbuf, 4u);
            ip += 4u;
        } else {
            return SP_ERR_MALFORMED;
        }

        /* Skip item_protection_index(2), then item_type(4). */
        {
            uint8_t tbuf[6];
            if ((infe.offset + infe.size) - ip < 6u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, ip, tbuf, 6u);
            if (st != SP_OK)
                return st;
            if (out->item_count < (uint32_t)(sizeof out->items / sizeof out->items[0])) {
                sp_heif_item *it = &out->items[out->item_count++];
                memset(it, 0, sizeof *it);
                it->id = item_id;
                memcpy(it->type, tbuf + 2u, 4u);
                it->type[4] = '\0';
                it->infe_offset = infe.offset;
                it->infe_size = infe.size;
            } else {
                out->too_many_items = true;
            }
        }

        p += infe.size;
    }

    return SP_OK;
}

/* Finds the already-recorded item with this id, or NULL. */
static sp_heif_item *find_item(sp_heif_meta *out, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < out->item_count; i++) {
        if (out->items[i].id == id)
            return &out->items[i];
    }
    return NULL;
}

/* iloc: item id -> base offset + extents. */
static sp_status parse_iloc(sp_file *f, const sp_heif_box *box, sp_heif_meta *out)
{
    uint8_t ver;
    uint64_t p, end = box->offset + box->size;
    uint8_t sizes[2];
    unsigned offset_size, length_size, base_offset_size, index_size;
    uint64_t item_count, i;
    uint8_t buf[8];
    sp_status st;

    st = read_fullbox_head(f, box->body_off, end, &ver, &p);
    if (st != SP_OK)
        return st;

    if (end - p < 2u)
        return SP_ERR_TRUNCATED;
    st = read_at(f, p, sizes, 2u);
    if (st != SP_OK)
        return st;
    offset_size = (unsigned)(sizes[0] >> 4);
    length_size = (unsigned)(sizes[0] & 0x0Fu);
    base_offset_size = (unsigned)(sizes[1] >> 4);
    index_size = (unsigned)(sizes[1] & 0x0Fu);
    p += 2u;

    /* The four size fields are each 0, 4 or 8 in every real file. Anything
     * else (a 2, a 6, a 15) is a value we would misparse, so refuse. */
    if ((offset_size != 0u && offset_size != 4u && offset_size != 8u) ||
        (length_size != 0u && length_size != 4u && length_size != 8u) ||
        (base_offset_size != 0u && base_offset_size != 4u && base_offset_size != 8u) ||
        (index_size != 0u && index_size != 4u && index_size != 8u))
        return SP_ERR_MALFORMED;

    if (ver < 2u) {
        if (end - p < 2u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 2u);
        if (st != SP_OK)
            return st;
        item_count = be_read(buf, 2u);
        p += 2u;
    } else {
        if (end - p < 4u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 4u);
        if (st != SP_OK)
            return st;
        item_count = be_read(buf, 4u);
        p += 4u;
    }
    if (item_count > SP_HEIF_MAX_ITEMS)
        return SP_ERR_MALFORMED;

    for (i = 0; i < item_count; i++) {
        uint32_t item_id;
        uint64_t base_offset = 0;
        uint64_t extent_count, e;
        uint8_t construction = 0u;
        uint16_t dref = 0u;
        sp_heif_item *it;

        if (ver < 2u) {
            if (end - p < 2u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, p, buf, 2u);
            if (st != SP_OK)
                return st;
            item_id = (uint32_t)be_read(buf, 2u);
            p += 2u;
        } else {
            if (end - p < 4u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, p, buf, 4u);
            if (st != SP_OK)
                return st;
            item_id = (uint32_t)be_read(buf, 4u);
            p += 4u;
        }

        if (ver == 1u || ver == 2u) {
            /* construction_method: reserved(12) + method(4), 2 bytes. The
             * method is the low nibble of the second byte. */
            if (end - p < 2u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, p, buf, 2u);
            if (st != SP_OK)
                return st;
            construction = (uint8_t)(buf[1] & 0x0Fu);
            p += 2u;
        }

        /* data_reference_index(2). */
        if (end - p < 2u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 2u);
        if (st != SP_OK)
            return st;
        dref = (uint16_t)be_read(buf, 2u);
        p += 2u;

        if (base_offset_size > 0u) {
            if (end - p < base_offset_size)
                return SP_ERR_TRUNCATED;
            st = read_at(f, p, buf, base_offset_size);
            if (st != SP_OK)
                return st;
            base_offset = be_read(buf, base_offset_size);
            p += base_offset_size;
        }

        if (end - p < 2u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 2u);
        if (st != SP_OK)
            return st;
        extent_count = be_read(buf, 2u);
        p += 2u;
        if (extent_count > SP_HEIF_MAX_EXTENTS)
            return SP_ERR_MALFORMED;

        it = find_item(out, item_id);
        if (it != NULL) {
            it->construction = construction;
            it->data_ref_index = dref;
            it->base_offset = base_offset;
            it->extent_count = 0u;
            it->have_location = true;
        }

        for (e = 0; e < extent_count; e++) {
            uint64_t eo = 0, el = 0;
            if (index_size > 0u && (ver == 1u || ver == 2u)) {
                if (end - p < index_size)
                    return SP_ERR_TRUNCATED;
                p += index_size; /* extent_index, not needed */
            }
            if (offset_size > 0u) {
                if (end - p < offset_size)
                    return SP_ERR_TRUNCATED;
                st = read_at(f, p, buf, offset_size);
                if (st != SP_OK)
                    return st;
                eo = be_read(buf, offset_size);
                p += offset_size;
            }
            if (length_size > 0u) {
                if (end - p < length_size)
                    return SP_ERR_TRUNCATED;
                st = read_at(f, p, buf, length_size);
                if (st != SP_OK)
                    return st;
                el = be_read(buf, length_size);
                p += length_size;
            }
            if (it != NULL) {
                if (it->extent_count <
                        (uint32_t)(sizeof it->extents / sizeof it->extents[0])) {
                    it->extents[it->extent_count].offset = eo;
                    it->extents[it->extent_count].length = el;
                    it->extent_count++;
                } else {
                    it->too_many_extents = true;
                }
            }
        }
    }

    return SP_OK;
}

/* iref: from-item -> to-items, with a reference type. */
static sp_status parse_iref(sp_file *f, const sp_heif_box *box, sp_heif_meta *out)
{
    uint8_t ver;
    uint64_t p, end = box->offset + box->size;
    sp_status st;

    st = read_fullbox_head(f, box->body_off, end, &ver, &p);
    if (st != SP_OK)
        return st;

    while (p < end) {
        uint8_t bh[8];
        uint64_t bsize, bend;
        uint32_t from_id, tocount, k;
        unsigned idsz = (ver == 0u) ? 2u : 4u;
        uint8_t buf[4];
        sp_heif_ref *r = NULL;

        if (end - p < 8u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, bh, 8u);
        if (st != SP_OK)
            return st;
        bsize = be_read(bh, 4u);
        if (bsize < 8u)
            return SP_ERR_MALFORMED;
        if (bsize > end - p)
            return SP_ERR_TRUNCATED;
        bend = p + bsize;

        if (out->ref_count < (uint32_t)(sizeof out->refs / sizeof out->refs[0])) {
            r = &out->refs[out->ref_count++];
            memset(r, 0, sizeof *r);
            memcpy(r->type, bh + 4u, 4u);
            r->type[4] = '\0';
        } else {
            out->too_many_refs = true;
        }

        {
            uint64_t q = p + 8u;
            if (bend - q < idsz)
                return SP_ERR_TRUNCATED;
            st = read_at(f, q, buf, idsz);
            if (st != SP_OK)
                return st;
            from_id = (uint32_t)be_read(buf, idsz);
            q += idsz;

            if (bend - q < 2u)
                return SP_ERR_TRUNCATED;
            st = read_at(f, q, buf, 2u);
            if (st != SP_OK)
                return st;
            tocount = (uint32_t)be_read(buf, 2u);
            q += 2u;
            if (tocount > SP_HEIF_MAX_REFS)
                return SP_ERR_MALFORMED;

            if (r != NULL) {
                r->from_id = from_id;
                r->to_count = 0u;
            }
            for (k = 0; k < tocount; k++) {
                uint32_t to_id;
                if (bend - q < idsz)
                    return SP_ERR_TRUNCATED;
                st = read_at(f, q, buf, idsz);
                if (st != SP_OK)
                    return st;
                to_id = (uint32_t)be_read(buf, idsz);
                q += idsz;
                if (r != NULL) {
                    if (r->to_count <
                            (uint32_t)(sizeof r->to_ids / sizeof r->to_ids[0])) {
                        r->to_ids[r->to_count++] = to_id;
                    } else {
                        r->too_many = true;
                    }
                }
            }
        }

        p = bend;
    }

    return SP_OK;
}

static sp_status parse_pitm(sp_file *f, const sp_heif_box *box, sp_heif_meta *out)
{
    uint8_t ver;
    uint64_t p, end = box->offset + box->size;
    uint8_t buf[4];
    sp_status st;

    st = read_fullbox_head(f, box->body_off, end, &ver, &p);
    if (st != SP_OK)
        return st;
    if (ver == 0u) {
        if (end - p < 2u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 2u);
        if (st != SP_OK)
            return st;
        out->primary_item = (uint32_t)be_read(buf, 2u);
    } else {
        if (end - p < 4u)
            return SP_ERR_TRUNCATED;
        st = read_at(f, p, buf, 4u);
        if (st != SP_OK)
            return st;
        out->primary_item = (uint32_t)be_read(buf, 4u);
    }
    out->have_primary = true;
    return SP_OK;
}

sp_status sp_heif_parse_meta(sp_file *f, const sp_heif_box *meta_box,
                             sp_heif_meta *out)
{
    sp_heif_box_iter it;
    sp_heif_box child;
    bool have = false;
    uint8_t ver;
    uint64_t after;
    sp_status st;

    if (f == NULL || meta_box == NULL || out == NULL)
        return SP_ERR_USAGE;

    memset(out, 0, sizeof *out);
    out->meta_offset = meta_box->offset;
    out->meta_size = meta_box->size;

    /* meta is a FullBox: its children start after version+flags. */
    st = read_fullbox_head(f, meta_box->body_off,
                           meta_box->offset + meta_box->size, &ver, &after);
    if (st != SP_OK)
        return st;
    out->meta_body_off = after;

    st = sp_heif_box_iter_init_range(&it, f, after,
                                     (meta_box->offset + meta_box->size) - after);
    if (st != SP_OK)
        return st;

    /* First pass: record every child box in order, and note iinf so item
     * types are known before iloc fills locations. */
    while ((st = sp_heif_box_next(&it, &child, &have)) == SP_OK && have) {
        if (out->box_count < (uint32_t)(sizeof out->boxes / sizeof out->boxes[0])) {
            out->boxes[out->box_count++] = child;
        } else {
            out->too_many_boxes = true;
        }

        if (memcmp(child.type, "iinf", 4u) == 0) {
            st = parse_iinf(f, &child, out);
            if (st != SP_OK)
                return st;
        } else if (memcmp(child.type, "pitm", 4u) == 0) {
            st = parse_pitm(f, &child, out);
            if (st != SP_OK)
                return st;
        }
    }
    if (st != SP_OK)
        return st;

    /* Second pass over the recorded boxes for iloc and iref, now that the
     * item table exists to hang locations on. iprp is descended into here too,
     * only far enough to note whether a native rotation property exists. */
    {
        uint32_t i;
        for (i = 0; i < out->box_count; i++) {
            if (memcmp(out->boxes[i].type, "iloc", 4u) == 0) {
                st = parse_iloc(f, &out->boxes[i], out);
                if (st != SP_OK)
                    return st;
            } else if (memcmp(out->boxes[i].type, "iref", 4u) == 0) {
                st = parse_iref(f, &out->boxes[i], out);
                if (st != SP_OK)
                    return st;
            } else if (memcmp(out->boxes[i].type, "iprp", 4u) == 0) {
                /* iprp -> ipco -> {irot|imir|...}. Walk shallowly; a read
                 * failure here is not fatal to the parse, it just leaves
                 * has_rotation false (the safe, warn-if-EXIF-had-it default). */
                sp_heif_box_iter pit;
                sp_heif_box ipco;
                bool ph = false;
                if (sp_heif_box_iter_init_range(&pit, f, out->boxes[i].body_off,
                                                out->boxes[i].body_len) == SP_OK) {
                    while (sp_heif_box_next(&pit, &ipco, &ph) == SP_OK && ph) {
                        if (memcmp(ipco.type, "ipco", 4u) != 0)
                            continue;
                        {
                            sp_heif_box_iter cit;
                            sp_heif_box prop;
                            bool ch = false;
                            if (sp_heif_box_iter_init_range(&cit, f, ipco.body_off,
                                                            ipco.body_len) != SP_OK)
                                break;
                            while (sp_heif_box_next(&cit, &prop, &ch) == SP_OK && ch) {
                                if (memcmp(prop.type, "irot", 4u) == 0 ||
                                    memcmp(prop.type, "imir", 4u) == 0)
                                    out->has_rotation = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return SP_OK;
}
