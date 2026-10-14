# =============================================================================
# XASMOS — Master build (amd64, UEFI / EXORD → xeret)
# =============================================================================
# ONE makefile. Official boot path: UEFI + GOP → BOOTX64.EFI
# Multiboot2 / -kernel ELF is not used until reintroduced cleanly.
#
#   make            → BOOTX64.EFI
#   make image      → FAT ESP disk
#   make run-uefi   → QEMU + OVMF
#   make clean
# =============================================================================

ARCH        ?= amd64
BUILD       := build/$(ARCH)
ESP         := $(BUILD)/esp
EFI_OUT     := $(ESP)/EFI/BOOT/BOOTX64.EFI
SO_OUT      := $(BUILD)/xasmos.so

CC          := clang
LD          := ld.bfd
OBJCOPY     := objcopy

CFLAGS      := -target x86_64-unknown-none \
               -std=c11 -ffreestanding -fno-builtin \
               -fno-stack-protector -fPIC -fshort-wchar \
               -mno-red-zone -mno-sse -mno-mmx -mno-sse2 \
               -Wall -Wextra -O2 \
               -I source/arch/amd64/boot/efi \
               -I source/kerncall/include \
               -I source/arch/amd64/kernel/xeret

LDS         := source/arch/amd64/boot/efi/linker-elf.lds
LDFLAGS     := -nostdlib -shared -Bsymbolic -T $(LDS)

# --- Only sources that exist and link into the EFI image today ---
SRCS := \
	source/arch/amd64/boot/efi/efi_main.c \
	source/arch/amd64/kernel/xeret/main.c \
	source/arch/amd64/kernel/xeret/fb_console.c \
	source/arch/amd64/kernel/xeret/mem/physical.c \
	source/arch/amd64/kernel/xeret/mem/token_ledger.c \
	source/arch/amd64/kernel/xeret/mem/paging.c

OBJS := $(patsubst source/%.c,$(BUILD)/obj/%.o,$(SRCS))

OVMF_CODE   ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS   ?= /usr/share/OVMF/OVMF_VARS.fd
QEMU        ?= qemu-system-x86_64

.PHONY: all efi image run-uefi info clean help

all: efi

help:
	@echo "XASMOS master build (UEFI amd64)"
	@echo "  make / make efi   -> $(EFI_OUT)"
	@echo "  make image        -> $(BUILD)/xasmos-esp.img"
	@echo "  make run-uefi     -> QEMU + OVMF"
	@echo "  make clean"
	@echo "Override OVMF: make run-uefi OVMF_CODE=/path/OVMF_CODE.fd"

dirs:
	@mkdir -p $(BUILD)/obj/arch/amd64/boot/efi
	@mkdir -p $(BUILD)/obj/arch/amd64/kernel/xeret/mem
	@mkdir -p $(ESP)/EFI/BOOT

$(BUILD)/obj/%.o: source/%.c | dirs
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(SO_OUT): $(OBJS) $(LDS) | dirs
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(EFI_OUT): $(SO_OUT) | dirs
	@echo "[OBJCOPY] $@"
	@$(OBJCOPY) --target=efi-app-x86_64 $< $@
	@echo "      OK $$(wc -c < $@) bytes"

efi: $(EFI_OUT)

image: $(EFI_OUT)
	@echo "[IMG] $(BUILD)/xasmos-esp.img"
	@dd if=/dev/zero of=$(BUILD)/xasmos-esp.img bs=1M count=32 status=none
	@mkfs.vfat -F 32 -n XASMOS $(BUILD)/xasmos-esp.img >/dev/null
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI 2>/dev/null || true
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI/BOOT 2>/dev/null || true
	@mcopy -o -i $(BUILD)/xasmos-esp.img $(EFI_OUT) ::/EFI/BOOT/BOOTX64.EFI
	@echo "      OK"

run-uefi: image
	@test -f "$(OVMF_CODE)" || (echo "Missing OVMF_CODE=$(OVMF_CODE)"; exit 1)
	@test -f "$(OVMF_VARS)" || (echo "Missing OVMF_VARS=$(OVMF_VARS)"; exit 1)
	@cp -f "$(OVMF_VARS)" $(BUILD)/OVMF_VARS.fd
	$(QEMU) -machine q35 -m 512M -cpu qemu64 \
	  -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,file=$(BUILD)/OVMF_VARS.fd \
	  -drive format=raw,file=$(BUILD)/xasmos-esp.img \
	  -net none -serial stdio

info: efi
	@ls -la $(EFI_OUT) $(SO_OUT)
	@od -An -tx1 -N16 $(EFI_OUT)

clean:
	rm -rf build bin
	@echo "[CLEAN] OK"
