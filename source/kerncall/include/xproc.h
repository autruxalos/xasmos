/******************************************************************************
 * XASMOS - Ring-3 execution of an owner's address space
 * Path: source|kerncall|include|xproc.h
 ******************************************************************************/
#ifndef XASMOS_XPROC_H
#define XASMOS_XPROC_H

#include "efi_types.h"
#include "paging.h"

/* int 0x80 convention: rax = number, rdi = argument; result in rax. */
#define XSYS_EXIT  1u    /* rdi = exit code; does not return        */
#define XSYS_LOG   2u    /* rdi = value; kernel records it, returns 0 */

#define XPROC_EXIT   0
#define XPROC_FAULT  1
#define XPROC_LOG_MAX 8

typedef struct {
    int    reason;               /* XPROC_EXIT or XPROC_FAULT */
    UINT64 exit_code;
    UINT64 vector, error, rip, cr2;   /* valid when reason == XPROC_FAULT */
    UINT64 log[XPROC_LOG_MAX];
    UINT32 log_n;
} XProcResult;

void gdt_init(void);
int  gdt_ready(void);

/* gdt_init + IDT reload with the new code selector + syscall gate + hook. */
void xproc_init(void);

/* Switches to as, drops to ring 3 at entry with the given stack, and returns
 * when the program exits or faults. The kernel survives either. */
int  xproc_run(XAspace *as, UINT64 entry, UINT64 user_rsp, XProcResult *res);

#endif
