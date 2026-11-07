/******************************************************************************
 * XASMOS - boot volume and system disk registration
 * Path: source/arch/amd64/init/boot_volume.h
 ******************************************************************************/
#ifndef XASMOS_BOOT_VOLUME_H
#define XASMOS_BOOT_VOLUME_H

/* Creates |dev|rd0 (RAM, 64 MiB), formats it and mounts it at the root. Then
 * publishes the system disk as |dev|nd0 if one is present. Returns 0, or a
 * negative code if the boot volume could not be created. A missing system
 * disk is not an error. */
int boot_volume_init(void);

#endif /* XASMOS_BOOT_VOLUME_H */
