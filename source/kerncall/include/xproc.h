/******************************************************************************
 * XASMOS - Ring-3 execution of an owner's address space, with preemption
 * Path: source|kerncall|include|xproc.h
 ******************************************************************************/
#ifndef XASMOS_XPROC_H
#define XASMOS_XPROC_H

#include "efi_types.h"
#include "paging.h"

/* int 0x80 convention: rax = number, rdi = argument; result in rax. */
#define XSYS_EXIT   1u    /* rdi = exit code; does not return                    */
#define XSYS_LOG    2u    /* rdi = value; kernel records it, returns 0           */
#define XSYS_YIELD  3u    /* give the CPU back; resumes with rax = 0             */

#define XPROC_EXIT     0
#define XPROC_FAULT    1
#define XPROC_PREEMPT  2  /* quantum expired; context saved, can be resumed     */
#define XPROC_YIELD    3  /* voluntary yield;  context saved, can be resumed     */
#define XPROC_LOG_MAX  8

/* Everything needed to resume a ring-3 program. Field order is fixed: the
 * assembly in proc.c indexes it by byte offset (static-asserted there).
 * NOT saved yet: x87/SSE state, so two programs must not interleave while
 * both use XMM registers. */
typedef struct {
    UINT64 rax, rbx, rcx, rdx, rsi, rdi, rbp;
    UINT64 r8, r9, r10, r11, r12, r13, r14, r15;
    UINT64 rip, rsp, rflags;
} XContext;

typedef struct {
    int    reason;               /* XPROC_* */
    UINT64 exit_code;
    UINT64 vector, error, rip, cr2;   /* valid when reason == XPROC_FAULT (rip also on PREEMPT/YIELD) */
    UINT64 log[XPROC_LOG_MAX];
    UINT32 log_n;
} XProcResult;

void gdt_init(void);
int  gdt_ready(void);

/* gdt_init + IDT reload + syscall gate + hardware-interrupt gates + hooks. */
void xproc_init(void);

/* Fresh context: all registers zero, starting at entry with the given stack. */
void xctx_init(XContext *c, UINT64 entry, UINT64 user_rsp);

/* CPU quantum per run. 0 disables preemption (programs run until they exit,
 * yield or fault). Returns -1 if lapic_timer_init() has not succeeded. */
int  xproc_set_quantum_ms(UINT32 ms);

/* Runs ctx in as until it exits, faults, yields or is preempted. On PREEMPT
 * and YIELD the context is updated in place, so calling again resumes it. */
int  xproc_run_ctx(XAspace *as, XContext *ctx, XProcResult *res);

/* One-shot convenience wrapper: builds a fresh context and runs it once. */
int  xproc_run(XAspace *as, UINT64 entry, UINT64 user_rsp, XProcResult *res);

/* Interrupts nobody asked for (diagnostics). */
UINT64 xproc_unexpected_irqs(void);
UINT64 xproc_last_unexpected_vector(void);

#endif
