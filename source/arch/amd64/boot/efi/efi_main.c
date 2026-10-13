/******************************************************************************
 * XASMOS - Minimal UEFI entry point (x86_64, no EDK2/gnu-efi dependency)
 * Path: source|arch|amd64|boot|efi|efi_main.c
 ******************************************************************************/
#include "efi_types.h"

/* Framebuffer handoff struct passed to kernel_main. Kernel code (text
 * blitter, EXFS, etc.) must be rebuilt against this instead of 0xB8000. */
typedef struct {
    UINT64 base;
    UINT32 width;
    UINT32 height;
    UINT32 pixels_per_scanline;
    UINT32 pixel_format;   /* matches EFI_GRAPHICS_PIXEL_FORMAT */
} FramebufferInfo;

extern void kernel_main(FramebufferInfo *fb);

static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* Static buffer for the memory map -- avoids needing AllocatePool for this. */
#define MAX_MMAP_BYTES 16384
static UINT8 mmap_buffer[MAX_MMAP_BYTES];

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

    /* 1. Locate the Graphics Output Protocol and read its current mode. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = BS->LocateProtocol(&gopGuid, 0, (void **)&gop);
    if (status != EFI_SUCCESS || gop == 0) {
        /* No GOP available -- nothing we can draw to. Halt visibly. */
        for (;;) { __asm__ volatile ("hlt"); }
    }

    FramebufferInfo fb;
    fb.base                 = gop->Mode->FrameBufferBase;
    fb.width                = gop->Mode->Info->HorizontalResolution;
    fb.height                = gop->Mode->Info->VerticalResolution;
    fb.pixels_per_scanline    = gop->Mode->Info->PixelsPerScanLine;
    fb.pixel_format           = (UINT32)gop->Mode->Info->PixelFormat;

    /* 2. Get the memory map -- required to call ExitBootServices, and the
     *    MapKey from THIS exact call must match the one we pass below. */
    UINTN mmapSize = MAX_MMAP_BYTES;
    UINTN mapKey = 0, descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    status = BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)mmap_buffer,
                               &mapKey, &descriptorSize, &descriptorVersion);
    if (status != EFI_SUCCESS) {
        for (;;) { __asm__ volatile ("hlt"); }
    }

    /* 3. Leave firmware services -- no more Boot Services calls after this. */
    status = BS->ExitBootServices(ImageHandle, mapKey);
    if (status != EFI_SUCCESS) {
        /* MapKey went stale (firmware updated the map between calls).
         * Minimal recovery: retry once with a fresh map. Real kernel
         * should loop this properly; one retry is enough to get moving. */
        mmapSize = MAX_MMAP_BYTES;
        BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)mmap_buffer,
                          &mapKey, &descriptorSize, &descriptorVersion);
        BS->ExitBootServices(ImageHandle, mapKey);
    }

    /* 4. Firmware services are gone. Hand off to the exokernel. */
    kernel_main(&fb);

    for (;;) { __asm__ volatile ("hlt"); }
    return EFI_SUCCESS;
}
