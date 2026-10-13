/******************************************************************************
 * XASMOS - Minimal UEFI type definitions (hand-written, no EDK2 dependency)
 * Path: source|arch|amd64|boot|efi|efi_types.h
 ******************************************************************************/
#ifndef XASMOS_EFI_TYPES_H
#define XASMOS_EFI_TYPES_H

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef UINT64              UINTN;
typedef long long           INTN;
typedef UINT16              CHAR16;
typedef UINT64              EFI_STATUS;
typedef void*                EFI_HANDLE;
typedef void*                EFI_EVENT;
typedef UINT8                BOOLEAN;

#define EFI_SUCCESS          0ULL
#define EFIERR(x) (0x8000000000000000ULL | (x))
#define EFI_BUFFER_TOO_SMALL EFIERR(5)

#define EFIAPI __attribute__((ms_abi))

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* --- Graphics Output Protocol --- */
typedef struct {
    UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN  SizeOfInfo;
    UINT64 FrameBufferBase;
    UINTN  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
        UINT32 ModeNumber, UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);
    void *Blt; /* unused here */
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

/* --- Memory map / Boot Services (only what we use) --- */
typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    UINT64 Signature; UINT32 Revision; UINT32 HeaderSize;
    UINT32 CRC32; UINT32 Reserved;
} EFI_TABLE_HEADER;

/* Field order and counts below are taken directly from the UEFI 2.x spec
 * table layout (indices 0-37 after Hdr). Every slot we don't call is kept
 * as void* padding of the EXACT count needed to preserve real offsets:
 *   0-3   RaiseTPL, RestoreTPL, AllocatePages, FreePages
 *   4     GetMemoryMap            <- used
 *   5-6   AllocatePool, FreePool
 *   7-25  CreateEvent .. UnloadImage                 (19 entries)
 *   26    ExitBootServices        <- used
 *   27-36 GetNextMonotonicCount .. LocateHandleBuffer (10 entries)
 *   37    LocateProtocol          <- used
 */
typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *_pad_a[4];
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
        UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
    void *_pad_b[2];
    void *_pad_c[19];
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
    void *_pad_d[10];
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    void *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;

/* --- Shared kernel handoff struct (single definition, used by efi_main.c
 *     and fb_console.c / kernel_main.c -- never redeclare this elsewhere) --- */
typedef struct {
    UINT64 base;
    UINT32 width;
    UINT32 height;
    UINT32 pixels_per_scanline;
    UINT32 pixel_format;   /* matches EFI_GRAPHICS_PIXEL_FORMAT */
} FramebufferInfo;

#endif
