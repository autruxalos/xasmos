/******************************************************************************
 * XASMOS - storage: the one place that decides which disk the system uses
 * Path: source/arch/amd64/drivers/storage/storage.h
 *
 * Commands and filesystems never touch a controller directly. They call
 * storage_open() once and then use storage_device(). Adding a new controller
 * (AHCI, USB mass storage) means changing this module, not the commands.
 *
 * "Test disk" is a disk whose controller reports the serial number "xasmos",
 * which is the QEMU test image. Write commands check it before they send
 * anything to the device.
 ******************************************************************************/
#ifndef XASMOS_STORAGE_H
#define XASMOS_STORAGE_H

#include "../../../../core/blk/blk.h"

#define STORAGE_OK       0
#define STORAGE_ENODEV (-10)   /* no supported storage controller on the bus */

/* Opens the system disk, once. Returns STORAGE_OK, STORAGE_ENODEV, or the
 * BLK_E_* code the controller driver reported. A failed open may be retried. */
int storage_open(void);

/* The open device, or NULL if storage_open() has not succeeded. */
BlkDev *storage_device(void);

/* 1 if the open device is the test disk (serial "xasmos"), else 0. */
int storage_is_test_disk(void);
/* Copies the open controller's serial into out. Returns 1 on success. */
int storage_serial(char *out, unsigned max);

#endif /* XASMOS_STORAGE_H */
