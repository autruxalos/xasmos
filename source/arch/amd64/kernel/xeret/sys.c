/******************************************************************************
 * XASMOS - Resource syscalls: alloc / map / unmap / free / info, and reaping
 * Path: source|arch|amd64|kernel|xeret|sys.c
 *
 * Trust model: every register is attacker-controlled, so every value is
 * checked and none is ever used as a pointer. The owner is taken from the
 * address space the kernel itself set up. A token that belongs to someone
 * else and one that never existed produce the SAME error, so a program cannot
 * probe which tokens are live.
 *
 * Quota checks are not atomic with the allocation they guard. That is sound
 * while the kernel runs one CPU with interrupts off; under SMP they must move
 * inside the ledger lock.
 ******************************************************************************/
#include "xring_table.h"
#include "xsys.h"
#include "dev.h"
#include "stage_volumes.h"

/* ---- user memory, validated against the program's own tables -------------
 * The program's pages are reached through its pml4, never through CR3, so a
 * request can only read memory that address space actually maps. walk_pte
 * returns 0 for a missing or huge page, which is exactly the refusal we want. */
/* Copies a NUL-terminated name from the program's memory. Every byte is read
 * only through the program's own page tables: the page must be present, 4 KiB,
 * and user-accessible (XPT_USER). Returns the length, or -1 on any refusal. */
static int user_copy_name(XAspace *as, UINT64 ptr, char *out, UINT32 max) {
    UINT32 i;
    if (max == 0) return -1;
    for (i = 0; i < max; i++) {
        UINT64 va = ptr + i;
        if (va < ptr) return -1;                          /* wrapped past 2^64 */
        volatile UINT64 *pte = xpt_user_pte(as->pml4_phys, va & ~0xFFFULL);
        if (!pte) return -1;                              /* absent or huge */
        if (!(*pte & XPT_USER)) return -1;                /* kernel page: refuse */
        UINT64 phys = *pte & XPT_ADDR_MASK;
        char c = *((volatile char *)(UINTN)(phys + (va & 0xFFFULL)));
        out[i] = c;
        if (c == '\0') return (int)i;
    }
    return -1;                                            /* no terminator within max */
}

#include "mem/owner.h"
#include "mem/derive_call.h"
#define XOWNER_TABLE (XOWNER_FIRST_INSTANCE + XOWNER_MAX)

typedef struct {
    UINT64 frames;
    UINT32 tokens;
    UINT32 set;
} Quota;

static Quota  g_quota[XOWNER_TABLE];
static UINT64 g_def_frames = 2048;     /* 8 MiB */
static UINT32 g_def_tokens = 256;

int xproc_set_quota(UINT32 owner, UINT64 max_frames, UINT32 max_tokens) {
    if (owner == XTOK_OWNER_KERNEL || owner >= XOWNER_TABLE) return -1;
    g_quota[owner].frames = max_frames;
    g_quota[owner].tokens = max_tokens;
    g_quota[owner].set    = 1;
    return 0;
}

static void quota_of(UINT32 owner, UINT64 *frames, UINT32 *tokens) {
    if (owner < XOWNER_TABLE && g_quota[owner].set) {
        *frames = g_quota[owner].frames;
        *tokens = g_quota[owner].tokens;
    } else {
        *frames = g_def_frames;
        *tokens = g_def_tokens;
    }
}

static void ret(ExcFrame *f, long long code) {
    f->rax = (unsigned long long)code;
}

static long long map_xas_err(int rc) {
    switch (rc) {
    case XAS_OK:          return 0;
    case XAS_ERR_ARG:     return XERR_INVAL;
    case XAS_ERR_WX:      return XERR_PERM;
    case XAS_ERR_RANGE:   return XERR_RANGE;
    case XAS_ERR_TOKEN:   return XERR_BADTOKEN;
    case XAS_ERR_OWNER:   return XERR_BADTOKEN;     /* not yours == does not exist */
    case XAS_ERR_RIGHTS:  return XERR_PERM;
    case XAS_ERR_BUSY:    return XERR_BUSY;
    case XAS_ERR_NOMEM:   return XERR_NOMEM;
    case XAS_ERR_NOTMAPPED: return XERR_NOMAP;
    default:              return XERR_INVAL;
    }
}

static void sys_alloc(XAspace *as, ExcFrame *f) {
    UINT64 n      = f->rdi;
    UINT64 rights = f->rsi;
    f->rdx = 0;                                     /* never leave kernel bytes behind */

    if (n == 0 || rights == 0)                { ret(f, XERR_INVAL); return; }
    if (rights & ~(UINT64)XSYS_USER_RIGHTS)   { ret(f, XERR_PERM);  return; }

    UINT64 max_frames, used_frames;
    UINT32 max_tokens, used_tokens;
    quota_of(as->owner_id, &max_frames, &max_tokens);
    xtok_owner_usage(as->owner_id, &used_frames, &used_tokens);

    /* Compare without ever multiplying or adding past 64 bits. */
    if (used_tokens >= max_tokens)                          { ret(f, XERR_NOMEM); return; }
    if (n > max_frames || used_frames > max_frames - n)     { ret(f, XERR_NOMEM); return; }

    XToken tok = xtok_alloc_ram(as->owner_id, n, (UINT32)rights);
    if (tok == XTOKEN_INVALID)                { ret(f, XERR_NOMEM); return; }

    f->rdx = tok;
    ret(f, 0);
}

static void sys_map(XAspace *as, ExcFrame *f) {
    if (f->rdx > 0xFFFFFFFFULL)  { ret(f, XERR_INVAL); return; }
    ret(f, map_xas_err(xas_map(as, f->rsi, f->rdi, (UINT32)f->rdx)));
}

static void sys_unmap(XAspace *as, ExcFrame *f) {
    ret(f, map_xas_err(xas_unmap(as, f->rdi)));
}

static void sys_free(XAspace *as, ExcFrame *f) {
    int rc = xtok_release_owned(f->rdi, as->owner_id);
    switch (rc) {
    case 0:                 ret(f, 0);              break;
    case XTOK_REL_EBUSY:    ret(f, XERR_BUSY);      break;
    case XTOK_REL_ERIGHTS:  ret(f, XERR_PERM);      break;
    default:                ret(f, XERR_BADTOKEN);  break;
    }
}

static void sys_info(XAspace *as, ExcFrame *f) {
    UINT64 max_frames, used_frames;
    UINT32 max_tokens, used_tokens;
    quota_of(as->owner_id, &max_frames, &max_tokens);
    xtok_owner_usage(as->owner_id, &used_frames, &used_tokens);

    switch (f->rdi) {
    case XINFO_FRAMES_USED:  f->rax = used_frames;  break;
    case XINFO_FRAMES_LIMIT: f->rax = max_frames;   break;
    case XINFO_TOKENS_USED:  f->rax = used_tokens;  break;
    case XINFO_TOKENS_LIMIT: f->rax = max_tokens;   break;
    default:                 ret(f, XERR_INVAL);    break;
    }
}

/* XSYS_REQUEST_BLOCKS: rdi = name pointer ("rd0", "nd0"), rsi = first block,
 * rdx = block count, rcx = rights (READ and/or WRITE only). Returns the token
 * in rdx on success, 0 in rdx on failure. Every value is checked before use,
 * and the owner is always as->owner_id. */
static void sys_request_blocks(XAspace *as, ExcFrame *f) {
    char name[8];
    char full[16] = "|dev|";
    UINT64 ptr    = f->rdi;
    UINT64 first  = f->rsi;
    UINT64 count  = f->rdx;
    UINT64 rights = f->rcx;
    f->rdx = 0;                                       /* never leave kernel bytes */

    if (count == 0)                                   { ret(f, XERR_INVAL); return; }
    if (rights == 0 ||
        (rights & ~(UINT64)(XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE))) { ret(f, XERR_PERM); return; }

    int nlen = user_copy_name(as, ptr, name, sizeof name);
    if (nlen <= 0)                                    { ret(f, XERR_INVAL); return; }

    for (int i = 0; i <= nlen && i + 5 < (int)sizeof full; i++) full[5 + i] = name[i];

    BlkDev *dev = dev_lookup(full);
    if (!dev)                                         { ret(f, XERR_BADTOKEN); return; }

    if (!xtok_grant_check_range(name, as->owner_id, (UINT32)rights, first, count)) {
        ret(f, XERR_PERM);
        return;
    }

    /* Range check without overflow: first + count must not pass the device. */
    UINT64 total = blk_sector_count(dev);
    if (first > total || count > total - first)       { ret(f, XERR_RANGE); return; }

    XToken tok = xtok_alloc_blocks(as->owner_id, dev, first, count, (UINT32)rights);
    if (tok == XTOKEN_INVALID)                        { ret(f, XERR_NOMEM); return; }

    f->rdx = tok;
    ret(f, 0);
}


/* ---- program rings (XSYS_QUEUE_*) ----------------------------------------
 * Validation only; the table (xring_table.c) decides everything. The owner is
 * always the caller's own address space, never an argument. A token that is not
 * a ring of this owner answers XERR_BADTOKEN, the same as an unknown one. */
static void sys_queue_create(XAspace *as, ExcFrame *f) {
    XToken tok = XTOKEN_INVALID;
    f->rdx = 0;                        /* same convention as sys_alloc */
    int rc = xrt_create(as->owner_id, (UINT32)f->rdi, &tok);
    if (rc > 0) {
        f->rdx = (unsigned long long)tok;
        ret(f, 0);
    } else if (rc == XRT_E_ARG) {
        ret(f, XERR_INVAL);
    } else {
        ret(f, XERR_NOMEM);                /* cap reached, no id, or no frames */
    }
}

static void sys_queue_ring(XAspace *as, ExcFrame *f) {
    int id = xrt_lookup(as->owner_id, (XToken)f->rdi);
    KRing *r = (id > 0) ? xrt_ring((uint32_t)id) : 0;
    if (!r) { ret(f, XERR_BADTOKEN); return; }
    ret(f, (long long)kr_process(r));  /* a pass is only a hint: the program never depends on it */
}

static void sys_queue_close(XAspace *as, ExcFrame *f) {
    if (xrt_destroy(as->owner_id, (XToken)f->rdi) != 0) { ret(f, XERR_BADTOKEN); return; }
    ret(f, 0);                         /* released later, by xrt_reap, once drained */
}

/* ---- XSYS_DERIVE --------------------------------------------------------
 * Attenuation only: the caller presents a parent it holds and asks for a
 * relative subrange with rights. The child belongs to the caller. Every refusal
 * is XERR_BADTOKEN, so the call is no oracle over tokens it does not hold.
 * rdx is cleared first: the answer also travels in rdx. */
static void sys_derive(XAspace *as, ExcFrame *f) {
    UINT64 parent = f->rdi;
    UINT64 first  = f->rsi;
    UINT64 count  = f->rdx;
    UINT64 rights = f->rcx;
    XToken out = XTOKEN_INVALID;
    f->rdx = 0;                                       /* never leave kernel bytes */

    if (rights > 0xFFFFFFFFULL)                       { ret(f, XERR_BADTOKEN); return; }
    if (xsys_derive(as->owner_id, (XToken)parent, first, count,
                    (UINT32)rights, &out) != XDERIVE_OK) { ret(f, XERR_BADTOKEN); return; }
    f->rdx = (unsigned long long)out;
    ret(f, 0);
}

typedef void (*XsysHandler)(XAspace *as, ExcFrame *f);

static const XsysHandler g_xsys_table[XSYS_TABLE_SIZE] = {
    [XSYS_ALLOC]          = sys_alloc,
    [XSYS_MAP]            = sys_map,
    [XSYS_UNMAP]          = sys_unmap,
    [XSYS_FREE]           = sys_free,
    [XSYS_INFO]           = sys_info,
    [XSYS_REQUEST_BLOCKS] = sys_request_blocks,
    [XSYS_QUEUE_CREATE]   = sys_queue_create,
    [XSYS_QUEUE_RING]     = sys_queue_ring,
    [XSYS_QUEUE_CLOSE]    = sys_queue_close,
    [XSYS_DERIVE]         = sys_derive,
};

/* Every registered number must fit in the table. */
typedef char xsys_table_fits[(XSYS_REQUEST_BLOCKS < XSYS_TABLE_SIZE) ? 1 : -1];
typedef char xsys_derive_fits[(XSYS_DERIVE < XSYS_TABLE_SIZE) ? 1 : -1];

void xsys_resource(XAspace *as, ExcFrame *f) {
    UINT64 n = f->rax;
    XsysHandler h = (n < XSYS_TABLE_SIZE) ? g_xsys_table[n] : 0;
    if (!h) { ret(f, XERR_NOSYS); return; }
    h(as, f);
}

UINT32 xproc_reap(XAspace *as) {
    if (!as || !as->live) return 0;

    UINT32 owner = as->owner_id;
    UINT32 total = 0;
    xrt_destroy_owner(owner);       /* its rings close first; xrt_reap frees them */
    xrt_reap();                     /* release the drained ones now, while the space still exists */
    XToken list[64];

    for (;;) {
        UINT32 n = xtok_owner_tokens(owner, list, 64);
        if (n == 0) break;

        UINT32 progress = 0;
        for (UINT32 i = 0; i < n; i++) {
            xas_revoke_token(list[i]);              /* unmap it everywhere first   */
            if (xtok_reclaim(list[i]) == 0) progress++;
        }
        if (progress == 0) break;                   /* never spin on a stuck token */
        total += progress;
    }

    xas_destroy(as);
    return total;
}

/* Exported for the loader backend (load_amd64.c): the limits that
 * xproc_set_quota and the defaults give to an owner. Read-only. */
void xproc_quota_get(UINT32 owner, UINT64 *max_frames, UINT32 *max_tokens) {
    quota_of(owner, max_frames, max_tokens);
}
