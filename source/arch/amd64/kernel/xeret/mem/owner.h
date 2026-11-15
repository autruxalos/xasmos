/******************************************************************************
 * XASMOS - per-instance owners (interface)
 * Path: source/arch/amd64/kernel/xeret/mem/owner.h
 *
 * One owner per program execution, assigned by the kernel. The owner is the
 * accounting and teardown handle for the tokens that execution holds. It names
 * no program and grants no authority: authority is the token itself.
 ******************************************************************************/
#ifndef XASMOS_OWNER_H
#define XASMOS_OWNER_H

#include "capability.h"

#define XOWNER_FIRST_INSTANCE 16u   /* below this: fixed owners (kernel, FIRST_APP, ...) */
#define XOWNER_MAX            64u   /* live instances at once */

void   owner_init(void);
UINT32 owner_new(void);             /* a fresh id, or 0 if none is free */
int    owner_retire(UINT32 id);     /* 0 if retired; -1 if unknown or still holding */
int    owner_is_live(UINT32 id);    /* 1 if the id is handed out now */

#endif
