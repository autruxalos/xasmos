# make/amd64/uefi.mk
BUILD       := build/amd64-uefi
ESP         := $(BUILD)/esp
EFI_OUT     := $(ESP)/EFI/BOOT/BOOTX64.EFI
SO_OUT      := $(BUILD)/xasmos.so

CC          := clang
LD          := ld.bfd
OBJCOPY     := objcopy

EXORD_EFI   := source/arch/amd64/boot/exord/efi
XERET       := source/arch/amd64/kernel/xeret

CFLAGS      := -target x86_64-unknown-none \
               -std=c11 -ffreestanding -fno-builtin \
               -fno-stack-protector -fPIC -fshort-wchar \
               -mno-red-zone -mno-sse -mno-mmx -mno-sse2 \
               -Wall -Wextra -O2 \
               -I $(EXORD_EFI) \
               -I source/kerncall/include \
               -I $(XERET)

LDS         := $(EXORD_EFI)/linker-elf.lds
LDFLAGS     := -nostdlib -shared -Bsymbolic -T $(LDS)

SRCS := \
	$(EXORD_EFI)/efi_main.c \
	$(XERET)/main.c \
	$(XERET)/fb_console.c \
	$(XERET)/mem/physical.c \
	$(XERET)/mem/paging.c \
	$(XERET)/mem/token_ledger.c

OBJS := $(patsubst source/%.c,$(BUILD)/obj/%.o,$(SRCS))

OVMF_CODE   ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS   ?= /usr/share/OVMF/OVMF_VARS.fd
QEMU        ?= qemu-system-x86_64

.PHONY: all efi image run run-uefi info clean dirs

all: efi

dirs:
	@mkdir -p 
