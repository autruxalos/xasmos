/******************************************************************************
 * XASMOS - endpoints: receiver-owned objects for transferring titularity
 * Path: source/arch/amd64/kernel/xeret/mem/endpoint.h
 *
 * The receiver creates an endpoint and holds RECV. It mints SEND for the holder
 * it trusts to offer to it. A giver offers a token with GIVE through a SEND; the
 * token is frozen until the receiver accepts or the giver cancels.
 ******************************************************************************/
#ifndef XASMOS_ENDPOINT_H
#define XASMOS_ENDPOINT_H

#include "capability.h"

int xep_create(UINT32 recv_owner, XToken *ep_recv);
int xep_send_cap(UINT32 recv_owner, XToken ep_recv, UINT32 holder, XToken *ep_send);
int xep_destroy(UINT32 recv_owner, XToken ep_recv);

int xtok_offer(UINT32 giver, XToken tok, XToken ep_send);
int xtok_accept(UINT32 recv_owner, XToken ep_recv, XToken *out);
int xtok_cancel(UINT32 giver, XToken tok);

/* Full transfer in one call: the receiver opens an endpoint, the giver sends
 * a SEND to it and offers, the receiver accepts. The only way titularity moves. */
int xep_transfer(UINT32 from, XToken t, UINT32 to, XToken *got);

#endif
