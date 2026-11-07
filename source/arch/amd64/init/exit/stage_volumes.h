/******************************************************************************
 * XASMOS - bootstrap stage: volumes
 * Path: source/arch/amd64/init/exit/stage_volumes.h
 ******************************************************************************/
#ifndef XASMOS_STAGE_VOLUMES_H
#define XASMOS_STAGE_VOLUMES_H

/* Returns 0 when the boot volume is mounted. A missing system disk is not an
 * error. A negative value means the boot volume could not be created, and the
 * shell must not start. */
int stage_volumes(void);

#endif /* XASMOS_STAGE_VOLUMES_H */
