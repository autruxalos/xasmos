/******************************************************************************
 * Regression harness: token/map syscalls, quotas, ownership, reaping.
 * A ring-3 interpreter runs scripts of syscalls; the kernel inspects results.
 * Path: tests/xeret/proc/syscall_test_main.c
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "paging.h"
#include "xproc.h"
#include "diag.h"

extern void fb_console_init(FramebufferInfo *fb);

#define UVA_CODE_N    0x8000000000
#define UVA_SCRIPT_N  0x8000001000
#define UVA_RESULT_N  0x8000002000
#define UVA_STACK_N   0x8000003000
#define S2(x) #x
#define S(x)  S2(x)
#define UVA_CODE   ((UINT64)UVA_CODE_N)
#define UVA_SCRIPT ((UINT64)UVA_SCRIPT_N)
#define UVA_RESULT ((UINT64)UVA_RESULT_N)
#define UVA_STACK  ((UINT64)UVA_STACK_N)
#define VA1        0x8000100000ULL
#define VA_NEVER   0x8000900000ULL

/* script ops: 4..8 are real syscalls; 100..102 run inline in ring 3 */
#define OP_ALLOC 4
#define OP_MAP   5
#define OP_UNMAP 6
#define OP_FREE  7
#define OP_INFO  8
#define OP_STORE 100
#define OP_LOAD  101
#define OP_CALL  102
#define REF(k)   ((1ULL << 63) | (UINT64)(k))

/* Entry = 32 bytes {op,a1,a2,a3}. Result = 16 bytes {rax,rdx}. A1 with bit 63
 * set means "the rdx saved by script entry k" (a token from an earlier call). */
__asm__(
    ".text\n"
    ".globl ux_interp\nux_interp:\n"
    "  movabsq $" S(UVA_SCRIPT_N) ", %r14\n"
    "  movabsq $" S(UVA_RESULT_N) ", %r12\n"
    "  xorl %r13d, %r13d\n"
    "ux_loop:\n"
    "  movq %r13, %rcx\n  shlq $5, %rcx\n"
    "  movq (%r14,%rcx), %rax\n"
    "  testq %rax, %rax\n  jz ux_done\n"
    "  movq 8(%r14,%rcx), %rdi\n"
    "  movq 16(%r14,%rcx), %rsi\n"
    "  movq 24(%r14,%rcx), %rdx\n"
    "  btq $63, %rdi\n  jnc ux_noref\n"
    "  andq $0xFF, %rdi\n  shlq $4, %rdi\n  movq 8(%r12,%rdi), %rdi\n"
    "ux_noref:\n"
    "  cmpq $100, %rax\n  je ux_store\n"
    "  cmpq $101, %rax\n  je ux_load\n"
    "  cmpq $102, %rax\n  je ux_call\n"
    "  int $0x80\n  jmp ux_save\n"
    "ux_store:\n  movq %rsi, (%rdi)\n  xorl %eax, %eax\n  xorl %edx, %edx\n  jmp ux_save\n"
    "ux_load:\n  movq (%rdi), %rax\n  xorl %edx, %edx\n  jmp ux_save\n"
    "ux_call:\n  call *%rdi\n  xorl %eax, %eax\n  xorl %edx, %edx\n"
    "ux_save:\n"
    "  movq %r13, %rcx\n  shlq $4, %rcx\n"
    "  movq %rax, (%r12,%rcx)\n  movq %rdx, 8(%r12,%rcx)\n"
    "  incq %r13\n  jmp ux_loop\n"
    "ux_done:\n"
    "  movq %r13, %rdi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl ux_interp_end\nux_interp_end:\n"
);
extern char ux_interp[], ux_interp_end[];

static int fails;
static void check(const char *name, int ok) {
    serial_puts(ok ? "PASS  " : "FAIL  ");
    serial_puts(name);
    serial_puts("\n");
    if (!ok) fails++;
}

#define RW_R  (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define RX_R  (XTOK_RIGHT_READ | XTOK_RIGHT_EXECUTE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define R_ONLY (XTOK_RIGHT_READ | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define RWX_R (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_EXECUTE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)

typedef struct {
    XAspace  as;
    XContext ctx;
    XToken   tCode, tScript, tResult, tStack;
    UINT64   pScript, pResult;
    int      n;
} Sp;

static XProcResult R;
static UINT64 F0;

static UINT64 rd64(UINT64 phys) { return *(volatile UINT64 *)(UINTN)phys; }

/* An owner with the interpreter in an R+X page and script/result/stack pages. */
static int sp_new(Sp *p, UINT32 owner) {
    p->n = 0;
    if (xas_create(&p->as, owner) != XAS_OK) return -1;
    p->tCode   = xtok_alloc_ram(owner, 1, RX_R);
    p->tScript = xtok_alloc_ram(owner, 1, RW_R);
    p->tResult = xtok_alloc_ram(owner, 1, RW_R);
    p->tStack  = xtok_alloc_ram(owner, 1, RW_R);
    UINT64 pc = 0, ps = 0;
    xtok_ram_info(p->tCode, &pc, 0);
    xtok_ram_info(p->tScript, &p->pScript, 0);
    xtok_ram_info(p->tResult, &p->pResult, 0);
    xtok_ram_info(p->tStack, &ps, 0);
    if (xas_map(&p->as, UVA_CODE,   p->tCode,   XPERM_R | XPERM_X) != XAS_OK) return -1;
    if (xas_map(&p->as, UVA_SCRIPT, p->tScript, XPERM_R | XPERM_W) != XAS_OK) return -1;
    if (xas_map(&p->as, UVA_RESULT, p->tResult, XPERM_R | XPERM_W) != XAS_OK) return -1;
    if (xas_map(&p->as, UVA_STACK,  p->tStack,  XPERM_R | XPERM_W) != XAS_OK) return -1;
    volatile UINT8 *d = (volatile UINT8 *)(UINTN)pc;
    for (int i = 0; ux_interp + i < ux_interp_end; i++) d[i] = (UINT8)ux_interp[i];
    xctx_init(&p->ctx, UVA_CODE, UVA_STACK + 4096);
    return 0;
}

static void sc(Sp *p, UINT64 op, UINT64 a1, UINT64 a2, UINT64 a3) {
    volatile UINT64 *e = (volatile UINT64 *)(UINTN)(p->pScript + 32ULL * (UINT64)p->n++);
    e[0] = op; e[1] = a1; e[2] = a2; e[3] = a3;
}
static long long rax_of(Sp *p, int i) { return *(volatile long long *)(UINTN)(p->pResult + 16ULL * (UINT64)i); }
static UINT64    rdx_of(Sp *p, int i) { return *(volatile UINT64 *)(UINTN)(p->pResult + 16ULL * (UINT64)i + 8); }
static int sp_run(Sp *p) { return xproc_run_ctx(&p->as, &p->ctx, &R); }

static int all_clean(void) {
    return phys_free_count() == F0 && xtok_live_count() == 0 && xas_live_mappings() == 0;
}

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    if (phys_init_from_handoff(hand) != 0) { serial_puts("FAIL  phys_init\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    xtok_init();
    UINT64 fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL * hand->fb.height;
    check("xpt_init_identity", xpt_init_identity(hand->phys_end, hand->fb.base, fb_bytes) == 0);
    xproc_init();
    F0 = phys_free_count();

    Sp p;

    /* ===== 1. a program allocates, maps, uses and inspects its own memory ===== */
    check("[1] create process (owner 1)", sp_new(&p, 1) == 0);
    sc(&p, OP_ALLOC, 2, RW_R, 0);                    /* 0 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);/* 1 */
    sc(&p, OP_LOAD,  VA1, 0, 0);                     /* 2: fresh memory reads zero */
    sc(&p, OP_STORE, VA1, 0xAB12, 0);                /* 3 */
    sc(&p, OP_LOAD,  VA1, 0, 0);                     /* 4 */
    sc(&p, OP_STORE, VA1 + 0x1000, 0x77, 0);         /* 5: second frame of the token */
    sc(&p, OP_INFO,  XINFO_FRAMES_USED, 0, 0);       /* 6 */
    sc(&p, OP_INFO,  XINFO_TOKENS_USED, 0, 0);       /* 7 */
    check("[1] script runs to its exit", sp_run(&p) == XPROC_EXIT && R.exit_code == 8);
    check("[1] ALLOC succeeded and returned a token", rax_of(&p, 0) == 0 && rdx_of(&p, 0) != 0);
    check("[1] MAP succeeded", rax_of(&p, 1) == 0);
    check("[1] fresh memory reads as zero (no leak from earlier owners)", rax_of(&p, 2) == 0);
    check("[1] the program reads back what it stored", rax_of(&p, 4) == 0xAB12);
    check("[1] INFO: frames used = 4 infrastructure + 2", rax_of(&p, 6) == 6);
    check("[1] INFO: tokens used = 4 infrastructure + 1", rax_of(&p, 7) == 5);
    {
        UINT64 ph = 0;
        check("[1] the kernel can find the frames behind that token", xtok_ram_info(rdx_of(&p, 0), &ph, 0) == 0);
        check("[1] the ring-3 stores landed in the token's physical frames",
              rd64(ph) == 0xAB12 && rd64(ph + 0x1000) == 0x77);
    }
    check("[1] reap reclaims all 5 tokens (it never freed the 2-frame one)", xproc_reap(&p.as) == 5);
    check("[1] nothing leaked: frames, tokens, mappings", all_clean());

    /* ===== 2. full lifecycle: free after unmap, double free ===== */
    check("[2] create", sp_new(&p, 1) == 0);
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 0 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);/* 1 */
    sc(&p, OP_FREE,  REF(0), 0, 0);                  /* 2: still mapped */
    sc(&p, OP_UNMAP, VA1, 0, 0);                     /* 3 */
    sc(&p, OP_FREE,  REF(0), 0, 0);                  /* 4 */
    sc(&p, OP_FREE,  REF(0), 0, 0);                  /* 5: double free */
    sc(&p, OP_INFO,  XINFO_TOKENS_USED, 0, 0);       /* 6 */
    check("[2] runs", sp_run(&p) == XPROC_EXIT);
    check("[2] freeing a mapped token is refused (BUSY)", rax_of(&p, 2) == XERR_BUSY);
    check("[2] unmap, then free, succeed", rax_of(&p, 3) == 0 && rax_of(&p, 4) == 0);
    check("[2] a second free of the same token is BADTOKEN", rax_of(&p, 5) == XERR_BADTOKEN);
    check("[2] the freed token no longer counts against the owner", rax_of(&p, 6) == 4);
    xproc_reap(&p.as);
    check("[2] clean", all_clean());

    /* ===== 3. rights: a read-only token cannot be made writable ===== */
    check("[3] create", sp_new(&p, 1) == 0);
    sc(&p, OP_ALLOC, 1, R_ONLY, 0);                  /* 0 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);/* 1: asks for more than the token has */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R);          /* 2 */
    sc(&p, OP_LOAD,  VA1, 0, 0);                     /* 3 */
    sc(&p, OP_STORE, VA1, 1, 0);                     /* 4: hardware refuses */
    check("[3] runs until the illegal store", sp_run(&p) == XPROC_FAULT);
    check("[3] MAP with more than the token allows is refused (PERM)", rax_of(&p, 1) == XERR_PERM);
    check("[3] the read-only MAP and a load succeed", rax_of(&p, 2) == 0 && rax_of(&p, 3) == 0);
    check("[3] the store faulted: #PF present+write+user on that page",
          R.vector == 14 && R.error == 7 && R.cr2 == VA1);
    check("[3] reap after a fault reclaims everything", xproc_reap(&p.as) == 5 && all_clean());

    /* ===== 4. W^X and JIT: write code, then re-map it executable ===== */
    check("[4] create", sp_new(&p, 1) == 0);
    sc(&p, OP_ALLOC, 1, RWX_R, 0);                   /* 0 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W | XPERM_X); /* 1: W^X */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);/* 2 */
    sc(&p, OP_STORE, VA1, 0xC3, 0);                  /* 3: 0xC3 = ret */
    sc(&p, OP_UNMAP, VA1, 0, 0);                     /* 4 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_X);/* 5 */
    sc(&p, OP_CALL,  VA1, 0, 0);                     /* 6: run the code it just wrote */
    check("[4] runs to the end, no fault", sp_run(&p) == XPROC_EXIT && R.exit_code == 7);
    check("[4] write+execute in one mapping is refused", rax_of(&p, 1) == XERR_PERM);
    check("[4] write, unmap, re-map executable: all succeed", rax_of(&p, 2) == 0 && rax_of(&p, 4) == 0 && rax_of(&p, 5) == 0);
    check("[4] the program executed the code it wrote and returned", rax_of(&p, 6) == 0);
    xproc_reap(&p.as);
    check("[4] clean", all_clean());

    check("[4b] create", sp_new(&p, 1) == 0);
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 0: no EXECUTE right */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_X);/* 1 */
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);/* 2 */
    sc(&p, OP_CALL,  VA1, 0, 0);                     /* 3: jump into data */
    check("[4b] jumping into a data page faults", sp_run(&p) == XPROC_FAULT);
    check("[4b] a token without EXECUTE cannot be mapped executable", rax_of(&p, 1) == XERR_PERM);
    check("[4b] #PF instruction fetch on an NX page (error 0x15)", R.vector == 14 && R.error == 0x15 && R.cr2 == VA1);
    xproc_reap(&p.as);
    check("[4b] clean", all_clean());

    /* ===== 5. ownership: another owner's token and an invented one look the same ===== */
    Sp A, B;
    check("[5] create owner 1 and owner 2", sp_new(&A, 1) == 0 && sp_new(&B, 2) == 0);
    sc(&A, OP_ALLOC, 1, RW_R, 0);
    check("[5] owner 1 allocates", sp_run(&A) == XPROC_EXIT && rax_of(&A, 0) == 0);
    UINT64 tA = rdx_of(&A, 0);
    UINT64 garbage = (3000ULL << 32) | 1ULL;         /* a slot nobody holds */
    sc(&B, OP_MAP,  tA,      VA1, XPERM_R | XPERM_W);/* 0 */
    sc(&B, OP_FREE, tA,      0, 0);                  /* 1 */
    sc(&B, OP_MAP,  garbage, VA1, XPERM_R | XPERM_W);/* 2 */
    sc(&B, OP_FREE, garbage, 0, 0);                  /* 3 */
    sc(&B, OP_FREE, 0,       0, 0);                  /* 4 */
    check("[5] owner 2 runs", sp_run(&B) == XPROC_EXIT);
    check("[5] MAP of someone else's token: BADTOKEN", rax_of(&B, 0) == XERR_BADTOKEN);
    check("[5] FREE of someone else's token: BADTOKEN", rax_of(&B, 1) == XERR_BADTOKEN);
    check("[5] an invented token gives the SAME answers (no probing oracle)",
          rax_of(&B, 2) == rax_of(&B, 0) && rax_of(&B, 3) == rax_of(&B, 1));
    check("[5] the null token too", rax_of(&B, 4) == XERR_BADTOKEN);
    check("[5] owner 1's token is untouched: live and not mapped by owner 2", xtok_map_count(tA) == 0);
    check("[5] reaping owner 2 does not touch owner 1", xproc_reap(&B.as) == 4);
    {
        UINT64 f = 0; UINT32 t = 0;
        xtok_owner_usage(1, &f, &t);
        check("[5] owner 1 still holds its 5 tokens / 5 frames", f == 5 && t == 5);
    }
    xproc_reap(&A.as);
    check("[5] clean", all_clean());

    /* ===== 6. quotas: frames and tokens are capped per owner ===== */
    check("[6] quota set for owner 3: 10 spare frames, 4 spare tokens", xproc_set_quota(3, 4 + 10, 4 + 4) == 0);
    check("[6] create", sp_new(&p, 3) == 0);
    sc(&p, OP_ALLOC, 6, RW_R, 0);                    /* 0 ok */
    sc(&p, OP_ALLOC, 6, RW_R, 0);                    /* 1 frames would exceed */
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 2 */
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 3 */
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 4 */
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 5 tokens would exceed */
    sc(&p, OP_INFO,  XINFO_FRAMES_USED, 0, 0);       /* 6 */
    sc(&p, OP_INFO,  XINFO_FRAMES_LIMIT, 0, 0);      /* 7 */
    sc(&p, OP_INFO,  XINFO_TOKENS_USED, 0, 0);       /* 8 */
    sc(&p, OP_INFO,  XINFO_TOKENS_LIMIT, 0, 0);      /* 9 */
    sc(&p, OP_FREE,  REF(0), 0, 0);                  /* 10 */
    sc(&p, OP_ALLOC, 2, RW_R, 0);                    /* 11 room again */
    check("[6] runs", sp_run(&p) == XPROC_EXIT);
    check("[6] first allocation fits", rax_of(&p, 0) == 0);
    check("[6] a second 6-frame allocation breaks the frame quota (NOMEM)", rax_of(&p, 1) == XERR_NOMEM);
    check("[6] three 1-frame allocations fit", rax_of(&p, 2) == 0 && rax_of(&p, 3) == 0 && rax_of(&p, 4) == 0);
    check("[6] the next one breaks the TOKEN quota (NOMEM)", rax_of(&p, 5) == XERR_NOMEM);
    check("[6] INFO reports 13/14 frames and 8/8 tokens",
          rax_of(&p, 6) == 13 && rax_of(&p, 7) == 14 && rax_of(&p, 8) == 8 && rax_of(&p, 9) == 8);
    check("[6] freeing makes room again", rax_of(&p, 10) == 0 && rax_of(&p, 11) == 0);
    xproc_reap(&p.as);
    check("[6] clean", all_clean());

    /* ===== 7. hostile arguments ===== */
    check("[7] create", sp_new(&p, 4) == 0);
    sc(&p, OP_ALLOC, 0, R_ONLY, 0);                  /* 0 */
    sc(&p, OP_ALLOC, 1, 0, 0);                       /* 1 */
    sc(&p, OP_ALLOC, 1, R_ONLY | XTOK_RIGHT_DMA, 0); /* 2 */
    sc(&p, OP_ALLOC, 1, XTOK_RIGHT_READ | (1ULL << 31), 0); /* 3 */
    sc(&p, OP_ALLOC, 0xFFFFFFFFFFFFULL, RW_R, 0);    /* 4 absurd size */
    sc(&p, OP_ALLOC, 1, RW_R, 0);                    /* 5 valid */
    sc(&p, OP_MAP,   REF(5), 0x200000, XPERM_R);     /* 6 kernel address */
    sc(&p, OP_MAP,   REF(5), VA1 + 8, XPERM_R);      /* 7 unaligned */
    sc(&p, OP_MAP,   REF(5), VA1, 0);                /* 8 no permissions */
    sc(&p, OP_MAP,   REF(5), VA1, 8);                /* 9 unknown bit */
    sc(&p, OP_MAP,   REF(5), VA1, 1ULL << 40);       /* 10 wide value */
    sc(&p, OP_UNMAP, VA1, 0, 0);                     /* 11 nothing there */
    sc(&p, OP_FREE,  0, 0, 0);                       /* 12 */
    sc(&p, OP_INFO,  99, 0, 0);                      /* 13 */
    sc(&p, 99,       0, 0, 0);                       /* 14 not a syscall */
    sc(&p, OP_MAP,   REF(5), XAS_USER_TOP, XPERM_R); /* 15 past the user range */
    sc(&p, OP_MAP,   REF(5), VA1, (1ULL << 32) | XPERM_R); /* 16 would truncate to plain R */
    check("[7] runs", sp_run(&p) == XPROC_EXIT);
    check("[7] zero frames: INVAL", rax_of(&p, 0) == XERR_INVAL);
    check("[7] empty rights: INVAL", rax_of(&p, 1) == XERR_INVAL);
    check("[7] asking for the DMA right: PERM", rax_of(&p, 2) == XERR_PERM);
    check("[7] unknown right bits: PERM", rax_of(&p, 3) == XERR_PERM);
    check("[7] absurd size: NOMEM, without overflow", rax_of(&p, 4) == XERR_NOMEM);
    check("[7] a valid allocation still works afterwards", rax_of(&p, 5) == 0);
    check("[7] mapping at a kernel address: RANGE", rax_of(&p, 6) == XERR_RANGE);
    check("[7] unaligned address: RANGE", rax_of(&p, 7) == XERR_RANGE);
    check("[7] no permissions requested: INVAL", rax_of(&p, 8) == XERR_INVAL);
    check("[7] unknown permission bit: INVAL", rax_of(&p, 9) == XERR_INVAL);
    check("[7] permission word wider than 32 bits: INVAL", rax_of(&p, 10) == XERR_INVAL);
    check("[7] unmapping an address that is not mapped: NOMAP", rax_of(&p, 11) == XERR_NOMAP);
    check("[7] freeing the null token: BADTOKEN", rax_of(&p, 12) == XERR_BADTOKEN);
    check("[7] unknown INFO selector: INVAL", rax_of(&p, 13) == XERR_INVAL);
    check("[7] unknown syscall number: NOSYS", rax_of(&p, 14) == XERR_NOSYS);
    check("[7] address at the end of user space: RANGE", rax_of(&p, 15) == XERR_RANGE);
    check("[7] a permission word whose low 32 bits look valid is still refused: INVAL (no silent truncation)", rax_of(&p, 16) == XERR_INVAL);
    xproc_reap(&p.as);
    check("[7] clean", all_clean());

    /* ===== 8. a program cannot exhaust the kernel's mapping records ===== */
    check("[8] create", sp_new(&p, 5) == 0);
    sc(&p, OP_ALLOC, 1, RW_R, 0);                                    /* 0 */
    for (int i = 0; i <= 60; i++)                                    /* 1..61 */
        sc(&p, OP_MAP, REF(0), VA1 + (UINT64)i * 0x1000, XPERM_R);
    sc(&p, OP_UNMAP, VA1, 0, 0);                                     /* 62 */
    sc(&p, OP_MAP,   REF(0), VA1 + 61ULL * 0x1000, XPERM_R);         /* 63 */
    check("[8] runs", sp_run(&p) == XPROC_EXIT);
    int okmaps = 0;
    for (int i = 1; i <= 60; i++) if (rax_of(&p, i) == 0) okmaps++;
    check("[8] 60 mappings fit (64 minus the 4 the process already has)", okmaps == 60);
    check("[8] the 61st is refused (NOMEM)", rax_of(&p, 61) == XERR_NOMEM);
    check("[8] the owner holds exactly the per-owner cap", xas_live_mappings() == XAS_MAX_MAPS_PER_OWNER);
    check("[8] after an unmap there is room again", rax_of(&p, 62) == 0 && rax_of(&p, 63) == 0);
    xproc_reap(&p.as);
    check("[8] reap releases every mapping record", all_clean());

    /* ===== 9. exit without cleaning up, and fault without cleaning up ===== */
    check("[9] create", sp_new(&p, 6) == 0);
    sc(&p, OP_ALLOC, 2, RW_R, 0);
    sc(&p, OP_ALLOC, 3, RW_R, 0);
    sc(&p, OP_ALLOC, 1, RW_R, 0);
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);
    sc(&p, OP_STORE, VA1, 5, 0);
    check("[9] program exits holding 3 allocations and a mapping", sp_run(&p) == XPROC_EXIT);
    check("[9] it holds 4+6 frames and 4+3 tokens before the reap",
          ({ UINT64 f = 0; UINT32 t = 0; xtok_owner_usage(6, &f, &t); f == 10 && t == 7; }));
    check("[9] reap returns all 7 tokens", xproc_reap(&p.as) == 7);
    check("[9] everything is back", all_clean());

    check("[9b] create", sp_new(&p, 7) == 0);
    sc(&p, OP_ALLOC, 2, RW_R, 0);
    sc(&p, OP_MAP,   REF(0), VA1, XPERM_R | XPERM_W);
    sc(&p, OP_STORE, VA1, 9, 0);
    sc(&p, OP_LOAD,  VA_NEVER, 0, 0);                /* touches nothing it owns */
    check("[9b] program faults holding memory", sp_run(&p) == XPROC_FAULT);
    check("[9b] #PF page not present (error 4)", R.vector == 14 && R.error == 4 && R.cr2 == VA_NEVER);
    check("[9b] reap after the fault reclaims it all", xproc_reap(&p.as) == 5 && all_clean());

    check("every frame is back after all of that", phys_free_count() == F0);
    serial_puts(fails ? "RESULT: FAILURES\n" : "RESULT: ALL PASS\n");
    serial_hex((unsigned long long)fails); serial_puts(" failing\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
