/******************************************************************************
 * XASMOS - device registry (portable)
 * Path: source/core/dev/dev.h
 *
 * Block devices are published under |dev with explicit names:
 *   |dev|nd0   NVMe          |dev|sd0   SATA (AHCI)     |dev|hd0   IDE
 *   |dev|md0   ST-506        |dev|rd0   RAM (boot volume)
 * A name is registered once by the driver that owns it. Commands look devices
 * up by their full path, so the user always says which disk it is.
 *
 * Each device may carry the serial number its controller reports. The
 * registry only stores the string; the architecture decides what it means.
 ******************************************************************************/
#ifndef XASMOS_DEV_H
#define XASMOS_DEV_H

#include "../blk/blk.h"

#define DEV_MAX        8
#define DEV_NAME_MAX   8
#define DEV_SERIAL_MAX 24
#define DEV_PREFIX     "|dev|"

#define DEV_OK        0
#define DEV_EEXIST   (-1)
#define DEV_EFULL    (-2)
#define DEV_EINVAL   (-3)
#define DEV_ENOTFOUND (-4)

/* Publishes d under name (e.g. "nd0"). The name must be non-empty and short. */
int      dev_register(const char *name, BlkDev *d);

/* Stores the controller's serial number for a registered device. */
int      dev_set_serial(const char *name, const char *serial);

/* The serial number of a registered device, or NULL if none was stored. */
const char *dev_serial(const char *path);

/* The device at path (e.g. "|dev|nd0"), or NULL if no such device exists. */
BlkDev  *dev_lookup(const char *path);

/* Calls cb(name, device) for each registered device. Returns the count. */
int      dev_list(void (*cb)(const char *name, BlkDev *d));

#endif /* XASMOS_DEV_H */
