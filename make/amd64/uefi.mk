# make|amd64|uefi.mk
BUILD   := build/amd64-uefi
SRC_DIR := source
EFI_OUT := $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

SRCS := arch/amd64/boot/exord/efi/efi_main.c \
        arch/amd64/kernel/xeret/main.c \
        arch/amd64/kernel/xeret/fb_console.c \
        arch/amd64/kernel/xeret/mem/physical.c \
        arch/amd64/kernel/xeret/mem/paging.c \
        arch/amd64/kernel/xeret/mem/token_ledger.c
OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
          -mno-red-zone -fno-stack-protector -fno-stack-check -Wall -Wextra \
          -I$(SRC_DIR)/kerncall/include \
          -I$(SRC_DIR)/arch/amd64/boot/exord/efi \
          -I$(SRC_DIR)/arch/amd64/kernel/xeret

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

$(EFI_OUT): $(OBJS)
	@mkdir -p $(dir $@)
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(OBJS)

.PHONY: efi clean run

efi: $(EFI_OUT)

clean:
	rm -rf $(BUILD)

IMG  := $(BUILD)/esp.img
OVMF ?= /usr/share/edk2/x64/OVMF.4m.fd

$(IMG): $(EFI_OUT)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mkfs.fat -F 32 $@ >/dev/null
	mmd   -i $@ ::/EFI ::/EFI/BOOT
	mcopy -i $@ $(EFI_OUT) ::/EFI/BOOT/BOOTX64.EFI

run: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive format=raw,file=$(IMG) -m 256M
