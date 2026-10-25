/******************************************************************************
 * XASMOS - resource syscalls (internal to xeret)
 * Path: source|arch|amd64|kernel|xeret|xsys.h
 ******************************************************************************/
#ifndef XASMOS_XSYS_H
#define XASMOS_XSYS_H

#include "xproc.h"
#include "xframe.h"

/* Handles XSYS_ALLOC .. XSYS_INFO for the program running in `as`. The owner
 * is always as->owner_id: it never comes from a register. Sets rax (and rdx
 * for ALLOC) in the saved frame; always returns. */
void xsys_resource(XAspace *as, ExcFrame *f);

#endif
