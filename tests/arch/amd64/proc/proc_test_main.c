/******************************************************************************
 * Regression harness: MAP/NX/W^X/ownership/revocation, enforced by hardware.
 * Real ring-3 programs try to break each rule; the kernel must survive all.
 * Path: tests/xeret/proc/proc_test_main.c
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "paging.h"
#include "xproc.h"
#include "diag.h"

extern void fb_console_init(FramebufferInfo *fb);

/* ---- user virtual layout (numeric, so the asm below can stringify them) -- */
#define UVA_CODE_N   0x8000000000
#define UVA_DATA_N   0x8000001000
#define UVA_RO_N     0x8000002000
#define UVA_STACK_N  0x8000003000
#define UVA_NONE_N   0x8000004000
#define UVA_MULTI_N  0x8000010000
#define KERNEL_VA_N  0x200000
#define S2(x) #x
#define S(x)  S2(x)

#define UVA_CODE  ((UINT64)UVA_CODE_N)
#define UVA_DATA  ((UINT64)UVA_DATA_N)
#define UVA_RO    ((UINT64)UVA_RO_N)
#define UVA_STACK ((UINT64)UVA_STACK_N)
#define UVA_NONE  ((UINT64)UVA_NONE_N)
#define UVA_MULTI ((UINT64)UVA_MULTI_N)

/* ---- the ring-3 programs (position-independent machine code) ------------- */
__asm__(
    ".text\n"
    ".globl ub_good\nub_good:\n"
    "  movabsq $" S(UVA_DATA_N) ", %rbx\n"
    "  movq $0x42, (%rbx)\n"
    "  movq (%rbx), %rdi\n"
    "  movl $2, %eax\n  int $0x80\n"          /* LOG(read-back value) */
    "  movq $7, %rdi\n  movl $1, %eax\n  int $0x80\n"   /* EXIT(7) */
    "  ud2\n"
    ".globl ub_good_end\nub_good_end:\n"

    ".globl ub_badsys\nub_badsys:\n"
    "  movl $99, %eax\n  int $0x80\n"
    "  movq %rax, %rdi\n  movl $1, %eax\n  int $0x80\n"  /* EXIT(rax) */
    "  ud2\n"
    ".globl ub_badsys_end\nub_badsys_end:\n"

    ".globl ub_write_ro\nub_write_ro:\n"
    "  movabsq $" S(UVA_RO_N) ", %rbx\n  movq $1, (%rbx)\n  ud2\n"
    ".globl ub_write_ro_end\nub_write_ro_end:\n"

    ".globl ub_exec_data\nub_exec_data:\n"
    "  movabsq $" S(UVA_DATA_N) ", %rax\n  jmp *%rax\n"
    ".globl ub_exec_data_end\nub_exec_data_end:\n"

    ".globl ub_read_kernel\nub_read_kernel:\n"
    "  movabsq $" S(KERNEL_VA_N) ", %rbx\n  movq (%rbx), %rax\n  ud2\n"
    ".globl ub_read_kernel_end\nub_read_kernel_end:\n"

    ".globl ub_read_none\nub_read_none:\n"
    "  movabsq $" S(UVA_NONE_N) ", %rbx\n  movq (%rbx), %rax\n  ud2\n"
    ".globl ub_read_none_end\nub_read_none_end:\n"

    ".globl ub_port\nub_port:\n"
    "  movw $0x3f8, %dx\n  movb $0x41, %al\n  outb %al, %dx\n  ud2\n"
    ".globl ub_port_end\nub_port_end:\n"

    ".globl ub_write_a\nub_write_a:\n"
    "  movabsq $" S(UVA_DATA_N) ", %rbx\n  movq $0x99, (%rbx)\n"
    "  xorl %edi, %edi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl ub_write_a_end\nub_write_a_end:\n"

    ".globl ub_write_b\nub_write_b:\n"
    "  movabsq $" S(UVA_DATA_N) ", %rbx\n  movq $0x2222, (%rbx)\n"
    "  xorl %edi, %edi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl ub_write_b_end\nub_write_b_end:\n"

    ".globl ub_multi\nub_multi:\n"
    "  movabsq $" S(UVA_MULTI_N) ", %rbx\n  movq $0x3333, 0x2000(%rbx)\n"
    "  xorl %edi, %edi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl ub_multi_end\nub_multi_end:\n"
);
extern char ub_good[], ub_good_end[], ub_badsys[], ub_badsys_end[];
extern char ub_write_ro[], ub_write_ro_end[], ub_exec_data[], ub_exec_data_end[];
extern char ub_read_kernel[], ub_read_kernel_end[], ub_read_none[], ub_read_none_end[];
extern char ub_port[], ub_port_end[], ub_write_a[], ub_write_a_end[];
extern char ub_write_b[], ub_write_b_end[], ub_multi[], ub_multi_end[];

/* ---- helpers ------------------------------------------------------------- */
static int fails;
static void check(const char *name, int ok) {
    serial_puts(ok ? "PASS  " : "FAIL  ");
    serial_puts(name);
    serial_puts("\n");
    if (!ok) fails++;
}

static UINT64 rd64(UINT64 phys) { return *(volatile UINT64 *)(UINTN)phys; }
static void   wr64(UINT64 phys, UINT64 v) { *(volatile UINT64 *)(UINTN)phys = v; }

static UINT64 phys_of(XToken t) { UINT64 p = 0; xtok_ram_info(t, &p, 0); return p; }

static XToken mk(UINT32 owner, UINT64 frames, UINT32 rights) {
    return xtok_alloc_ram(owner, frames, rights);
}

static void load_code(UINT64 phys, const char *s, const char *e) {
    volatile UINT8 *d = (volatile UINT8 *)(UINTN)phys;
    for (int i = 0; i < 4096; i++) d[i] = 0;
    for (int i = 0; s + i < e; i++) d[i] = (UINT8)s[i];
}

static XProcResult R;

static int run(XAspace *as, UINT64 code_phys, const char *s, const char *e) {
    load_code(code_phys, s, e);
    return xproc_run(as, UVA_CODE, UVA_STACK + 4096, &R);
}

static void show(const char *label) {
    serial_puts("      ["); serial_puts(label); serial_puts("] vec=");
    serial_hex(R.vector); serial_puts(" err="); serial_hex(R.error);
    serial_puts(" cr2="); serial_hex(R.cr2); serial_puts(" rip="); serial_hex(R.rip);
    serial_puts("\n");
}

static void expect_fault(const char *name, UINT64 vec, UINT64 err, UINT64 cr2, int check_cr2) {
    int ok = R.reason == XPROC_FAULT && R.vector == vec && R.error == err &&
             (!check_cr2 || R.cr2 == cr2);
    check(name, ok);
    if (!ok) show(name);
}

#define RW_RIGHTS  (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define RX_RIGHTS  (XTOK_RIGHT_READ | XTOK_RIGHT_EXECUTE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define RO_RIGHTS  (XTOK_RIGHT_READ | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    if (phys_init_from_handoff(hand) != 0) { serial_puts("FAIL  phys_init\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    xtok_init();

    UINT64 fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL * hand->fb.height;
    check("xpt_init_identity (NX mandatory)", xpt_init_identity(hand->phys_end, hand->fb.base, fb_bytes) == 0);
    xproc_init();

    /* ---- the protections are really on, not just reported ---- */
    {
        UINT32 lo, hi; UINT64 cr0;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080u));
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        check("EFER.NXE is set in the CPU", (lo & (1u << 11)) != 0);
        check("CR0.WP is set in the CPU",   (cr0 & (1ULL << 16)) != 0);
        check("xpt_protections() reports NX|WP",
              (xpt_protections() & (XPROT_NX | XPROT_WP)) == (XPROT_NX | XPROT_WP));
        serial_puts("      SMEP active: "); serial_hex(xpt_protections() & XPROT_SMEP ? 1 : 0); serial_puts("\n");
    }

    UINT64 F0 = phys_free_count();

    XAspace as1, as2, dead;
    check("owner 0 (kernel) cannot own an address space", xas_create(&dead, XTOK_OWNER_KERNEL) == XAS_ERR_ARG);
    check("create address space for owner 1", xas_create(&as1, 1) == XAS_OK);
    check("create address space for owner 2", xas_create(&as2, 2) == XAS_OK);
    check("the two address spaces have different PML4s", as1.pml4_phys != as2.pml4_phys);

    /* ================= kernel-side MAP rules (no ring 3 needed) ============ */
    XToken tCode1 = mk(1, 1, RX_RIGHTS);
    XToken tData1 = mk(1, 1, RW_RIGHTS);
    XToken tRo1   = mk(1, 1, RO_RIGHTS);
    XToken tStk1  = mk(1, 1, RW_RIGHTS);
    UINT64 pCode1 = phys_of(tCode1), pData1 = phys_of(tData1), pRo1 = phys_of(tRo1);
    wr64(pRo1, 0x1122334455667788ULL);

    check("map code page R+X",   xas_map(&as1, UVA_CODE,  tCode1, XPERM_R | XPERM_X) == XAS_OK);
    check("map data page R+W",   xas_map(&as1, UVA_DATA,  tData1, XPERM_R | XPERM_W) == XAS_OK);
    check("map read-only page",  xas_map(&as1, UVA_RO,    tRo1,   XPERM_R) == XAS_OK);
    check("map stack page R+W",  xas_map(&as1, UVA_STACK, tStk1,  XPERM_R | XPERM_W) == XAS_OK);
    check("a mapped token has map_count 1", xtok_map_count(tData1) == 1);

    check("W+X together is refused (W^X)",
          xas_map(&as1, UVA_NONE, tCode1, XPERM_W | XPERM_X) == XAS_ERR_WX);
    check("owner 2 cannot map owner 1's token",
          xas_map(&as2, UVA_NONE, tData1, XPERM_R) == XAS_ERR_OWNER);
    check("requesting W on a token without WRITE is refused",
          xas_map(&as1, UVA_NONE, tRo1, XPERM_R | XPERM_W) == XAS_ERR_RIGHTS);
    check("requesting X on a token without EXECUTE is refused",
          xas_map(&as1, UVA_NONE, tData1, XPERM_R | XPERM_X) == XAS_ERR_RIGHTS);

    XToken tNoMap = mk(1, 1, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE);
    check("a token without the MAP right cannot be mapped at all",
          xas_map(&as1, UVA_NONE, tNoMap, XPERM_R) == XAS_ERR_RIGHTS);

    check("unaligned virtual address is refused", xas_map(&as1, UVA_NONE + 8, tData1, XPERM_R) == XAS_ERR_RANGE);
    check("kernel virtual range is refused",      xas_map(&as1, KERNEL_VA_N, tData1, XPERM_R) == XAS_ERR_RANGE);
    check("address at the user top is refused",   xas_map(&as1, XAS_USER_TOP, tData1, XPERM_R) == XAS_ERR_RANGE);
    check("zero/unknown permission bits are refused",
          xas_map(&as1, UVA_NONE, tData1, 0) == XAS_ERR_ARG && xas_map(&as1, UVA_NONE, tData1, 8) == XAS_ERR_ARG);

    check("mapping over a live mapping is refused", xas_map(&as1, UVA_DATA, tData1, XPERM_R | XPERM_W) == XAS_ERR_BUSY);
    check("...and the failed map did not leak a pin", xtok_map_count(tData1) == 1);

    XToken tTmp = mk(1, 1, RW_RIGHTS);
    xtok_release(tTmp);
    check("a released (stale) token cannot be mapped", xas_map(&as1, UVA_NONE, tTmp, XPERM_R) == XAS_ERR_TOKEN);

    check("a mapped token cannot be released", xtok_release(tData1) == -2);

    /* ================= ring-3 programs trying to break the rules ========= */
    check("run: well-behaved program", run(&as1, pCode1, ub_good, ub_good_end) == XPROC_EXIT);
    check("  exits with code 7", R.exit_code == 7);
    check("  logged exactly one value, the 0x42 it wrote and read back", R.log_n == 1 && R.log[0] == 0x42);
    check("  kernel sees the byte it wrote in the shared frame", rd64(pData1) == 0x42);

    check("run: unknown syscall", run(&as1, pCode1, ub_badsys, ub_badsys_end) == XPROC_EXIT);
    check("  returns -1 to the caller", R.exit_code == (UINT64)-1);

    check("run: write to a read-only page faults", run(&as1, pCode1, ub_write_ro, ub_write_ro_end) == XPROC_FAULT);
    expect_fault("  #PF, present+write+user (error 7), CR2 = the RO page", 14, 0x7, UVA_RO, 1);
    check("  the read-only frame was NOT modified", rd64(pRo1) == 0x1122334455667788ULL);

    check("run: jump into a data page faults", run(&as1, pCode1, ub_exec_data, ub_exec_data_end) == XPROC_FAULT);
    expect_fault("  #PF, instruction-fetch on NX page (error 0x15)", 14, 0x15, UVA_DATA, 1);
    check("  faulting RIP is the data page", R.rip == UVA_DATA);

    check("run: read kernel memory faults", run(&as1, pCode1, ub_read_kernel, ub_read_kernel_end) == XPROC_FAULT);
    expect_fault("  #PF, protection violation from user (error 5)", 14, 0x5, KERNEL_VA_N, 1);

    check("run: read an unmapped address faults", run(&as1, pCode1, ub_read_none, ub_read_none_end) == XPROC_FAULT);
    expect_fault("  #PF, page not present (error 4)", 14, 0x4, UVA_NONE, 1);

    check("run: port I/O from ring 3 faults", run(&as1, pCode1, ub_port, ub_port_end) == XPROC_FAULT);
    expect_fault("  #GP (no I/O permission)", 13, 0x0, 0, 0);

    /* ================= isolation between owners ========================== */
    XToken tCode2 = mk(2, 1, RX_RIGHTS);
    XToken tData2 = mk(2, 1, RW_RIGHTS);
    XToken tStk2  = mk(2, 1, RW_RIGHTS);
    UINT64 pCode2 = phys_of(tCode2), pData2 = phys_of(tData2);
    check("owner 2 maps its own pages at the SAME virtual addresses",
          xas_map(&as2, UVA_CODE, tCode2, XPERM_R | XPERM_X) == XAS_OK &&
          xas_map(&as2, UVA_DATA, tData2, XPERM_R | XPERM_W) == XAS_OK &&
          xas_map(&as2, UVA_STACK, tStk2, XPERM_R | XPERM_W) == XAS_OK);
    check("run: owner 2 writes its own data page", run(&as2, pCode2, ub_write_b, ub_write_b_end) == XPROC_EXIT);
    check("  owner 2's write landed in owner 2's frame", rd64(pData2) == 0x2222);
    check("  owner 1's frame at the same virtual address is untouched", rd64(pData1) == 0x42);

    /* ================= multi-frame token ================================= */
    XToken tMulti = mk(1, 3, RW_RIGHTS);
    UINT64 pMulti = phys_of(tMulti);
    check("a fresh multi-frame token is zeroed", rd64(pMulti) == 0 && rd64(pMulti + 0x1000) == 0 && rd64(pMulti + 0x2000) == 0);
    check("map a 3-frame token", xas_map(&as1, UVA_MULTI, tMulti, XPERM_R | XPERM_W) == XAS_OK);
    check("run: write into the THIRD page of the token", run(&as1, pCode1, ub_multi, ub_multi_end) == XPROC_EXIT);
    check("  it landed in the third frame, not the first", rd64(pMulti + 0x2000) == 0x3333 && rd64(pMulti) == 0);

    /* ================= revocation ======================================== */
    check("run: program writes its data page", run(&as1, pCode1, ub_write_a, ub_write_a_end) == XPROC_EXIT);
    check("  frame holds 0x99", rd64(pData1) == 0x99);
    check("kernel revokes the data token: 1 mapping removed", xas_revoke_token(tData1) == 1);
    check("  map_count is back to 0", xtok_map_count(tData1) == 0);
    check("run: the same program after revocation", run(&as1, pCode1, ub_write_a, ub_write_a_end) == XPROC_FAULT);
    expect_fault("  #PF, write to a no-longer-present page (error 6)", 14, 0x6, UVA_DATA, 1);
    check("  the revoked frame was not written again", rd64(pData1) == 0x99);
    check("  after revocation the token can be released", xtok_release(tData1) == 0);

    /* ================= zero-on-allocate: no cross-owner leaks ============ */
    XToken tLeak = mk(1, 1, RW_RIGHTS);
    UINT64 pLeak = phys_of(tLeak);
    for (int i = 0; i < 512; i++) wr64(pLeak + (UINT64)i * 8, 0xABABABABABABABABULL);
    xtok_release(tLeak);
    XToken tNew = mk(2, 1, RW_RIGHTS);
    check("the allocator recycled the same physical frame", phys_of(tNew) == pLeak);
    int clean = 1;
    for (int i = 0; i < 512; i++) if (rd64(pLeak + (UINT64)i * 8) != 0) clean = 0;
    check("...and the new owner sees all zeros, not the old owner's bytes", clean);

    /* ================= teardown: nothing leaks =========================== */
    xas_destroy(&as1);
    xas_destroy(&as2);
    check("destroyed address spaces report dead", !as1.live && !as2.live);
    check("tokens still pinned by nothing after destroy", xtok_map_count(tCode1) == 0 && xtok_map_count(tStk2) == 0);
    xtok_release(tCode1); xtok_release(tRo1); xtok_release(tStk1); xtok_release(tNoMap);
    xtok_release(tCode2); xtok_release(tData2); xtok_release(tStk2);
    xtok_release(tMulti); xtok_release(tNew);
    check("every frame is back: tokens AND page-table frames", phys_free_count() == F0);
    check("no live tokens remain", xtok_live_count() == 0);

    serial_puts(fails ? "RESULT: FAILURES\n" : "RESULT: ALL PASS\n");
    serial_hex((unsigned long long)fails); serial_puts(" failing\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
