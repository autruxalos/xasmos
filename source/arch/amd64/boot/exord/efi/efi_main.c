/******************************************************************************
 * XASMOS EXORD UEFI entry
 * Path: source/arch/amd64/boot/exord/efi/efi_main.c
 ******************************************************************************/
#include "efi_types.h"

extern void kernel_main(XeretHandoff *hand);

static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static UINT8        g_mmap_raw[XERET_MMAP_MAX_BYTES];
static XeretHandoff g_hand;

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static UINT64 compute_phys_end(const UINT8 *map, UINT64 map_size, UINT64 desc_size) {
    UINT64 end = 0;
    UINT64 off;

    if (desc_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return 0;
    }

    for (off = 0; off + desc_size <= map_size; off += desc_size) {
        const EFI_MEMORY_DESCRIPTOR *d =
            (const EFI_MEMORY_DESCRIPTOR *)(map + off);
        UINT64 region_end;

        switch (d->Type) {
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiConventionalMemory:
        case EfiACPIReclaimMemory:
            break;
        default:
            continue;
        }

        region_end = d->PhysicalStart + (d->NumberOfPages << 12);
        if (region_end > end) {
            end = region_end;
        }
    }
    return end;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status;
    UINTN mmap_size;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;
    int attempt;

    status = BS->LocateProtocol(&gopGuid, 0, (void **)&gop);
    if (status != EFI_SUCCESS || gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0) {
        halt_forever();
    }

    g_hand.fb.base                = gop->Mode->FrameBufferBase;
    g_hand.fb.width               = gop->Mode->Info->HorizontalResolution;
    g_hand.fb.height              = gop->Mode->Info->VerticalResolution;
    g_hand.fb.pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    g_hand.fb.pixel_format        = (UINT32)gop->Mode->Info->PixelFormat;

    for (attempt = 0; attempt < 8; attempt++) {
        mmap_size    = XERET_MMAP_MAX_BYTES;
        map_key      = 0;
        desc_size    = 0;
        desc_version = 0;

        status = BS->GetMemoryMap(&mmap_size,
                                  (EFI_MEMORY_DESCRIPTOR *)g_mmap_raw,
                                  &map_key,
                                  &desc_size,
                                  &desc_version);

        if (status == EFI_BUFFER_TOO_SMALL) {
            halt_forever();
        }
        if (status != EFI_SUCCESS) {
            halt_forever();
        }

        status = BS->ExitBootServices(ImageHandle, map_key);
        if (status == EFI_SUCCESS) {
            break;
        }
    }

    if (status != EFI_SUCCESS) {
        halt_forever();
    }

    g_hand.magic             = XERET_HANDOFF_MAGIC;
    g_hand.mmap_addr         = (UINT64)(UINTN)g_mmap_raw;
    g_hand.mmap_size         = (UINT64)mmap_size;
    g_hand.mmap_desc_size    = (UINT64)desc_size;
    g_hand.mmap_desc_version = desc_version;
    g_hand._pad              = 0;
    g_hand.phys_end          = compute_phys_end(g_mmap_raw, mmap_size, desc_size);

    kernel_main(&g_hand);
    halt_forever();
    return EFI_SUCCESS;
}
