/******************************************************************************
 * XASMOS - Page tables and per-owner address spaces (amd64 Long Mode)
 * Path: source|arch|amd64|kernel|xeret|mem|paging.c
 *
 * Kernel identity map: 2 MiB pages, supervisor-only, shared via PML4[0].
 * User mappings: 4 KiB pages built ONLY by xas_map(), whose permissions come
 * from the token's rights:
 *     READ     -> present + user
 *     WRITE    -> + writable
 *     (no EXEC)-> + NX
 * and whose owner must match the address space's owner.
 *
 * Lock order (never reversed anywhere):  g_pt_lock  ->  ledger lock
 *                                        g_pt_lock  ->  physical lock
 * The ledger and the physical allocator never call back into this file, and
 * never nest with each other.
 ******************************************************************************/
#include "kmemory.h"
#include "paging.h"
#include "spinlock.h"

#define XPT_ENTS      512ULL
#define XPT_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define XPT_HUGE_MASK 0x000FFFFFFFE00000ULL
#define LAPIC_IOAPIC_BASE 0xFEC00000ULL
#define FOUR_GIB      0x100000000ULL
#define PML4_SPAN     (1ULL << 39)          /* 512 GiB */
#define XAS_MAX_MAPS  256

#define MSR_EFER      0xC0000080u
#define EFER_NXE      (1ULL << 11)
#define CR0_WP        (1ULL << 16)
#define CR4_SMEP      (1ULL << 20)

typedef struct {
    UINT64 as_pml4;     /* identifies the address space */
    UINT64 virt;
    XToken tok;
    UINT64 pages;
    int    live;
} MapRec;

static UINT64     g_pml4_phys;
static UINT32     g_prot;
static MapRec     g_maps[XAS_MAX_MAPS];
static XSpinlock  g_pt_lock = XSPINLOCK_INIT;

/* ---- small helpers ------------------------------------------------------ */

static UINT64 align_down(UINT64 v, UINT64 a) { return v & ~(a - 1); }
static UINT64 align_up(UINT64 v, UINT64 a)   { return (v + a - 1) & ~(a - 1); }

static volatile UINT64 *tbl(UINT64 phys) {
    return (volatile UINT64 *)(UINTN)phys;
}

static UINT64 alloc_zero_table(void) {
    UINT64 phys = phys_alloc_frames(1);
    if (phys == 0) return 0;
    volatile UINT64 *p = tbl(phys);
    for (UINT64 i = 0; i < XPT_ENTS; i++) p[i] = 0;
    return phys;
}

static void write_cr3(UINT64 v) { __asm__ volatile ("mov %0, %%cr3" : : "r"(v) : "memory"); }
UINT64 xpt_read_cr3(void) { UINT64 v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v; }
static void invlpg(UINT64 va) { __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory"); }

static void cpuid(UINT32 leaf, UINT32 sub, UINT32 *a, UINT32 *b, UINT32 *c, UINT32 *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static UINT64 rdmsr(UINT32 msr) {
    UINT32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((UINT64)hi << 32) | lo;
}
static void wrmsr(UINT32 msr, UINT64 v) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((UINT32)v), "d"((UINT32)(v >> 32)));
}

UINT32 xpt_protections(void) { return g_prot; }
UINT64 xpt_pml4_phys(void)   { return g_pml4_phys; }

/* Turns on the bits that make the page-table permissions real. Without
 * EFER.NXE the NX bit is a reserved bit; without CR0.WP the kernel would
 * silently ignore read-only pages. Returns -1 if the CPU has no NX. */
static int enable_protections(void) {
    UINT32 a, b, c, d;
    UINT32 prot = 0;

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    if (a < 0x80000001u) return -1;
    cpuid(0x80000001u, 0, &a, &b, &c, &d);
    if (!(d & (1u << 20))) return -1;                    /* CPUID.80000001h:EDX.NX */

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);
    prot |= XPROT_NX;

    UINT64 cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    prot |= XPROT_WP;

    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        if (b & (1u << 7)) {                              /* CPUID.7:EBX.SMEP */
            UINT64 cr4;
            __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= CR4_SMEP;
            __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
            prot |= XPROT_SMEP;
        }
    }
    g_prot = prot;
    return 0;
}

/* ---- kernel identity map ------------------------------------------------ */

static volatile UINT64 *next_level(volatile UINT64 *table, UINT64 idx, UINT64 flags) {
    UINT64 phys;
    if (!(table[idx] & XPT_PRESENT)) {
        phys = alloc_zero_table();
        if (phys == 0) return 0;
        table[idx] = phys | flags;
    } else {
        phys = table[idx] & XPT_ADDR_MASK;
    }
    return tbl(phys);
}

/* First mapping wins: an entry already present is left alone. */
static int map_huge(UINT64 pml4_phys, UINT64 start, UINT64 end, UINT64 flags) {
    volatile UINT64 *pml4 = tbl(pml4_phys);
    for (UINT64 a = align_down(start, XERET_HUGE_SIZE); a < end; a += XERET_HUGE_SIZE) {
        volatile UINT64 *pdpt = next_level(pml4, (a >> 39) & 0x1FF, XPT_KERNEL_RW);
        if (!pdpt) return -1;
        volatile UINT64 *pd = next_level(pdpt, (a >> 30) & 0x1FF, XPT_KERNEL_RW);
        if (!pd) return -1;
        UINT64 idx = (a >> 21) & 0x1FF;
        if (!(pd[idx] & XPT_PRESENT)) {
            pd[idx] = (a & XPT_HUGE_MASK) | flags | XPT_HUGE;
        }
    }
    return 0;
}

int xpt_init_identity(UINT64 phys_end, UINT64 extra_base, UINT64 extra_size) {
    if (phys_end == 0) return -1;

    /* Per-owner address spaces share PML4[0], so the kernel map must fit it. */
    if (align_up(phys_end, XERET_HUGE_SIZE) > PML4_SPAN) return -1;
    if (extra_size != 0 && (extra_base + extra_size > PML4_SPAN || extra_base + extra_size < extra_base)) return -1;

    if (enable_protections() != 0) return -1;

    UINT64 pml4_phys = alloc_zero_table();
    if (pml4_phys == 0) return -1;

    if (map_huge(pml4_phys, 0, align_up(phys_end, XERET_HUGE_SIZE),
                 XPT_KERNEL_RW | XPT_GLOBAL) != 0) return -1;
    if (extra_size != 0) {
        if (map_huge(pml4_phys, extra_base, extra_base + extra_size,
                     XPT_KERNEL_RW | XPT_GLOBAL | XPT_UC) != 0) return -1;
    }
    if (map_huge(pml4_phys, LAPIC_IOAPIC_BASE, FOUR_GIB,
                 XPT_KERNEL_RW | XPT_GLOBAL | XPT_UC) != 0) return -1;

    write_cr3(pml4_phys);
    g_pml4_phys = pml4_phys;
    return 0;
}

int xpt_translate(UINT64 virt, UINT64 *out_phys) {
    if (g_pml4_phys == 0) return -1;

    UINT64 e = tbl(g_pml4_phys)[(virt >> 39) & 0x1FF];
    if (!(e & XPT_PRESENT)) return -1;
    e = tbl(e & XPT_ADDR_MASK)[(virt >> 30) & 0x1FF];
    if (!(e & XPT_PRESENT)) return -1;
    e = tbl(e & XPT_ADDR_MASK)[(virt >> 21) & 0x1FF];
    if (!(e & XPT_PRESENT) || !(e & XPT_HUGE)) return -1;

    if (out_phys) *out_phys = (e & XPT_HUGE_MASK) | (virt & (XERET_HUGE_SIZE - 1));
    return 0;
}

/* ---- 4 KiB user mappings ------------------------------------------------ */

#define XPT_UPPER (XPT_PRESENT | XPT_WRITE | XPT_USER)   /* intermediate levels */

/* The PTE slot for va, or 0 if any level is absent. Never allocates. */
static volatile UINT64 *walk_pte(UINT64 pml4_phys, UINT64 va) {
    UINT64 e = tbl(pml4_phys)[(va >> 39) & 0x1FF];
    if (!(e & XPT_PRESENT)) return 0;
    e = tbl(e & XPT_ADDR_MASK)[(va >> 30) & 0x1FF];
    if (!(e & XPT_PRESENT)) return 0;
    e = tbl(e & XPT_ADDR_MASK)[(va >> 21) & 0x1FF];
    if (!(e & XPT_PRESENT) || (e & XPT_HUGE)) return 0;
    return tbl(e & XPT_ADDR_MASK) + ((va >> 12) & 0x1FF);
}

static int map_page(UINT64 pml4_phys, UINT64 va, UINT64 pa, UINT64 leaf) {
    volatile UINT64 *pdpt = next_level(tbl(pml4_phys), (va >> 39) & 0x1FF, XPT_UPPER);
    if (!pdpt) return XAS_ERR_NOMEM;
    volatile UINT64 *pd = next_level(pdpt, (va >> 30) & 0x1FF, XPT_UPPER);
    if (!pd) return XAS_ERR_NOMEM;
    volatile UINT64 *pt = next_level(pd, (va >> 21) & 0x1FF, XPT_UPPER);
    if (!pt) return XAS_ERR_NOMEM;

    UINT64 idx = (va >> 12) & 0x1FF;
    if (pt[idx] & XPT_PRESENT) return XAS_ERR_BUSY;
    pt[idx] = (pa & XPT_ADDR_MASK) | leaf;
    return XAS_OK;
}

static void unmap_pages(UINT64 pml4_phys, UINT64 va, UINT64 pages) {
    for (UINT64 i = 0; i < pages; i++) {
        volatile UINT64 *pte = walk_pte(pml4_phys, va + i * XERET_FRAME_SIZE);
        if (pte && (*pte & XPT_PRESENT)) {
            *pte = 0;
            invlpg(va + i * XERET_FRAME_SIZE);
        }
    }
}

/* Caller holds g_pt_lock. */
static void drop_record(MapRec *r) {
    unmap_pages(r->as_pml4, r->virt, r->pages);
    xtok_map_release(r->tok);
    r->live = 0;
}

static int map_err(int rc) {
    switch (rc) {
    case XTOK_MAP_ESTALE:  return XAS_ERR_TOKEN;
    case XTOK_MAP_EKIND:   return XAS_ERR_TOKEN;
    case XTOK_MAP_EOWNER:  return XAS_ERR_OWNER;
    case XTOK_MAP_ERIGHTS: return XAS_ERR_RIGHTS;
    default:               return XAS_ERR_BUSY;
    }
}

int xas_create(XAspace *as, UINT32 owner_id) {
    if (!as || g_pml4_phys == 0 || owner_id == XTOK_OWNER_KERNEL) return XAS_ERR_ARG;

    xspin_lock(&g_pt_lock);
    UINT64 pml4_phys = alloc_zero_table();
    if (pml4_phys == 0) { xspin_unlock(&g_pt_lock); return XAS_ERR_NOMEM; }

    /* The kernel must live entirely in PML4[0]; share it, supervisor-only. */
    volatile UINT64 *kp = tbl(g_pml4_phys);
    volatile UINT64 *np = tbl(pml4_phys);
    for (UINT64 i = 1; i < XPT_ENTS; i++) {
        if (kp[i] & XPT_PRESENT) {
            phys_free_frames(pml4_phys, 1);
            xspin_unlock(&g_pt_lock);
            return XAS_ERR_ARG;
        }
    }
    np[0] = kp[0];

    as->pml4_phys = pml4_phys;
    as->owner_id  = owner_id;
    as->live      = 1;
    xspin_unlock(&g_pt_lock);
    return XAS_OK;
}

int xas_map(XAspace *as, UINT64 virt, XToken tok, UINT32 perms) {
    if (!as || !as->live) return XAS_ERR_ARG;
    if (perms == 0 || (perms & ~(XPERM_R | XPERM_W | XPERM_X))) return XAS_ERR_ARG;
    if ((perms & XPERM_W) && (perms & XPERM_X)) return XAS_ERR_WX;
    if ((virt & (XERET_FRAME_SIZE - 1)) != 0) return XAS_ERR_RANGE;
    if (virt < XAS_USER_BASE || virt >= XAS_USER_TOP) return XAS_ERR_RANGE;

    /* Rights the token must carry. READ is always required (x86 can't map
     * write-only or execute-only); MAP is the gate this file exists for. */
    UINT32 need = XTOK_RIGHT_MAP | XTOK_RIGHT_READ;
    if (perms & XPERM_W) need |= XTOK_RIGHT_WRITE;
    if (perms & XPERM_X) need |= XTOK_RIGHT_EXECUTE;

    /* Hardware permissions follow from the request, which the rights bound. */
    UINT64 leaf = XPT_PRESENT | XPT_USER;
    if (perms & XPERM_W)    leaf |= XPT_WRITE;
    if (!(perms & XPERM_X)) leaf |= XPT_NX;

    xspin_lock(&g_pt_lock);

    UINT64 phys = 0, frames = 0;
    int rc = xtok_map_acquire(tok, as->owner_id, need, &phys, &frames);
    if (rc != XTOK_MAP_OK) { xspin_unlock(&g_pt_lock); return map_err(rc); }

    if (frames == 0 || frames > (XAS_USER_TOP - virt) / XERET_FRAME_SIZE) {
        xtok_map_release(tok);
        xspin_unlock(&g_pt_lock);
        return XAS_ERR_RANGE;
    }

    MapRec *rec = 0;
    for (int i = 0; i < XAS_MAX_MAPS; i++) {
        if (!g_maps[i].live) { rec = &g_maps[i]; break; }
    }
    if (!rec) {
        xtok_map_release(tok);
        xspin_unlock(&g_pt_lock);
        return XAS_ERR_NOMEM;
    }

    for (UINT64 i = 0; i < frames; i++) {
        int r = map_page(as->pml4_phys, virt + i * XERET_FRAME_SIZE,
                         phys + i * XERET_FRAME_SIZE, leaf);
        if (r != XAS_OK) {
            unmap_pages(as->pml4_phys, virt, i);    /* roll back this call only */
            xtok_map_release(tok);
            xspin_unlock(&g_pt_lock);
            return r;
        }
    }

    rec->as_pml4 = as->pml4_phys;
    rec->virt    = virt;
    rec->tok     = tok;
    rec->pages   = frames;
    rec->live    = 1;
    xspin_unlock(&g_pt_lock);
    return XAS_OK;
}

int xas_unmap(XAspace *as, UINT64 virt) {
    if (!as || !as->live) return XAS_ERR_ARG;
    int rc = XAS_ERR_NOTMAPPED;
    xspin_lock(&g_pt_lock);
    for (int i = 0; i < XAS_MAX_MAPS; i++) {
        if (g_maps[i].live && g_maps[i].as_pml4 == as->pml4_phys && g_maps[i].virt == virt) {
            drop_record(&g_maps[i]);
            rc = XAS_OK;
            break;
        }
    }
    xspin_unlock(&g_pt_lock);
    return rc;
}

int xas_revoke_token(XToken tok) {
    int n = 0;
    xspin_lock(&g_pt_lock);
    for (int i = 0; i < XAS_MAX_MAPS; i++) {
        if (g_maps[i].live && g_maps[i].tok == tok) {
            drop_record(&g_maps[i]);
            n++;
        }
    }
    xspin_unlock(&g_pt_lock);
    return n;
}

void xas_destroy(XAspace *as) {
    if (!as || !as->live) return;

    xspin_lock(&g_pt_lock);
    for (int i = 0; i < XAS_MAX_MAPS; i++) {
        if (g_maps[i].live && g_maps[i].as_pml4 == as->pml4_phys) drop_record(&g_maps[i]);
    }

    /* Free the user-half tables; PML4[0] is the shared kernel map: not ours. */
    volatile UINT64 *pml4 = tbl(as->pml4_phys);
    for (UINT64 i4 = 1; i4 < XPT_ENTS; i4++) {
        if (!(pml4[i4] & XPT_PRESENT)) continue;
        UINT64 pdpt_phys = pml4[i4] & XPT_ADDR_MASK;
        volatile UINT64 *pdpt = tbl(pdpt_phys);
        for (UINT64 i3 = 0; i3 < XPT_ENTS; i3++) {
            if (!(pdpt[i3] & XPT_PRESENT)) continue;
            UINT64 pd_phys = pdpt[i3] & XPT_ADDR_MASK;
            volatile UINT64 *pd = tbl(pd_phys);
            for (UINT64 i2 = 0; i2 < XPT_ENTS; i2++) {
                if (pd[i2] & XPT_PRESENT) phys_free_frames(pd[i2] & XPT_ADDR_MASK, 1);
            }
            phys_free_frames(pd_phys, 1);
        }
        phys_free_frames(pdpt_phys, 1);
    }
    phys_free_frames(as->pml4_phys, 1);
    as->live = 0;
    xspin_unlock(&g_pt_lock);
}
