/* Transfer by endpoint: offer and accept. The receiver owns the endpoint and
 * decides; the giver cannot push a token into an owner that has not opened one. */
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "owner.h"
#include "endpoint.h"
#include "derive_call.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define GIVEABLE (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)
static BlkDev dev;

static void test_offer_then_accept_moves_titularity(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE), got = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    CHECK(xtok_offer(giver, t, ep_send) == 0);
    CHECK(xtok_accept(recv, ep_recv, &got) == 0);
    CHECK(got == t);
    CHECK(xtok_release_owned(t, giver) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(t, recv) == 0);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_no_endpoint_no_transfer(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE);
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xtok_offer(giver, t, XTOKEN_INVALID) == -1);      /* no endpoint named */
    CHECK(xtok_offer(giver, t, t) == -1);                   /* not an endpoint */
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    XToken got = XTOKEN_INVALID;
    CHECK(xtok_accept(recv, ep_recv, &got) == -1);          /* nothing offered */
    CHECK(xtok_release_owned(t, giver) == 0);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_offer_needs_give_and_send(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    CHECK(xtok_offer(giver, t, ep_send) == -1);             /* no GIVE on the token */
    CHECK(xtok_release_owned(t, giver) == 0);
    XToken t2 = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE);
    CHECK(xtok_offer(giver, t2, ep_recv) == -1);            /* RECV is not a SEND */
    CHECK(xtok_release_owned(t2, giver) == 0);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_offered_token_is_frozen(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE), got = XTOKEN_INVALID;
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID, d = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    CHECK(xtok_offer(giver, t, ep_send) == 0);
    CHECK(xtok_release_owned(t, giver) == XTOK_REL_EBUSY);  /* frozen */
    CHECK(xsys_derive(giver, t, 0, 4, XTOK_RIGHT_READ, &d) == XDERIVE_REFUSED);
    CHECK(xtok_offer(giver, t, ep_send) == -1);             /* one offer at a time */
    CHECK(xtok_cancel(giver, t) == 0);                      /* giver withdraws */
    CHECK(xtok_accept(recv, ep_recv, &got) == -1);          /* nothing to accept */
    CHECK(xtok_release_owned(t, giver) == 0);               /* usable again */
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_send_cap_cannot_accept(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE), got = XTOKEN_INVALID;
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    CHECK(xtok_offer(giver, t, ep_send) == 0);
    CHECK(xtok_accept(giver, ep_send, &got) == -1);         /* sender cannot accept */
    CHECK(xtok_accept(recv, ep_recv, &got) == 0);
    CHECK(xtok_release_owned(got, recv) == 0);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_destroy_cancels_offer(void) {
    UINT32 giver = owner_new(), recv = owner_new();
    XToken t = xtok_alloc_blocks(giver, &dev, 0, 8, GIVEABLE);
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(xep_send_cap(recv, ep_recv, giver, &ep_send) == 0);
    CHECK(xtok_offer(giver, t, ep_send) == 0);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(xtok_release_owned(t, giver) == 0);               /* token back with giver */
    CHECK(xtok_release_owned(ep_send, giver) == 0);   /* a holder frees its SEND first */
    CHECK(owner_retire(giver) == 0);
    CHECK(owner_retire(recv) == 0);
}

static void test_owner_with_endpoint_cannot_retire(void) {
    UINT32 recv = owner_new();
    XToken ep_recv = XTOKEN_INVALID;
    CHECK(xep_create(recv, &ep_recv) == 0);
    CHECK(owner_retire(recv) == -1);
    CHECK(xep_destroy(recv, ep_recv) == 0);
    CHECK(owner_retire(recv) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    test_offer_then_accept_moves_titularity();
    test_no_endpoint_no_transfer();
    test_offer_needs_give_and_send();
    test_offered_token_is_frozen();
    test_send_cap_cannot_accept();
    test_destroy_cancels_offer();
    test_owner_with_endpoint_cannot_retire();
    printf("endpoint: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
