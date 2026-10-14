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
	@mkdir -p $(BUILD)/obj/arch/amd64/boot/exord/efi
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
	@dd if=/dev/zero of=$(BUILD)/xasmos-esp.img bs=1M count=32 status=none
	@mkfs.vfat -F 32 -n XASMOS $(BUILD)/xasmos-esp.img >/dev/null
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI 2>/dev/null || true
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI/BOOT 2>/dev/null || true
	@mcopy -o -i $(BUILD)/xasmos-esp.img $(EFI_OUT) ::/EFI/BOOT/BOOTX64.EFI
	@echo "[IMG] $(BUILD)/xasmos-esp.img"

run run-uefi: image
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

clean:
	rm -rf $(BUILD)
