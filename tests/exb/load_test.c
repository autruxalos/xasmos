/* Host test for the portable loader (load.c).
 * The backend is a fake: it records every frame, mapping and release, and it
 * can fail any call on demand. Each failure must leave no live resource behind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "exb.h"
#include "xload.h"
#include "xload_backend.h"
#include "capability.h"

/* The arch-defined address space, faked for the host. */
struct XAspace { int id; };

#define FT 64
#define FM 64

typedef struct { int live; uint64_t frames; uint32_t rights; uint8_t *mem; int mapped; } FTok;
typedef struct { int live; uint64_t va; uint64_t tok; uint32_t perms; } FMap;

static FTok ft[FT];
static FMap fm[FM];
static int alloc_calls, map_calls;
static int fail_alloc_at = -1, fail_map_at = -1, quota_ok_answer = 1;
static int violations;   /* backend contract breaches seen by the fake */

static int passes, fails;
#define CHECK(cond, ...) do { if (cond) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static void reset(void) {
    for (int i = 0; i < FT; i++) { free(ft[i].mem); ft[i].mem = NULL; ft[i].live = 0; ft[i].mapped = 0; }
    for (int i = 0; i < FM; i++) fm[i].live = 0;
    alloc_calls = map_calls = 0;
    fail_alloc_at = fail_map_at = -1;
    quota_ok_answer = 1;
    violations = 0;
}

static int live_tokens(void) { int n = 0; for (int i = 0; i < FT; i++) n += ft[i].live; return n; }
static int live_maps(void)   { int n = 0; for (int i = 0; i < FM; i++) n += fm[i].live; return n; }
static int live_frames(void) { uint64_t n = 0; for (int i = 0; i < FT; i++) if (ft[i].live) n += ft[i].frames; return (int)n; }

/* ---- the fake backend ---- */
int xlb_quota_ok(uint32_t owner, uint64_t frames, uint32_t tokens) {
    (void)owner; (void)frames; (void)tokens;
    return quota_ok_answer;
}

int xlb_alloc(uint32_t owner, uint64_t frames, uint32_t rights, uint64_t *token) {
    (void)owner;
    int k = alloc_calls++;
    if (k == fail_alloc_at) return XLOAD_E_NOMEM;
    for (int i = 0; i < FT; i++) {
        if (!ft[i].live) {
            ft[i].live = 1;
            ft[i].frames = frames;
            ft[i].rights = rights;
            ft[i].mapped = 0;
            ft[i].mem = calloc(frames ? frames : 1, XLB_PAGE);   /* zeroed, as the contract requires */
            if (!ft[i].mem) return XLOAD_E_NOMEM;
            *token = (uint64_t)(i + 1);
            return 0;
        }
    }
    return XLOAD_E_NOMEM;
}

void *xlb_kernel_ptr(uint64_t token) { return ft[token - 1].mem; }

void xlb_release(uint64_t token) {
    FTok *f = &ft[token - 1];
    if (f->mapped) violations++;          /* contract: never release a mapped token */
    free(f->mem);
    f->mem = NULL;
    f->live = 0;
}

int xlb_map(XAspace *as, uint64_t vaddr, uint64_t token, uint32_t perms) {
    (void)as;
    int k = map_calls++;
    if (k == fail_map_at) return XLOAD_E_MAP;
    FTok *f = &ft[token - 1];
    if (!f->live) violations++;
    if ((perms & XLB_PERM_W) && !(f->rights & XTOK_RIGHT_WRITE))   violations++;
    if ((perms & XLB_PERM_X) && !(f->rights & XTOK_RIGHT_EXECUTE)) violations++;
    for (int i = 0; i < FM; i++) {
        if (!fm[i].live) {
            fm[i].live = 1; fm[i].va = vaddr; fm[i].tok = token; fm[i].perms = perms;
            f->mapped++;
            return 0;
        }
    }
    return XLOAD_E_MAP;
}

int xlb_unmap(XAspace *as, uint64_t vaddr) {
    (void)as;
    for (int i = 0; i < FM; i++) {
        if (fm[i].live && fm[i].va == vaddr) {
            fm[i].live = 0;
            ft[fm[i].tok - 1].mapped--;
            return 0;
        }
    }
    return -1;
}

/* ---- helpers ---- */
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1);
    if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(2);
    fclose(f); *n = (size_t)l; return b;
}

static char dir[512];
static uint8_t *load_file(const char *name, size_t *n) {
    char p[600]; snprintf(p, sizeof p, "%s/%s.exb", dir, name);
    return slurp(p, n);
}

static uint64_t perms_at(uint64_t va, int *found) {
    for (int i = 0; i < FM; i++) if (fm[i].live && fm[i].va == va) { *found = 1; return fm[i].perms; }
    *found = 0; return 0;
}

/* ---- tests ---- */
static void test_valid_full(void) {
    reset();
    size_t n; uint8_t *img = load_file("valid_full", &n);
    XAspace as = { 1 };
    XLoadResult r;
    int rc = xload_exb(&as, 1, img, n, &r);
    CHECK(rc == XLOAD_OK, "valid_full loads (got %d)", rc);
    CHECK(r.entry == 0x8000000000ULL, "entry is the image entry");
    CHECK(r.stack_top == XLB_USER_TOP - XLB_PAGE, "stack top sits below the guard page");
    CHECK(r.stack_bytes == 8192, "stack size comes from the manifest (8192)");
    CHECK(r.stack_top % 16 == 0, "stack top is 16-byte aligned");
    CHECK(live_tokens() == 3, "two segments and one stack = 3 tokens (got %d)", live_tokens());
    CHECK(live_maps() == 3, "three mappings (got %d)", live_maps());
    CHECK(live_frames() == 1 + 2 + 2, "frames: 1 code + 2 data + 2 stack (got %d)", live_frames());

    int f; uint64_t p;
    p = perms_at(0x8000000000ULL, &f);
    CHECK(f && p == (XLB_PERM_R | XLB_PERM_X), "code is R+X, never W");
    p = perms_at(0x8000001000ULL, &f);
    CHECK(f && p == (XLB_PERM_R | XLB_PERM_W), "data is R+W, never X");
    p = perms_at(r.stack_top - 8192, &f);
    CHECK(f && p == (XLB_PERM_R | XLB_PERM_W), "stack is R+W");

    /* contents: code bytes are the 15 nops and ret from base(); data is 'data-seg' */
    uint8_t *code = ft[0].mem;
    CHECK(code[0] == 0x90 && code[14] == 0x90 && code[15] == 0xC3, "code bytes copied from the image");
    uint8_t *data = ft[1].mem;
    CHECK(memcmp(data, "data-seg", 8) == 0, "file bytes copied into the data segment");
    int zero = 1; for (size_t i = 8; i < 2 * XLB_PAGE; i++) if (data[i]) zero = 0;
    CHECK(zero, "the .bss part of the data segment is zero");
    CHECK(violations == 0, "no backend contract breach");
    free(img);
}

static void test_default_stack(void) {
    reset();
    size_t n; uint8_t *img = load_file("valid_nocaps", &n);
    XAspace as = { 2 }; XLoadResult r;
    CHECK(xload_exb(&as, 1, img, n, &r) == XLOAD_OK, "valid_nocaps loads");
    CHECK(r.stack_bytes == 4 * XLB_PAGE, "no manifest: default stack of 4 pages");
    free(img);
}

static void test_bss_segment(void) {
    reset();
    size_t n; uint8_t *img = load_file("valid_bss_only_segment", &n);
    XAspace as = { 3 }; XLoadResult r;
    CHECK(xload_exb(&as, 1, img, n, &r) == XLOAD_OK, "bss-only segment loads");
    CHECK(live_frames() == 1 + 64 + 4, "64 bss pages + code + default stack (got %d)", live_frames());
    free(img);
}

static void test_corpus_rejections(void) {
    char p[600]; snprintf(p, sizeof p, "%s/manifest.txt", dir);
    FILE *m = fopen(p, "r");
    if (!m) { perror(p); exit(2); }
    char name[128], exp[64]; int checked = 0;
    while (fscanf(m, "%127s %63s", name, exp) == 2) {
        if (strcmp(exp, "OK") == 0) continue;
        reset();
        size_t n; uint8_t *img = load_file(name, &n);
        XAspace as = { 4 }; XLoadResult r;
        int rc = xload_exb(&as, 1, img, n, &r);
        /* a zero-byte file is an argument error, caught before parsing */
        int want = (n == 0) ? XLOAD_E_ARG : XLOAD_E_IMAGE;
        if (rc != want || live_tokens() != 0 || live_maps() != 0 || alloc_calls != 0) {
            CHECK(0, "%s: expected %d with nothing touched (rc=%d tokens=%d allocs=%d)",
                  name, want, rc, live_tokens(), alloc_calls);
        } else passes++;
        checked++;
        free(img);
    }
    fclose(m);
    printf("      corpus rejections checked: %d\n", checked);
}

static void test_args_and_quota(void) {
    reset();
    size_t n; uint8_t *img = load_file("valid_full", &n);
    XAspace as = { 5 }; XLoadResult r;
    CHECK(xload_exb(NULL, 1, img, n, &r) == XLOAD_E_ARG, "null address space");
    CHECK(xload_exb(&as, 1, NULL, n, &r) == XLOAD_E_ARG, "null image");
    CHECK(xload_exb(&as, 1, img, 0, &r) == XLOAD_E_ARG, "zero size");
    CHECK(xload_exb(&as, 0, img, n, &r) == XLOAD_E_ARG, "owner 0 is the kernel, refused");
    CHECK(alloc_calls == 0 && live_tokens() == 0, "bad arguments touch nothing");

    quota_ok_answer = 0;
    CHECK(xload_exb(&as, 1, img, n, &r) == XLOAD_E_QUOTA, "quota denied");
    CHECK(alloc_calls == 0, "quota denied before any frame is requested");
    free(img);
}

/* Fail the k-th allocation, for every k: the load must fail cleanly each time. */
static void test_alloc_failures(void) {
    size_t n; uint8_t *img = load_file("valid_full", &n);
    int total = 3;   /* two segments + stack */
    for (int k = 0; k < total; k++) {
        reset();
        fail_alloc_at = k;
        XAspace as = { 6 }; XLoadResult r;
        int rc = xload_exb(&as, 1, img, n, &r);
        CHECK(rc == XLOAD_E_NOMEM, "alloc #%d fails -> NOMEM (got %d)", k, rc);
        CHECK(live_tokens() == 0, "alloc #%d fails -> no token left alive (got %d)", k, live_tokens());
        CHECK(live_maps() == 0, "alloc #%d fails -> no mapping left (got %d)", k, live_maps());
        CHECK(live_frames() == 0, "alloc #%d fails -> no frame left (got %d)", k, live_frames());
        CHECK(violations == 0, "alloc #%d fails -> no contract breach", k);
    }
    free(img);
}

/* Fail the k-th mapping, for every k: the load must undo everything before it. */
static void test_map_failures(void) {
    size_t n; uint8_t *img = load_file("valid_full", &n);
    int total = 3;
    for (int k = 0; k < total; k++) {
        reset();
        fail_map_at = k;
        XAspace as = { 7 }; XLoadResult r;
        int rc = xload_exb(&as, 1, img, n, &r);
        CHECK(rc == XLOAD_E_MAP, "map #%d fails -> MAP (got %d)", k, rc);
        CHECK(live_tokens() == 0 && live_maps() == 0 && live_frames() == 0,
              "map #%d fails -> everything undone (tokens=%d maps=%d frames=%d)",
              k, live_tokens(), live_maps(), live_frames());
        CHECK(violations == 0, "map #%d fails -> no contract breach", k);
    }
    free(img);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: load_test CORPUS_DIR\n"); return 2; }
    snprintf(dir, sizeof dir, "%s", argv[1]);

    test_valid_full();
    test_default_stack();
    test_bss_segment();
    test_corpus_rejections();
    test_args_and_quota();
    test_alloc_failures();
    test_map_failures();

    printf("xload: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
