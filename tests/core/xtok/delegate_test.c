/* Delegation chain: shell -> program. The shell holds a block token with GIVE,
 * gives it to a fresh program owner, and the program derives from it. The
 * shell loses access on give. The program cannot widen what it received, and
 * a third owner with no token gets nothing. */
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "derive_call.h"
#include "owner.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define RG (XTOK_RIGHT_READ | XTOK_RIGHT_GIVE | XTOK_RIGHT_RELEASE)
static BlkDev dev;

static void test_delegated_token_derives_within_bounds(void) {
    UINT32 shell = owner_new(), prog = owner_new();
    XToken t = xtok_alloc_blocks(shell, &dev, 0, 64, RG);
    CHECK(t != XTOKEN_INVALID);
    CHECK(xtok_give(shell, t, prog) == 0);
    XToken c = XTOKEN_INVALID;
    CHECK(xsys_derive(prog, t, 8, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, &c) == XDERIVE_OK);
    XToken d = XTOKEN_INVALID;   /* refusals overwrite their out: keep c intact */
    CHECK(xsys_derive(prog, t, 60, 8, XTOK_RIGHT_READ, &d) == XDERIVE_REFUSED); /* past end */
    CHECK(xsys_derive(prog, t, 0, 8, XTOK_RIGHT_WRITE, &d) == XDERIVE_REFUSED); /* not held */
    CHECK(xtok_release_owned(c, prog) == 0);
    CHECK(xtok_release_owned(t, prog) == 0);
    CHECK(owner_retire(shell) == 0);
    CHECK(owner_retire(prog) == 0);
}

static void test_giver_loses_access(void) {
    UINT32 shell = owner_new(), prog = owner_new();
    XToken t = xtok_alloc_blocks(shell, &dev, 0, 64, RG);
    CHECK(xtok_give(shell, t, prog) == 0);
    XToken c = XTOKEN_INVALID;
    CHECK(xsys_derive(shell, t, 0, 8, XTOK_RIGHT_READ, &c) == XDERIVE_REFUSED);
    CHECK(xtok_give(shell, t, owner_new()) == -1);
    CHECK(xtok_release_owned(t, shell) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(t, prog) == 0);
    CHECK(owner_retire(shell) == 0);
    CHECK(owner_retire(prog) == 0);
}

static void test_program_without_token_gets_nothing(void) {
    UINT32 shell = owner_new(), prog = owner_new(), other = owner_new();
    XToken t = xtok_alloc_blocks(shell, &dev, 0, 64, RG);
    CHECK(xtok_give(shell, t, prog) == 0);
    XToken c = XTOKEN_INVALID;
    CHECK(xsys_derive(other, t, 0, 8, XTOK_RIGHT_READ, &c) == XDERIVE_REFUSED);
    CHECK(c == XTOKEN_INVALID);
    CHECK(xtok_release_owned(t, prog) == 0);
    CHECK(owner_retire(shell) == 0);
    CHECK(owner_retire(prog) == 0);
    CHECK(owner_retire(other) == 0);
}

static void test_delegation_without_give_refused(void) {
    UINT32 shell = owner_new(), prog = owner_new();
    XToken t = xtok_alloc_blocks(shell, &dev, 0, 64, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(xtok_give(shell, t, prog) == -1);
    CHECK(xtok_release_owned(t, shell) == 0);
    CHECK(owner_retire(shell) == 0);
    CHECK(owner_retire(prog) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    test_delegated_token_derives_within_bounds();
    test_giver_loses_access();
    test_program_without_token_gets_nothing();
    test_delegation_without_give_refused();
    printf("delegate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
