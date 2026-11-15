/* Host test for the XSYS_DERIVE call. The caller presents a parent it holds and
 * asks for a relative subrange with rights. Every refusal is one code, no token
 * is written on refusal, and the child belongs to the caller. */
#include <stdint.h>
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "derive_call.h"
#include "owner.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define R   (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE)
#define RW  (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE)
#define RWG (RW | XTOK_RIGHT_GIVE)

static BlkDev dev;

/* A 64-block root on the device, held by owner o, with full rights and GIVE. */
static XToken root(UINT32 o) {
    return xtok_alloc_blocks(o, &dev, 0, 64, RWG);
}

static void drop(UINT32 o, XToken t) {
    if (t == XTOKEN_INVALID) return;
    int rc = xtok_release_owned(t, o);
    if (rc != 0) { checks++; failures++;
        fprintf(stderr, "FAIL drop: release of %llx by owner %u returned %d\n",
                (unsigned long long)t, o, rc); }
}

static void retire_or_report(UINT32 o) {
    UINT64 f = 0; UINT32 tk = 0;
    if (owner_retire(o) == 0) { checks++; return; }
    xtok_owner_usage(o, &f, &tk);
    checks++; failures++;
    fprintf(stderr, "FAIL retire owner %u: frames=%llu tokens=%u\n", o,
            (unsigned long long)f, tk);
}

static void test_child_is_relative_and_owned(void) {
    UINT32 a = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID;
    CHECK(xsys_derive(a, p, 8, 8, R, &c) == XDERIVE_OK);
    CHECK(c != XTOKEN_INVALID);
    void *dv = 0; UINT64 s = 99;
    CHECK(xtok_blk_range(c, &dv, &s) == 0 && dv == &dev && s == 8);  /* relative to the root */
    UINT64 f = 0; UINT32 tk = 0;
    xtok_owner_usage(a, &f, &tk);
    CHECK(tk == 2);                                       /* root and child, both the caller's */
    drop(a, c); drop(a, p);
    retire_or_report(a);
}

static void test_foreign_parent_refused(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID;
    CHECK(xsys_derive(b, p, 0, 8, R, &c) == XDERIVE_REFUSED);
    CHECK(c == XTOKEN_INVALID);                           /* nothing written on refusal */
    drop(a, p);
    retire_or_report(a);
    retire_or_report(b);
}

static void test_ram_parent_refused(void) {
    UINT32 a = owner_new();
    XToken r = xtok_alloc_ram(a, 1, XTOK_RIGHTS_RAM_DEFAULT), c = XTOKEN_INVALID;
    CHECK(xsys_derive(a, r, 0, 1, R, &c) == XDERIVE_REFUSED);
    CHECK(c == XTOKEN_INVALID);
    drop(a, r);
    retire_or_report(a);
}

static void test_unknown_or_released_refused(void) {
    UINT32 a = owner_new();
    XToken c = XTOKEN_INVALID;
    CHECK(xsys_derive(a, XTOKEN_INVALID, 0, 1, R, &c) == XDERIVE_REFUSED);
    XToken p = root(a);
    drop(a, p);                                           /* stale from now on */
    CHECK(xsys_derive(a, p, 0, 1, R, &c) == XDERIVE_REFUSED);
    CHECK(c == XTOKEN_INVALID);
    retire_or_report(a);
}

static void test_range_never_grows(void) {
    UINT32 a = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID;
    CHECK(xsys_derive(a, p, 0, 64, R, &c) == XDERIVE_OK);  /* the whole root is allowed */
    drop(a, c);
    CHECK(xsys_derive(a, p, 1, 64, R, &c) == XDERIVE_REFUSED);   /* runs past the end */
    CHECK(xsys_derive(a, p, 64, 1, R, &c) == XDERIVE_REFUSED);   /* starts past the end */
    CHECK(xsys_derive(a, p, 0, 65, R, &c) == XDERIVE_REFUSED);
    CHECK(xsys_derive(a, p, UINT64_MAX, 2, R, &c) == XDERIVE_REFUSED); /* no overflow */
    CHECK(xsys_derive(a, p, 0, 0, R, &c) == XDERIVE_REFUSED);    /* empty */
    drop(a, p);
    retire_or_report(a);
}

static void test_rights_never_grow(void) {
    UINT32 a = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID;
    CHECK(xsys_derive(a, p, 0, 8, XTOK_RIGHT_MAP, &c) == XDERIVE_REFUSED);   /* not held */
    CHECK(xsys_derive(a, p, 0, 8, XTOK_RIGHT_EXECUTE, &c) == XDERIVE_REFUSED);
    CHECK(xsys_derive(a, p, 0, 8, 0, &c) == XDERIVE_REFUSED);                /* no rights */
    CHECK(xsys_derive(a, p, 0, 8, XTOK_RIGHT_GIVE | XTOK_RIGHT_RELEASE, &c) == XDERIVE_OK); /* held: allowed */
    drop(a, c);
    drop(a, p);
    retire_or_report(a);
}

static void test_child_cannot_widen_its_own_child(void) {
    UINT32 a = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID, d = XTOKEN_INVALID, e = XTOKEN_INVALID;
    CHECK(xsys_derive(a, p, 8, 8, R, &c) == XDERIVE_OK);      /* read-only child */
    CHECK(xsys_derive(a, c, 0, 9, R, &d) == XDERIVE_REFUSED); /* wider than c */
    CHECK(xsys_derive(a, c, 0, 1, XTOK_RIGHT_WRITE, &d) == XDERIVE_REFUSED); /* write c lacks */
    CHECK(xsys_derive(a, c, 0, 8, R, &d) == XDERIVE_OK);      /* the same width is fine */
    void *dv = 0; UINT64 s = 99;
    CHECK(xtok_blk_range(d, &dv, &s) == 0 && s == 8);         /* still device block 8 */
    CHECK(xsys_derive(a, d, 2, 2, R, &e) == XDERIVE_OK);      /* depth works */
    CHECK(xtok_blk_range(e, &dv, &s) == 0 && s == 10);
    drop(a, e); drop(a, d); drop(a, c); drop(a, p);
    retire_or_report(a);
}

static void test_refusal_leaves_nothing(void) {
    UINT32 a = owner_new();
    XToken p = root(a), c = XTOKEN_INVALID;
    UINT64 f0 = 0, f1 = 0; UINT32 t0 = 0, t1 = 0;
    xtok_owner_usage(a, &f0, &t0);
    CHECK(xsys_derive(a, p, 60, 60, R, &c) == XDERIVE_REFUSED);
    CHECK(xsys_derive(a, p, 0, 8, XTOK_RIGHT_MAP, &c) == XDERIVE_REFUSED);
    xtok_owner_usage(a, &f1, &t1);
    CHECK(f0 == f1 && t0 == t1);                          /* no token, no frames */
    drop(a, p);
    retire_or_report(a);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    test_child_is_relative_and_owned();
    test_foreign_parent_refused();
    test_ram_parent_refused();
    test_unknown_or_released_refused();
    test_range_never_grows();
    test_rights_never_grow();
    test_child_cannot_widen_its_own_child();
    test_refusal_leaves_nothing();
    printf("derive_call: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
