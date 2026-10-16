/******************************************************************************
 * XASMOS - UEFI entry point: builds XeretHandoff and hands off to xeret
 * Path: source|arch|amd64|boot|exord|efi|efi_main.c
 ******************************************************************************/
#include "efi_types.h"

extern void kernel_main(XeretHandoff *hand);

static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* Static storage for both the handoff struct and the raw memory map --
 * no dynamic allocation exists yet, and AllocatePool can't be used after
 * ExitBootServices anyway. XERET_MMAP_MAX_BYTES comes from efi_types.h so
 * the same constant governs this buffer and anyone else who cares about
 * the handoff's map capacity. */
static XeretHandoff g_hand;
static UINT8        g_mmap_buffer[XERET_MMAP_MAX_BYTES];

/* True for descriptor types that describe real, addressable RAM frames.
 * False for types that merely reserve address SPACE without backing
 * memory -- most importantly EfiMemoryMappedIO, which on real firmware
 * (and QEMU/OVMF) commonly reserves a 64-bit PCIe BAR window at a very
 * high physical address (seen here: 12 GiB at 0xFD00000000 on a 256 MiB
 * VM). Letting that drive phys_end would blow the bitmap out to its
 * 128 GiB cap for no reason, on every machine with a large 64-bit
 * MMIO aperture -- not a QEMU quirk, a general UEFI one. */
static int is_ram_backed_type(UINT32 type) {
    switch (type) {
    case EfiReservedMemoryType:
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
    case EfiUnusableMemory:
    case EfiPalCode:
        return 0;
    default:
        return 1;
    }
}

/* Finds the highest (PhysicalStart + NumberOfPages * 4K) across every
 * RAM-backed descriptor -- the bitmap must cover the full physical
 * address range of real memory the firmware reported, not MMIO windows.
 * Must walk with desc_size as the stride, never sizeof(EFI_MEMORY_DESCRIPTOR):
 * the UEFI spec allows DescriptorSize to exceed sizeof on future revisions. */
static UINT64 compute_phys_end(const UINT8 *map, UINT64 map_size, UINT64 desc_size) {
    UINT64 off, highest = 0;
    for (off = 0; off + desc_size <= map_size; off += desc_size) {
        const EFI_MEMORY_DESCRIPTOR *d = (const EFI_MEMORY_DESCRIPTOR *)(map + off);
        if (!is_ram_backed_type(d->Type)) continue;
        UINT64 end = d->PhysicalStart + d->NumberOfPages * 4096ULL;
        if (end > highest) highest = end;
    }
    return highest;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

    /* 1. Locate the Graphics Output Protocol and read its current mode. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = BS->LocateProtocol(&gopGuid, 0, (void **)&gop);
    if (status != EFI_SUCCESS || gop == 0) {
        for (;;) { __asm__ volatile ("hlt"); }  /* no GOP: nothing we can draw to */
    }

    g_hand.magic                    = XERET_HANDOFF_MAGIC;
    g_hand.fb.base                  = gop->Mode->FrameBufferBase;
    g_hand.fb.width                 = gop->Mode->Info->HorizontalResolution;
    g_hand.fb.height                = gop->Mode->Info->VerticalResolution;
    g_hand.fb.pixels_per_scanline   = gop->Mode->Info->PixelsPerScanLine;
    g_hand.fb.pixel_format          = (UINT32)gop->Mode->Info->PixelFormat;

    /* 2. Get the memory map. This MapKey is what ExitBootServices needs;
     *    any further Boot Services call (even LocateProtocol again) can
     *    invalidate it, so nothing else runs between here and step 3. */
    UINTN mmapSize = XERET_MMAP_MAX_BYTES;
    UINTN mapKey = 0, descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    status = BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)g_mmap_buffer,
                               &mapKey, &descriptorSize, &descriptorVersion);
    if (status != EFI_SUCCESS) {
        /* EFI_BUFFER_TOO_SMALL included: XERET_MMAP_MAX_BYTES isn't enough
         * on this machine. Fail loudly rather than hand off a truncated
         * map that phys_init_from_handoff would silently under-cover. */
        for (;;) { __asm__ volatile ("hlt"); }
    }

    /* 3. Leave firmware services. */
    status = BS->ExitBootServices(ImageHandle, mapKey);
    if (status != EFI_SUCCESS) {
        /* MapKey went stale (firmware updated the map between calls).
         * One retry with a fresh map/key, per the UEFI spec's own guidance. */
        mmapSize = XERET_MMAP_MAX_BYTES;
        status = BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)g_mmap_buffer,
                                   &mapKey, &descriptorSize, &descriptorVersion);
        if (status != EFI_SUCCESS) {
            for (;;) { __asm__ volatile ("hlt"); }
        }
        status = BS->ExitBootServices(ImageHandle, mapKey);
        if (status != EFI_SUCCESS) {
            for (;;) { __asm__ volatile ("hlt"); }
        }
    }

    /* 4. Firmware services are gone from here on: no more BS-> calls.
     *    Finish the handoff from the final, successfully-exited map. */
    g_hand.mmap_addr         = (UINT64)(UINTN)g_mmap_buffer;
    g_hand.mmap_size         = mmapSize;
    g_hand.mmap_desc_size    = descriptorSize;
    g_hand.mmap_desc_version = descriptorVersion;
    g_hand.phys_end          = compute_phys_end(g_mmap_buffer, mmapSize, descriptorSize);

    kernel_main(&g_hand);

    for (;;) { __asm__ volatile ("hlt"); }
    return EFI_SUCCESS;
}
