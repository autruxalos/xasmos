#include "xasm_user.h"
#include "xasm_ipc_proto.h"
#include "y_proto.h"
#define Y_IPC_VA      0x18000000ULL
#define Y_FB_VA       0x20000000ULL
#define Y_SURF_BASE   0x30000000ULL
#define Y_SURF_STRIDE 0x01000000ULL
typedef struct {
  uint8_t active, z; int32_t x, y; uint32_t w, h, stride; XToken tok;
} YSlot;
static YSlot g_slots[Y_MAX_SURFACES];
static uint32_t g_fb_w = 1024, g_fb_h = 768, g_fb_pitch = 4096, g_fb_bpp = 32;
static inline uint32_t *y_fb_ptr(void) { return (uint32_t *)(uintptr_t)Y_FB_VA; }
static inline const uint32_t *y_slot_ptr(uint32_t i) {
  return (const uint32_t *)(uintptr_t)(Y_SURF_BASE + (uint64_t)i * Y_SURF_STRIDE);
}
static void y_handle(XasmIpcMsg *m) {
  m->status = 0;
  if (m->op == OP_Y_HELLO) {
    YHelloInfo *hi = (YHelloInfo *)m->data;
    *hi = (YHelloInfo){Y_PROTO_MAGIC, g_fb_w, g_fb_h, g_fb_pitch, g_fb_bpp, Y_MAX_SURFACES};
    m->len = sizeof(YHelloInfo); return;
  }
  if (m->op == OP_Y_CREATE_SURFACE && m->len >= sizeof(YCreateSurfaceReq)) {
    const YCreateSurfaceReq *q = (const YCreateSurfaceReq *)m->data;
    for (uint32_t i = 0; i < Y_MAX_SURFACES; i++) if (!g_slots[i].active) {
      if (xsys_map(q->surf_tok, Y_SURF_BASE + (uint64_t)i * Y_SURF_STRIDE, 1u) < 0) { m->status = -13; return; }
      g_slots[i] = (YSlot){1, (uint8_t)i, q->x, q->y, q->w, q->h, q->stride, q->surf_tok};
      *(uint32_t *)m->data = i; m->len = 4; y_compose_rect(q->x, q->y, q->w, q->h); return;
    }
    m->status = -12; return;
  }
  if (m->op == OP_Y_COMMIT && m->len >= sizeof(YCommitReq)) {
    const YCommitReq *c = (const YCommitReq *)m->data;
    if (c->surf_id >= Y_MAX_SURFACES || !g_slots[i].active) { m->status = -22; return; }
    y_compose_rect(g_slots[i].x + (int32_t)c->x, g_slots[i].y + (int32_t)c->y, c->w, c->h); return;
  }
  if (m->op == OP_Y_DESTROY && m->len >= 4) {
    uint32_t id = *(const uint32_t *)m->data;
    if (id < Y_MAX_SURFACES && g_slots[i].active) {
      YSlot old = g_slots; g_slots[i].active = 0;
      (void)xsys_unmap(Y_SURF_BASE + (uint64_t)id * Y_SURF_STRIDE);
      y_compose_rect(old.x, old.y, old.w, old.h); return;
    }
  }
  m->status = -38;
}
void _start(XToken ep_recv, XToken ep_ctl, XToken fb_tok) {
  (void)ep_ctl;
  if (fb_tok) { (void)xsys_map(fb_tok, Y_FB_VA, XPERM_RW); y_fill_bg(0, 0, g_fb_w, g_fb_h); }
  for (;;) {
    XToken page_tok = 0;
    if (xsys_accept(ep_recv, &page_tok) < 0 || !page_tok) { (void)xsys3(XSYS_YIELD, 0, 0, 0); continue; }
    if (xsys_map(page_tok, Y_IPC_VA, XPERM_RW) < 0) { (void)xsys3(XSYS_YIELD, 0, 0, 0); continue; }
    XasmIpcMsg *msg = (XasmIpcMsg *)(uintptr_t)Y_IPC_VA;
    XToken reply_ep = msg->reply_ep;
    y_handle(msg);
    (void)xsys_unmap(Y_IPC_VA);
    if (reply_ep) (void)xsys_offer(page_tok, reply_ep);
  }
}
