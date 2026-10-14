/******************************************************************************
 * XASMOS - Minimal UEFI types + XeretHandoff (no EDK2)
 * Path: source/arch/amd64/boot/exord/efi/efi_types.h
 ******************************************************************************/
#ifndef XASMOS_EFI_TYPES_H
#define XASMOS_EFI_TYPES_H

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef UINT64             UINTN;
typedef long long          INTN;
typedef UINT16             CHAR16;
typedef UINT64             EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef void              *EFI_EVENT;
typedef UINT8              BOOLEAN;

#define EFI_SUCCESS          0ULL
#define EFIERR(x)            (0x8000000000000000ULL | (UINT64)(x))
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFIAPI               __attribute__((ms_abi))

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask                          = 2,
    PixelBltOnly                          = 3,
    PixelFormatMax                        = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                      Version;
    UINT32                      HorizontalResolution;
    UINT32                      VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT   PixelFormat;
    EFI_PIXEL_BITMASK           PixelInformation;
    UINT32                      PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                  MaxMode;
    UINT32                                  Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   *Info;
    UINTN                                   SizeOfInfo;
    UINT64                                  FrameBufferBase;
    UINTN                                   FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
                                   UINT32 ModeNumber, UINTN *SizeOfInfo,
                                   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
                                 UINT32 ModeNumber);
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EfiReservedMemoryType       0
#define EfiLoaderCode               1
#define EfiLoaderData               2
#define EfiBootServicesCode         3
#define EfiBootServicesData         4
#define EfiRuntimeServicesCode      5
#define EfiRuntimeServicesData      6
#define EfiConventionalMemory       7
#define EfiUnusableMemory           8
#define EfiACPIReclaimMemory        9
#define EfiACPIMemoryNVS            10
#define EfiMemoryMappedIO           11
#define EfiMemoryMappedIOPortSpace  12
#define EfiPalCode                  13
#define EfiPersistentMemory         14

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/*
 * Boot Services after Hdr: 0-3 TPL/pages, 4 GetMemoryMap, 5-6 pool,
 * 7-25 events..UnloadImage, 26 ExitBootServices, 27-36 ..., 37 LocateProtocol
 */
typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *_pad_a[4];
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                      EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                      UINTN *MapKey,
                                      UINTN *DescriptorSize,
                                      UINT32 *DescriptorVersion);
    void *_pad_b[2];
    void *_pad_c[19];
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
    void *_pad_d[10];
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol,
                                        void *Registration,
                                        void **Interface);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER   Hdr;
    CHAR16            *FirmwareVendor;
    UINT32             FirmwareRevision;
    EFI_HANDLE         ConsoleInHandle;
    void              *ConIn;
    EFI_HANDLE         ConsoleOutHandle;
    void              *ConOut;
    EFI_HANDLE         StandardErrorHandle;
    void              *StdErr;
    void              *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;

typedef struct {
    UINT64 base;
    UINT32 width;
    UINT32 height;
    UINT32 pixels_per_scanline;
    UINT32 pixel_format;
} FramebufferInfo;

#define XERET_HANDOFF_MAGIC  0x5845524554484E44ULL
#define XERET_MMAP_MAX_BYTES 65536

typedef struct {
    UINT64              magic;
    FramebufferInfo     fb;
    UINT64              mmap_addr;
    UINT64              mmap_size;
    UINT64              mmap_desc_size;
    UINT32              mmap_desc_version;
    UINT32              _pad;
    UINT64              phys_end;
} XeretHandoff;

#endif /* XASMOS_EFI_TYPES_H */
