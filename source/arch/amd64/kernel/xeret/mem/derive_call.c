/******************************************************************************
 * XASMOS - the call behind XSYS_DERIVE
 * Path: source/arch/amd64/kernel/xeret/mem/derive_call.c
 *
 * The rules live in xtok_derive_blocks, under the ledger lock. This file only
 * fixes the shape of the answer: a token on success, and one refusal otherwise.
 ******************************************************************************/
#include "derive_call.h"

int xsys_derive(UINT32 owner, XToken parent, UINT64 first, UINT64 count,
                UINT32 rights, XToken *out) {
    XToken t;
    if (out) *out = XTOKEN_INVALID;
    t = xtok_derive_blocks(owner, parent, first, count, rights);
    if (t == XTOKEN_INVALID) return XDERIVE_REFUSED;
    if (out) *out = t;
    return XDERIVE_OK;
}
