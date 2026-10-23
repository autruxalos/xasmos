/******************************************************************************
 * XASMOS - LAPIC one-shot timer (amd64): the CPU-time resource
 * Path: source|arch|amd64|kernel|xeret|lapic.h
 ******************************************************************************/
#ifndef XASMOS_LAPIC_H
#define XASMOS_LAPIC_H

#include "efi_types.h"

#define LAPIC_TIMER_VEC     0x40
#define LAPIC_SPURIOUS_VEC  0xFF

/* Masks the legacy PIC (so stray IRQs cannot land on exception vectors),
 * enables the local APIC (xAPIC MMIO or x2APIC MSRs, whichever the firmware
 * left active) and calibrates its timer against PIT channel 2.
 * Returns 0, or -1 if there is no usable APIC/PIT. Needs the APIC MMIO
 * window mapped (xpt_init_identity maps it). Loops are bounded: a machine
 * without a PIT fails cleanly instead of hanging. */
int    lapic_timer_init(void);

UINT32 lapic_ticks_per_ms(void);
int    lapic_x2apic(void);

void   lapic_timer_arm(UINT32 ticks);   /* one-shot; fires LAPIC_TIMER_VEC */
void   lapic_timer_stop(void);
void   lapic_eoi(void);

#endif
