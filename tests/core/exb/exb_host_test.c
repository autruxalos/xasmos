/* Host-side verification of the .exb parser, under ASan + UBSan.
 *   exb_host_test corpus DIR            every image must give its expected verdict
 *   exb_host_test fuzz DIR ITERS SEED   mutate the valid images; parser must never
 *                                       misbehave, and anything it ACCEPTS must satisfy
 *                                       the safety invariants checked below. */
#include "exb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *n; int c; } NAMES[] = {
    {"OK", EXB_OK}, {"E_SIZE", EXB_E_SIZE}, {"E_MAGIC", EXB_E_MAGIC}, {"E_VERSION", EXB_E_VERSION},
    {"E_HEADER", EXB_E_HEADER}, {"E_TARGET", EXB_E_TARGET}, {"E_CRC", EXB_E_CRC},
    {"E_SEGTAB", EXB_E_SEGTAB}, {"E_SEG", EXB_E_SEG}, {"E_WX", EXB_E_WX}, {"E_OVERLAP", EXB_E_OVERLAP},
    {"E_ENTRY", EXB_E_ENTRY}, {"E_CAP", EXB_E_CAP}, {"E_LIMIT", EXB_E_LIMIT}, {"E_KIND", EXB_E_KIND},
};
#define NNAMES (sizeof(NAMES) / sizeof(NAMES[0]))

static int code_of(const char *s) { for (size_t i = 0; i < NNAMES; i++) if (!strcmp(NAMES[i].n, s)) return NAMES[i].c; return 12345; }
static const char *name_of(int c) { for (size_t i = 0; i < NNAMES; i++) if (NAMES[i].c == c) return NAMES[i].n; return "?"; }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* Independent CRC-32 (bitwise, no table) so the nibble table in exb.c is checked too. */
static uint32_t crc_ref(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= (i >= 28 && i < 32) ? 0 : d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int is_zero(const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) if (b[i]) return 0; return 1; }
static int ovl(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; }

/* Everything the loader will rely on, re-derived from the raw bytes. Returns 0 if sound. */
static const char *invariants(const ExbInfo *in, const uint8_t *img, uint64_t size) {
    if (rd32(img + 24) != size) return "size field";
    if (rd32(img + 28) != crc_ref(img, (size_t)size)) return "crc";
    if (in->seg_count < 1 || in->seg_count > EXB_MAX_SEGS) return "seg_count";
    uint64_t so = rd32(img + 32), se = so + (uint64_t)in->seg_count * EXB_SEG_SIZE;
    uint64_t co = rd32(img + 40), cc = rd32(img + 44), ce = co + cc * EXB_CAP_SIZE;
    if (so < 64 || se > size) return "segtab bounds";
    if (cc && (co < 64 || ce > size || ovl(so, se, co, ce))) return "captab bounds";

    uint64_t pages = 0; int entry_ok = 0;
    for (uint32_t i = 0; i < in->seg_count; i++) {
        const ExbSeg *s = &in->seg[i];
        if (s->flags & ~7u) return "unknown flag";
        if (!(s->flags & EXB_SEG_R)) return "no read";
        if ((s->flags & EXB_SEG_W) && (s->flags & EXB_SEG_X)) return "W and X";
        if (s->vaddr % 4096 || s->vaddr < EXB_VA_MIN) return "vaddr";
        if (s->mem_size == 0 || s->mem_size > EXB_MAX_IMAGE_BYTES) return "mem";
        if (s->pages != (s->mem_size + 4095) / 4096) return "pages";
        if (s->vaddr + s->pages * 4096 > EXB_VA_LIMIT) return "end past limit";
        if (s->file_size > s->mem_size) return "filesz";
        if (s->file_size) {
            if (s->file_off < 64 || (uint64_t)s->file_off + s->file_size > size) return "data bounds";
            if (ovl(s->file_off, (uint64_t)s->file_off + s->file_size, so, se)) return "data/segtab";
            if (cc && ovl(s->file_off, (uint64_t)s->file_off + s->file_size, co, ce)) return "data/captab";
        }
        pages += s->pages;
        for (uint32_t j = 0; j < i; j++) {
            const ExbSeg *t = &in->seg[j];
            if (ovl(s->vaddr, s->vaddr + s->pages * 4096, t->vaddr, t->vaddr + t->pages * 4096)) return "virt overlap";
            if (s->file_size && t->file_size &&
                ovl(s->file_off, (uint64_t)s->file_off + s->file_size, t->file_off, (uint64_t)t->file_off + t->file_size)) return "data overlap";
        }
        if ((s->flags & EXB_SEG_X) && in->entry >= s->vaddr && in->entry - s->vaddr < s->file_size) entry_ok = 1;
    }
    if (!entry_ok) return "entry";
    if (pages != in->image_pages || pages > EXB_MAX_IMAGE_PAGES) return "page total";
    if (in->stack_bytes % 4096 || in->stack_bytes < 4096 || in->stack_bytes > EXB_MAX_STACK_PAGES * 4096u) return "stack";
    if (in->heap_frames > EXB_MAX_HEAP_REQUEST || in->extra_tokens > EXB_MAX_TOKEN_REQUEST) return "caps";
    return 0;
}

/* Parse a private, exact-size heap copy: any out-of-bounds read is an ASan error. */
static int parse_exact(const uint8_t *data, size_t len, ExbInfo *out) {
    uint8_t *copy = malloc(len ? len : 1);
    if (len) memcpy(copy, data, len);
    int rc = exb_parse(copy, len, out);
    if (rc == EXB_OK) {
        const char *bad = invariants(out, copy, len);
        if (bad) { fprintf(stderr, "ACCEPTED an unsound image: %s\n", bad); abort(); }
    } else if (!is_zero(out, sizeof(*out))) {
        fprintf(stderr, "rejected image (%s) but left a partially filled ExbInfo\n", name_of(rc));
        abort();
    }
    free(copy);
    return rc;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1); if (l && fread(b, 1, (size_t)l, f) != (size_t)l) exit(2);
    fclose(f); *n = (size_t)l; return b;
}

static int run_corpus(const char *dir) {
    char path[512]; snprintf(path, sizeof path, "%s/manifest.txt", dir);
    FILE *m = fopen(path, "r"); if (!m) { perror(path); return 2; }
    char name[128], exp[64]; int pass = 0, fail = 0;
    while (fscanf(m, "%127s %63s", name, exp) == 2) {
        snprintf(path, sizeof path, "%s/%s.exb", dir, name);
        size_t n; uint8_t *d = slurp(path, &n);
        ExbInfo info; int rc = parse_exact(d, n, &info);
        if (rc == code_of(exp)) pass++;
        else { fail++; printf("FAIL  %-34s expected %-10s got %s\n", name, exp, name_of(rc)); }
        free(d);
    }
    fclose(m);
    /* sizes the parser must reject without ever dereferencing the pointer */
    ExbInfo info;
    int unit = (exb_parse(0, 100, &info) == EXB_E_SIZE) &
               (exb_parse((const void *)8, EXB_MAX_FILE + 1ULL, &info) == EXB_E_SIZE) &
               (exb_parse((const void *)8, 63, &info) == EXB_E_SIZE) &
               (exb_parse((const void *)8, 0xFFFFFFFFFFFFFFFFULL, &info) == EXB_E_SIZE);
    if (!unit) { fail++; printf("FAIL  size guards must not touch the buffer\n"); }
    printf("corpus: %d correct verdicts, %d wrong\n", pass + (unit ? 1 : 0), fail);
    return fail != 0;
}

static uint64_t S;
static uint64_t rnd(void) { S ^= S >> 12; S ^= S << 25; S ^= S >> 27; return S * 0x2545F4914F6CDD1DULL; }

static const uint64_t INTERESTING[] = {
    0, 1, 2, 7, 8, 63, 64, 65, 0xFF, 0xFFF, 0x1000, 0x1001, 0x7FFFFFFF, 0x80000000ULL, 0xFFFFFFFFULL,
    EXB_VA_MIN, EXB_VA_MIN - 1, EXB_VA_LIMIT, EXB_VA_LIMIT - 4096, EXB_VA_LIMIT + 4096,
    1ULL << 47, 1ULL << 63, ~0ULL, EXB_MAX_IMAGE_BYTES, EXB_MAX_IMAGE_BYTES + 1,
};
#define NINT (sizeof(INTERESTING) / sizeof(INTERESTING[0]))

static int run_fuzz(const char *dir, unsigned long iters, uint64_t seed) {
    S = seed ? seed : 88172645463325252ULL;
    const char *bases[] = { "valid_full", "valid_nocaps", "valid_bss_only_segment", "valid_optional_unknown_cap" };
    long hist[64] = {0}; unsigned long accepted = 0;
    for (size_t b = 0; b < sizeof bases / sizeof bases[0]; b++) {
        char path[512]; snprintf(path, sizeof path, "%s/%s.exb", dir, bases[b]);
        size_t n0; uint8_t *orig = slurp(path, &n0);
        for (unsigned long it = 0; it < iters; it++) {
            size_t len = n0; uint8_t *w = malloc(n0 + 512); memcpy(w, orig, n0);
            int ops = 1 + (int)(rnd() % 4);
            while (ops--) switch (rnd() % 8) {
            case 0: if (len) { int k = 1 + (int)(rnd() % 8); while (k--) w[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); } break;
            case 1: if (len >= 4) { wr32(w + (rnd() % (len / 4)) * 4, (uint32_t)INTERESTING[rnd() % NINT]); } break;
            case 2: if (len >= 8) { uint64_t v = INTERESTING[rnd() % NINT]; size_t o = (rnd() % (len / 8)) * 8; wr32(w + o, (uint32_t)v); wr32(w + o + 4, (uint32_t)(v >> 32)); } break;
            case 3: len = rnd() % (len + 1); break;
            case 4: { size_t add = 1 + rnd() % 64; if (len + add > n0 + 448) break; for (size_t i = 0; i < add; i++) w[len + i] = (uint8_t)rnd(); len += add; break; }
            case 5: if (len > 64) { size_t o = 48 + rnd() % (len - 48); w[o] = (uint8_t)rnd(); } break;   /* anywhere past the fixed header */
            case 6: if (len >= 64) { size_t o = rnd() % 64; w[o] = (uint8_t)rnd(); } break;                /* header bytes */
            case 7: if (len >= 192) { size_t o = 64 + (rnd() % 8) * 4; wr32(w + o, (uint32_t)INTERESTING[rnd() % NINT]); } break; /* segment table words */
            }
            if (len >= 64 && rnd() % 100 < 90) { wr32(w + 24, (uint32_t)len); wr32(w + 28, crc_ref(w, len)); } /* reach the deep checks */
            ExbInfo info; int rc = parse_exact(w, len, &info);
            hist[-rc < 63 ? -rc : 63]++; if (rc == EXB_OK) accepted++;
            free(w);
        }
        free(orig);
    }
    printf("fuzz: %lu inputs, none crashed, none misread, %lu accepted (each re-verified)\n", iters * 4, accepted);
    printf("      verdicts reached:");
    for (size_t i = 0; i < NNAMES; i++) printf(" %s=%ld", NAMES[i].n, hist[-NAMES[i].c]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "corpus")) return run_corpus(argv[2]);
    if (argc >= 5 && !strcmp(argv[1], "fuzz")) return run_fuzz(argv[2], strtoul(argv[3], 0, 10), strtoull(argv[4], 0, 10));
    fprintf(stderr, "usage: corpus DIR | fuzz DIR ITERS SEED\n"); return 2;
}
