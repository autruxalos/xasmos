/******************************************************************************
 * XASMOS - the call behind XSYS_DERIVE (interface)
 * Path: source/arch/amd64/kernel/xeret/mem/derive_call.h
 *
 * Every refusal is the same code: a program learns nothing about a parent it
 * does not hold (foreign, unknown, RAM, or not wide enough).
 ******************************************************************************/
#ifndef XASMOS_DERIVE_CALL_H
#define XASMOS_DERIVE_CALL_H

#include "capability.h"

#define XDERIVE_OK       0
#define XDERIVE_REFUSED (-1)

int xsys_derive(UINT32 owner, XToken parent, UINT64 first, UINT64 count,
                UINT32 rights, XToken *out);

#endif
