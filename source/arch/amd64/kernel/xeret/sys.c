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
#include "xsys.h"

#define XOWNER_MAX 64

typedef struct {
    UINT64 frames;
    UINT32 tokens;
    UINT32 set;
} Quota;

static Quota  g_quota[XOWNER_MAX];
static UINT64 g_def_frames = 2048;     /* 8 MiB */
static UINT32 g_def_tokens = 256;

int xproc_set_quota(UINT32 owner, UINT64 max_frames, UINT32 max_tokens) {
    if (owner == XTOK_OWNER_KERNEL || owner >= XOWNER_MAX) return -1;
    g_quota[owner].frames = max_frames;
    g_quota[owner].tokens = max_tokens;
    g_quota[owner].set    = 1;
    return 0;
}

static void quota_of(UINT32 owner, UINT64 *frames, UINT32 *tokens) {
    if (owner < XOWNER_MAX && g_quota[owner].set) {
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

void xsys_resource(XAspace *as, ExcFrame *f) {
    switch (f->rax) {
    case XSYS_ALLOC: sys_alloc(as, f); break;
    case XSYS_MAP:   sys_map(as, f);   break;
    case XSYS_UNMAP: sys_unmap(as, f); break;
    case XSYS_FREE:  sys_free(as, f);  break;
    case XSYS_INFO:  sys_info(as, f);  break;
    default:         ret(f, XERR_NOSYS); break;
    }
}

UINT32 xproc_reap(XAspace *as) {
    if (!as || !as->live) return 0;

    UINT32 owner = as->owner_id;
    UINT32 total = 0;
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
