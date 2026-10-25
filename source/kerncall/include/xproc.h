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

/* Resource syscalls. Arguments travel in rdi, rsi, rdx; nothing is ever a
 * pointer, so the kernel never dereferences user memory on their behalf.
 * Result: rax = status (>= 0 ok, negative = XERR_*).                       */
#define XSYS_ALLOC  4u    /* rdi=frames rsi=rights      -> rax=0, rdx=token   */
#define XSYS_MAP    5u    /* rdi=token rsi=virt rdx=perms (XPERM_*) -> rax    */
#define XSYS_UNMAP  6u    /* rdi=virt                   -> rax                */
#define XSYS_FREE   7u    /* rdi=token                  -> rax                */
#define XSYS_INFO   8u    /* rdi=XINFO_*                -> rax=value or XERR  */

#define XINFO_FRAMES_USED   0u
#define XINFO_FRAMES_LIMIT  1u
#define XINFO_TOKENS_USED   2u
#define XINFO_TOKENS_LIMIT  3u

/* Rights a process may ask for. DMA is deliberately absent: only the kernel
 * hands out device-facing memory. */
#define XSYS_USER_RIGHTS  (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_EXECUTE | \
                           XTOK_RIGHT_MAP  | XTOK_RIGHT_RELEASE)

/* Syscall status codes. A token that is not yours reports XERR_BADTOKEN,
 * identical to one that never existed. */
#define XERR_NOSYS     (-1)
#define XERR_INVAL     (-2)   /* malformed argument                           */
#define XERR_NOMEM     (-3)   /* out of frames / quota / mapping slots        */
#define XERR_BADTOKEN  (-4)   /* stale, invalid, or not yours                 */
#define XERR_PERM      (-5)   /* the token's rights do not allow it, or W^X   */
#define XERR_BUSY      (-6)   /* address in use, or token still mapped        */
#define XERR_RANGE     (-7)   /* address outside the user range / unaligned   */
#define XERR_NOMAP     (-8)   /* nothing mapped at that address               */

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

/* Per-owner limits on frames and tokens (owner < 64; others use the default
 * of 2048 frames / 256 tokens). Kernel-minted tokens count too. */
int    xproc_set_quota(UINT32 owner, UINT64 max_frames, UINT32 max_tokens);

/* Process teardown. Revokes every mapping of every token the owner holds,
 * reclaims those tokens (whether or not they carry RELEASE), and destroys
 * the address space. Returns the number of tokens reclaimed. Call it after
 * a program exits or faults; nothing it held survives. */
UINT32 xproc_reap(XAspace *as);

/* Interrupts nobody asked for (diagnostics). */
UINT64 xproc_unexpected_irqs(void);
UINT64 xproc_last_unexpected_vector(void);

#endif
