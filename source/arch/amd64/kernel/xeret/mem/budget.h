/******************************************************************************
 * XASMOS - memory budgets (interface)
 * Path: source/arch/amd64/kernel/xeret/mem/budget.h
 *
 * A budget is a capability over frames. It holds a balance. A RAM token is
 * paid from a budget and is its child; releasing it refunds that budget.
 * The kernel keeps no quota table: who may spend what is the holder's choice.
 ******************************************************************************/
#ifndef XASMOS_BUDGET_H
#define XASMOS_BUDGET_H

#include "capability.h"

int xtok_budget_mint(UINT32 owner, UINT64 frames, XToken *out);
int xtok_budget_derive(UINT32 owner, XToken parent, UINT64 frames, XToken *out);
int xtok_budget_balance(XToken bud, UINT32 owner, UINT64 *frames);
int xtok_alloc_ram_from(UINT32 owner, XToken bud, UINT64 frames, UINT32 rights, XToken *out);

#endif
