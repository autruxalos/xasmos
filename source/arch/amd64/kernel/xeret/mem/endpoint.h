/******************************************************************************
 * XASMOS - endpoints: receiver-owned objects for transferring titularity
 * Path: source/arch/amd64/kernel/xeret/mem/endpoint.h
 *
 * An endpoint is an object owned by its receiver. The receiver holds RECV and
 * hands out SEND. A giver offers a token with GIVE through a SEND; the token
 * is frozen until the receiver accepts or the giver cancels.
 ******************************************************************************/
#ifndef XASMOS_ENDPOINT_H
#define XASMOS_ENDPOINT_H

#include "capability.h"

/* Creates an endpoint owned by recv_owner; ep receives its RECV token. */
int xep_create(UINT32 recv_owner, XToken *ep_recv);

/* Derives a SEND token for an endpoint the caller holds RECV on. */
int xep_send_cap(UINT32 recv_owner, XToken ep_recv, XToken *ep_send);

/* Destroys the endpoint; a pending offer is cancelled and its token released. */
int xep_destroy(UINT32 recv_owner, XToken ep_recv);

/* Offer: giver presents a GIVE token and a SEND. The token freezes. */
int xtok_offer(UINT32 giver, XToken tok, XToken ep_send);

/* Accept: receiver presents its RECV; the frozen token changes owner. */
int xtok_accept(UINT32 recv_owner, XToken ep_recv, XToken *out);

/* Cancel: giver withdraws its pending offer; the token is unfrozen. */
int xtok_cancel(UINT32 giver, XToken tok);

#endif
