/* XASMOS - .exb validator. Hardware-free: <stdint.h>/<stddef.h> only. */
#include "exb.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

uint32_t exb_crc32_image(const uint8_t *img, size_t size) {
    static const uint32_t tab[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        uint8_t b = (i >= 28 && i < 32) ? 0 : img[i];
        crc ^= b;
        crc = (crc >> 4) ^ tab[crc & 15];
        crc = (crc >> 4) ^ tab[crc & 15];
    }
    return ~crc;
}

static int overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; }

static void zero_out(ExbInfo *out) {
    uint8_t *z = (uint8_t *)out;
    for (size_t i = 0; i < sizeof(*out); i++) z[i] = 0;
}

static int parse_impl(const void *image, uint64_t size, ExbInfo *out) {
    const uint8_t *img = (const uint8_t *)image;
    if (!img || size < EXB_HEADER_SIZE || size > EXB_MAX_FILE) return EXB_E_SIZE;

    if (img[0] != 0x7F || img[1] != 'E' || img[2] != 'X' || img[3] != 'B') return EXB_E_MAGIC;
    if (img[4] != EXB_VERSION || img[5] != EXB_ENDIAN_LITTLE) return EXB_E_VERSION;
    if (le16(img + 6) != EXB_HEADER_SIZE) return EXB_E_HEADER;
    if (le32(img + 8) != 0) return EXB_E_HEADER;
    for (int i = 49; i < 64; i++) if (img[i] != 0) return EXB_E_HEADER;
    if (img[48] != EXB_KIND_USER) return EXB_E_KIND;
    if (le32(img + 12) != EXB_TARGET_AMD64) return EXB_E_TARGET;
    if (le32(img + 24) != size) return EXB_E_SIZE;
    if (le32(img + 28) != exb_crc32_image(img, (size_t)size)) return EXB_E_CRC;

    uint64_t entry = le64(img + 16);
    uint64_t seg_off = le32(img + 32), seg_cnt = le32(img + 36);
    uint64_t cap_off = le32(img + 40), cap_cnt = le32(img + 44);

    if (seg_cnt < 1 || seg_cnt > EXB_MAX_SEGS) return EXB_E_SEGTAB;
    uint64_t seg_end = seg_off + seg_cnt * EXB_SEG_SIZE;
    if (seg_off < EXB_HEADER_SIZE || seg_end > size) return EXB_E_SEGTAB;

    uint64_t cap_end = cap_off;
    if (cap_cnt > EXB_MAX_CAPS) return EXB_E_CAP;
    if (cap_cnt == 0) {
        if (cap_off != 0) return EXB_E_CAP;
    } else {
        cap_end = cap_off + cap_cnt * EXB_CAP_SIZE;
        if (cap_off < EXB_HEADER_SIZE || cap_end > size) return EXB_E_CAP;
        if (overlap(seg_off, seg_end, cap_off, cap_end)) return EXB_E_OVERLAP;
    }

    uint64_t v0[EXB_MAX_SEGS], v1[EXB_MAX_SEGS], d0[EXB_MAX_SEGS], d1[EXB_MAX_SEGS];
    uint64_t total_pages = 0;

    for (uint64_t i = 0; i < seg_cnt; i++) {
        const uint8_t *p = img + seg_off + i * EXB_SEG_SIZE;
        uint64_t vaddr = le64(p), mem = le64(p + 8), foff = le32(p + 16), fsz = le32(p + 20);
        uint32_t fl = le32(p + 24);

        if (le32(p + 28) != 0) return EXB_E_SEG;
        if (fl & ~(EXB_SEG_R | EXB_SEG_W | EXB_SEG_X)) return EXB_E_SEG;
        if (!(fl & EXB_SEG_R)) return EXB_E_SEG;
        if ((fl & EXB_SEG_W) && (fl & EXB_SEG_X)) return EXB_E_WX;
        if (vaddr & (EXB_PAGE - 1)) return EXB_E_SEG;
        if (vaddr < EXB_VA_MIN || vaddr >= EXB_VA_LIMIT) return EXB_E_SEG;
        if (mem == 0 || mem > EXB_MAX_IMAGE_BYTES) return EXB_E_SEG;

        uint64_t pages = (mem + EXB_PAGE - 1) / EXB_PAGE;
        uint64_t vend = vaddr + pages * EXB_PAGE;
        if (vend > EXB_VA_LIMIT) return EXB_E_SEG;

        if (fsz > mem) return EXB_E_SEG;
        if (fsz == 0) {
            if (foff != 0) return EXB_E_SEG;
        } else {
            if (foff < EXB_HEADER_SIZE) return EXB_E_SEG;
            if (foff + fsz > size) return EXB_E_SEG;
            if (overlap(foff, foff + fsz, seg_off, seg_end)) return EXB_E_OVERLAP;
            if (cap_cnt && overlap(foff, foff + fsz, cap_off, cap_end)) return EXB_E_OVERLAP;
        }

        total_pages += pages;
        if (total_pages > EXB_MAX_IMAGE_PAGES) return EXB_E_LIMIT;

        for (uint64_t j = 0; j < i; j++) {
            if (overlap(vaddr, vend, v0[j], v1[j])) return EXB_E_OVERLAP;
            if (fsz && d1[j] > d0[j] && overlap(foff, foff + fsz, d0[j], d1[j])) return EXB_E_OVERLAP;
        }
        v0[i] = vaddr; v1[i] = vend; d0[i] = foff; d1[i] = foff + fsz;

        out->seg[i].vaddr = vaddr;
        out->seg[i].mem_size = mem;
        out->seg[i].pages = pages;
        out->seg[i].file_off = (uint32_t)foff;
        out->seg[i].file_size = (uint32_t)fsz;
        out->seg[i].flags = fl;
    }
    out->seg_count = (uint32_t)seg_cnt;
    out->image_pages = total_pages;

    int entry_ok = 0;
    for (uint64_t i = 0; i < seg_cnt; i++) {
        const ExbSeg *s = &out->seg[i];
        if ((s->flags & EXB_SEG_X) && entry >= s->vaddr && entry - s->vaddr < s->file_size) entry_ok = 1;
    }
    if (!entry_ok) { zero_out(out); return EXB_E_ENTRY; }
    out->entry = entry;

    out->stack_bytes = (uint32_t)(EXB_DEFAULT_STACK_PAGES * EXB_PAGE);
    uint32_t seen = 0;
    for (uint64_t i = 0; i < cap_cnt; i++) {
        const uint8_t *p = img + cap_off + i * EXB_CAP_SIZE;
        uint32_t kind = le32(p);
        uint64_t value = le64(p + 8);
        if (le32(p + 4) != 0) { zero_out(out); return EXB_E_CAP; }
        uint32_t base = kind & ~EXB_CAP_OPTIONAL;
        if (base == 0) { zero_out(out); return EXB_E_CAP; }
        switch (base) {
        case EXB_CAP_STACK:
            if (seen & 1u) goto cap_fail;
            if (value % EXB_PAGE || value < EXB_PAGE || value > EXB_MAX_STACK_PAGES * EXB_PAGE) goto cap_fail;
            out->stack_bytes = (uint32_t)value; seen |= 1u; break;
        case EXB_CAP_HEAP_FRAMES:
            if (seen & 2u) goto cap_fail;
            if (value > EXB_MAX_HEAP_REQUEST) goto cap_fail;
            out->heap_frames = value; seen |= 2u; break;
        case EXB_CAP_TOKENS:
            if (seen & 4u) goto cap_fail;
            if (value > EXB_MAX_TOKEN_REQUEST) goto cap_fail;
            out->extra_tokens = (uint32_t)value; seen |= 4u; break;
        default:
            if (!(kind & EXB_CAP_OPTIONAL)) goto cap_fail;
            break;
        }
    }
    return EXB_OK;
cap_fail:
    zero_out(out);
    return EXB_E_CAP;
}

/* Public entry: any failure leaves *out zeroed, whatever path reported it. */
int exb_parse(const void *image, uint64_t size, ExbInfo *out) {
    int rc = parse_impl(image, size, out);
    if (rc != EXB_OK) zero_out(out);
    return rc;
}
