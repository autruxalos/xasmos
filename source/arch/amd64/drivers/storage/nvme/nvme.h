/******************************************************************************
 * XASMOS - NVMe block driver (PCIe, polled, one I/O queue pair)
 * Path: source/arch/amd64/drivers/storage/nvme/nvme.h
 *
 * The driver fills a BlkDev; EXFS and the rest of the system see only blk.h.
 * Data moves through one DMA bounce page, so any caller buffer works, and a
 * transfer is split into bounce-sized commands.
 ******************************************************************************/
#ifndef XASMOS_NVME_H
#define XASMOS_NVME_H

#include <stdint.h>
#include "../../../../../core/blk/blk.h"
#include "../../pci/pci.h"

/* Brings up the controller behind `d` (class 01/08). On BLK_OK, `out` is ready
 * for blk_read/blk_write. Returns BLK_OK, BLK_E_ARG, BLK_E_IO or BLK_E_TIMEOUT. */
int nvme_init(const PciDev *d, BlkDev *out);
/* Serial number of the controller, trailing blanks removed. Needs nvme_init. */
int nvme_controller_serial(char *out, uint32_t max);
/* Pure decoding helpers (no hardware), used by the host tests. */

/* log2 of the sector size selected by FLBAS in an Identify Namespace page,
 * or 0 if the field is outside 9..12 (512 B to 4 KiB). */
uint32_t nvme_lbads_from_identify(const uint8_t *id);

/* NSZE: namespace size in logical blocks (bytes 0..7). */
uint64_t nvme_nsze_from_identify(const uint8_t *id);

/* A completion entry belongs to the consumer when its phase bit equals the
 * phase the consumer expects. Returns 1 if it does. */
int      nvme_cqe_is_new(uint32_t dw3, uint32_t expect_phase);

/* Status field of a completion (SCT:SC, phase bit excluded). 0 = success. */
uint32_t nvme_cqe_status(uint32_t dw3);

#endif /* XASMOS_NVME_H */
