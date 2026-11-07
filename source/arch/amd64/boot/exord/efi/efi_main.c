/******************************************************************************
 * XASMOS - UEFI entry point: builds XeretHandoff and hands off to xeret
 * Path: source/arch/amd64/boot/exord/efi/efi_main.c
 *
 * Early diagnostics: every step writes one line to COM1 (115200 8N1) with a
 * code, so a failure before the kernel is visible. Port I/O needs no firmware
 * or kernel service, so it works before and after ExitBootServices.
 ******************************************************************************/
#include "efi_types.h"

extern void kernel_main(XeretHandoff *hand);

static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* Static storage for the handoff struct and the raw memory map: no dynamic
 * allocation exists, and AllocatePool can't be used after ExitBootServices. */
static XeretHandoff g_hand;
static UINT8        g_mmap_buffer[XERET_MMAP_MAX_BYTES];

/* ---- early serial (COM1 only) ------------------------------------------- */

#define BOOT_COM1 0x3F8

static inline void boot_outb(UINT16 port, UINT8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline UINT8 boot_inb(UINT16 port) {
    UINT8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void boot_serial_init(void) {
    boot_outb(BOOT_COM1 + 1, 0x00);   /* no interrupts */
    boot_outb(BOOT_COM1 + 3, 0x80);   /* DLAB on */
    boot_outb(BOOT_COM1 + 0, 0x01);   /* divisor 1: 115200 baud */
    boot_outb(BOOT_COM1 + 1, 0x00);
    boot_outb(BOOT_COM1 + 3, 0x03);   /* 8 bits, no parity, 1 stop */
    boot_outb(BOOT_COM1 + 2, 0xC7);   /* FIFO on and cleared */
    boot_outb(BOOT_COM1 + 4, 0x0B);   /* DTR, RTS, OUT2 */
}

/* Bounded wait for the transmit holding register: a missing UART must not
 * hang the boot. */
static void boot_putc(char c) {
    UINT32 spin;
    for (spin = 0; spin < 100000u; spin++) {
        if (boot_inb(BOOT_COM1 + 5) & 0x20) break;
    }
    boot_outb(BOOT_COM1, (UINT8)c);
}

static void boot_puts(const char *s) {
    while (*s) {
        if (*s == '\n') boot_putc('\r');
        boot_putc(*s++);
    }
}

/* Writes v as digits hexadecimal digits, prefixed with 0x. */
static void boot_hex(UINT64 v, int digits) {
    static const char h[] = "0123456789ABCDEF";
    char b[17];
    int i;
    for (i = digits - 1; i >= 0; i--) {
        b[i] = h[v & 0xF];
        v >>= 4;
    }
    b[digits] = '\0';
    boot_puts("0x");
    boot_puts(b);
}

/* "EXBOOT: <code> <what> <detail>" with detail in hex. */
static void boot_stage(const char *code, const char *what, UINT64 detail) {
    boot_puts("EXBOOT: ");
    boot_puts(code);
    boot_puts(" ");
    boot_puts(what);
    boot_puts(" ");
    boot_hex(detail, 16);
    boot_puts("\n");
}

/* A failed boot step: report it, then stop. Interrupts stay off. */
static void boot_fail(const char *code, const char *what, UINT64 detail)
    __attribute__((noreturn));

static void boot_fail(const char *code, const char *what, UINT64 detail) {
    boot_puts("EXBOOT: FAILED at ");
    boot_stage(code, what, detail);
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* ---- memory map helpers -------------------------------------------------- */

/* True for descriptor types that describe real, addressable RAM frames.
 * False for types that merely reserve address SPACE without backing memory,
 * most importantly EfiMemoryMappedIO: a 64-bit PCIe BAR window at a very high
 * physical address would otherwise drive phys_end to the bitmap's cap. */
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

/* Highest (PhysicalStart + NumberOfPages * 4K) over RAM-backed descriptors.
 * The stride is desc_size, never sizeof(EFI_MEMORY_DESCRIPTOR): the UEFI spec
 * allows DescriptorSize to exceed sizeof on future revisions. */
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

/* ---- entry point --------------------------------------------------------- */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

    boot_serial_init();
    boot_stage("E00", "start", 0);

    /* 1. Graphics Output Protocol: the framebuffer the kernel draws to. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = BS->LocateProtocol(&gopGuid, 0, (void **)&gop);
    if (status != EFI_SUCCESS || gop == 0) {
        boot_fail("E01", "gop", status);
    }
    boot_stage("E01", "gop ok", (UINT64)(UINTN)gop->Mode->FrameBufferBase);

    g_hand.magic                  = XERET_HANDOFF_MAGIC;
    g_hand.fb.base                = gop->Mode->FrameBufferBase;
    g_hand.fb.width               = gop->Mode->Info->HorizontalResolution;
    g_hand.fb.height              = gop->Mode->Info->VerticalResolution;
    g_hand.fb.pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    g_hand.fb.pixel_format        = (UINT32)gop->Mode->Info->PixelFormat;

    /* 2. Where this image sits in memory: the kernel reserves it, so the
     *    allocator never hands out its .text, .data or .bss. */
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    status = BS->HandleProtocol(ImageHandle, &loadedImageGuid, (void **)&li);
    if (status != EFI_SUCCESS || li == 0) {
        boot_fail("E02", "image info", status);
    }
    g_hand.image_base = (UINT64)(UINTN)li->ImageBase;
    g_hand.image_size = li->ImageSize;
    boot_stage("E02", "image", g_hand.image_size);

    /* 3. Memory map. Its MapKey is what ExitBootServices needs; nothing else
     *    may call Boot Services between here and step 4. */
    UINTN mmapSize = XERET_MMAP_MAX_BYTES;
    UINTN mapKey = 0, descriptorSize = 0;
    UINT32 descriptorVersion = 0;

    status = BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)g_mmap_buffer,
                               &mapKey, &descriptorSize, &descriptorVersion);
    if (status != EFI_SUCCESS) {
        /* EFI_BUFFER_TOO_SMALL: XERET_MMAP_MAX_BYTES is too small here. The
         * size the firmware asks for is reported, instead of handing off a
         * truncated map that would under-cover the physical memory. */
        boot_stage("E03", "memory map need", (UINT64)mmapSize);
        boot_fail("E03", "memory map", status);
    }
    boot_stage("E03", "memory map ok", (UINT64)mmapSize);

    /* 4. Leave firmware services. */
    status = BS->ExitBootServices(ImageHandle, mapKey);
    if (status != EFI_SUCCESS) {
        /* The key went stale: the firmware changed the map between calls.
         * One retry with a fresh map and key, as the UEFI spec advises. */
        boot_stage("E04", "exit boot services retry", status);

        mmapSize = XERET_MMAP_MAX_BYTES;
        status = BS->GetMemoryMap(&mmapSize, (EFI_MEMORY_DESCRIPTOR *)g_mmap_buffer,
                                   &mapKey, &descriptorSize, &descriptorVersion);
        if (status != EFI_SUCCESS) {
            boot_stage("E05", "memory map retry need", (UINT64)mmapSize);
            boot_fail("E05", "memory map retry", status);
        }

        status = BS->ExitBootServices(ImageHandle, mapKey);
        if (status != EFI_SUCCESS) {
            boot_fail("E06", "exit boot services", status);
        }
    }
    boot_stage("E04", "exit boot services ok", 0);

    /* 5. Firmware services are gone: no more BS-> calls. Finish the handoff
     *    from the final, successfully exited map. */
    g_hand.mmap_addr         = (UINT64)(UINTN)g_mmap_buffer;
    g_hand.mmap_size         = mmapSize;
    g_hand.mmap_desc_size    = descriptorSize;
    g_hand.mmap_desc_version = descriptorVersion;
    g_hand.phys_end          = compute_phys_end(g_mmap_buffer, mmapSize, descriptorSize);

    boot_stage("E07", "handoff to kernel", g_hand.phys_end);
    kernel_main(&g_hand);

    for (;;) { __asm__ volatile ("hlt"); }
    return EFI_SUCCESS;
}
