#ifndef HOST_STUBS_H
#define HOST_STUBS_H
#include <stdint.h>
#include "blk.h"

/* A RAM block device with the given geometry; zeroed on creation. */
void host_dev_init(BlkDev *d, uint32_t sector_size, uint64_t sector_count);
/* While on, every read and write fails with BLK_E_IO. */
void host_dev_fail(int on);

#endif
