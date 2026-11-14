#ifndef HOST_KMEMORY_STUB_H
#define HOST_KMEMORY_STUB_H
#include "efi_types.h"

#define XERET_FRAME_SHIFT     12
#define XERET_FRAME_SIZE      ((UINT64)4096)
#define XERET_FRAMES_PER_HUGE 512u

UINT64 phys_alloc_frames(UINT64 n);
void   phys_free_frames(UINT64 addr, UINT64 n);
UINT64 phys_alloc_huge(void);

#endif
