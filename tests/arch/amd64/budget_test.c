/* budget_test: memory as a token. A budget is a capability over frames: it
 * holds a balance, derives sub-budgets by balance, and pays for RAM tokens.
 * Releasing RAM refunds the budget that paid for it. No table of quotas. */
#include <stdio.h>
#include "capability.h"
#include "owner.h"
#include "budget.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void b1_pays_and_refunds(void) {
    UINT32 a = owner_new();
    XToken bud = XTOKEN_INVALID, ram = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(a, 8, &bud) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 8);
    CHECK(xtok_alloc_ram_from(a, bud, 3, XTOK_RIGHTS_RAM_DEFAULT, &ram) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 5);
    CHECK(xtok_release_owned(ram, a) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 8);   /* refunded */
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
}

static void b2_refuses_without_balance(void) {
    UINT32 a = owner_new();
    XToken bud = XTOKEN_INVALID, ram = XTOKEN_INVALID, ok = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(a, 4, &bud) == 0);
    CHECK(xtok_alloc_ram_from(a, bud, 5, XTOK_RIGHTS_RAM_DEFAULT, &ram) != 0);
    CHECK(ram == XTOKEN_INVALID);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 4);   /* nothing taken */
    CHECK(xtok_alloc_ram_from(a, bud, 4, XTOK_RIGHTS_RAM_DEFAULT, &ok) == 0);
    CHECK(xtok_alloc_ram_from(a, bud, 1, XTOK_RIGHTS_RAM_DEFAULT, &ram) != 0);
    CHECK(xtok_release_owned(ok, a) == 0);
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
}

static void b3_derive_splits_balance(void) {
    UINT32 a = owner_new();
    XToken bud = XTOKEN_INVALID, sub = XTOKEN_INVALID, ram = XTOKEN_INVALID;
    UINT64 bp = 0, bs = 0;
    CHECK(xtok_budget_mint(a, 10, &bud) == 0);
    CHECK(xtok_budget_derive(a, bud, 6, &sub) == 0);
    CHECK(xtok_budget_balance(bud, a, &bp) == 0 && bp == 4);     /* parent gave 6 away */
    CHECK(xtok_budget_balance(sub, a, &bs) == 0 && bs == 6);
    CHECK(xtok_budget_derive(a, bud, 5, &ram) != 0);             /* more than remains */
    CHECK(xtok_alloc_ram_from(a, sub, 6, XTOK_RIGHTS_RAM_DEFAULT, &ram) == 0);
    CHECK(xtok_budget_balance(bud, a, &bp) == 0 && bp == 4);     /* child paid, not parent */
    CHECK(xtok_release_owned(ram, a) == 0);
    CHECK(xtok_budget_balance(sub, a, &bs) == 0 && bs == 6);     /* refund went to sub */
    CHECK(xtok_release_owned(sub, a) == 0);
    CHECK(xtok_budget_balance(bud, a, &bp) == 0 && bp == 10);    /* returned on release */
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
}

static void b4_foreign_owner_cannot_spend(void) {
    UINT32 a = owner_new(), o = owner_new();
    XToken bud = XTOKEN_INVALID, ram = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(a, 4, &bud) == 0);
    CHECK(xtok_alloc_ram_from(o, bud, 1, XTOK_RIGHTS_RAM_DEFAULT, &ram) != 0);
    CHECK(xtok_budget_balance(bud, o, &bal) != 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 4);
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(o) == 0);
}

/* Conservation: balances plus the RAM frames alive equal what was minted. */
static void b5_conservation(void) {
    UINT32 a = owner_new();
    XToken bud = XTOKEN_INVALID, r1 = XTOKEN_INVALID, r2 = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(a, 12, &bud) == 0);
    CHECK(xtok_alloc_ram_from(a, bud, 2, XTOK_RIGHTS_RAM_DEFAULT, &r1) == 0);
    CHECK(xtok_alloc_ram_from(a, bud, 3, XTOK_RIGHTS_RAM_DEFAULT, &r2) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal + 2 + 3 == 12);
    CHECK(xtok_release_owned(r1, a) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal + 3 == 12);
    CHECK(xtok_release_owned(r2, a) == 0);
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
}

int main(void) {
    xtok_init();
    owner_init();
    UINT32 base = xtok_live_count();
    b1_pays_and_refunds();
    b2_refuses_without_balance();
    b3_derive_splits_balance();
    b4_foreign_owner_cannot_spend();
    b5_conservation();
    CHECK(xtok_live_count() == base);
    printf("budget: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
