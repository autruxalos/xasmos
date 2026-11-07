# make|amd64|uefi.mk
BUILD   := build/amd64-uefi
SRC_DIR := source
EFI_OUT := $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

SRCS := \
        arch/amd64/boot/exord/efi/efi_main.c \
        arch/amd64/kernel/xeret/main.c \
        arch/amd64/kernel/xeret/fb_console.c \
        arch/amd64/kernel/xeret/serial.c \
        arch/amd64/kernel/xeret/idt.c \
        arch/amd64/kernel/xeret/kbd.c \
        arch/amd64/kernel/xeret/kernel_lib.c \
        arch/amd64/kernel/xeret/gdt.c \
        arch/amd64/kernel/xeret/lapic.c \
        arch/amd64/kernel/xeret/proc.c \
        arch/amd64/kernel/xeret/sys.c \
        arch/amd64/kernel/xeret/load_amd64.c \
        arch/amd64/kernel/xeret/mem/physical.c \
        arch/amd64/kernel/xeret/mem/paging.c \
        arch/amd64/kernel/xeret/mem/token_ledger.c \
        arch/amd64/drivers/pci/pci.c \
        arch/amd64/init/exit/exit.c \
        arch/amd64/init/exit/stage_volumes.c \
        arch/amd64/apps/xsh/core/shell.c \
        arch/amd64/apps/xsh/utils/form.c \
        arch/amd64/apps/xsh/utils/remove.c \
        arch/amd64/apps/xsh/utils/echo.c \
        arch/amd64/apps/xsh/utils/mem.c \
        arch/amd64/apps/xsh/utils/tokens.c \
        arch/amd64/apps/xsh/utils/stat.c \
        arch/amd64/apps/xsh/utils/hexdump.c \
        arch/amd64/apps/xsh/utils/run.c \
        arch/amd64/apps/xsh/utils/pci.c \
        arch/amd64/apps/xsh/utils/nvme.c \
        arch/amd64/apps/xsh/utils/fs/mount.c \
        arch/amd64/apps/xsh/utils/fs/unmount.c \
        arch/amd64/apps/xsh/utils/fs/format.c \
        arch/amd64/apps/xsh/utils/fs/sync.c \
        arch/amd64/apps/xsh/utils/fs/install.c \
        arch/amd64/apps/xsh/images/images.c \
        arch/amd64/drivers/storage/nvme/nvme.c \
        arch/amd64/drivers/storage/storage.c \
        arch/amd64/syslib/core/path.c \
        core/xerlibc/src/mem.c \
        core/xerlibc/src/str.c \
        core/xerlibc/src/fmt.c \
        core/blk/blk.c \
        core/blk/ramdisk.c \
        core/dev/dev.c \
        core/exfs/exfs.c \
        core/exfs/gmap.c \
        core/exfs/sb.c \
        core/exb/exb.c \
        core/exb/load.c
OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
          -mno-red-zone -fno-stack-protector -fno-stack-check -Wall -Wextra \
          -I$(SRC_DIR)/kerncall/include \
          -I$(SRC_DIR)/arch/amd64/include \
          -I$(SRC_DIR)/core/xerlibc/include \
          -I$(SRC_DIR)/core/blk \
          -I$(SRC_DIR)/core/dev \
          -I$(SRC_DIR)/core/exfs \
          -I$(SRC_DIR)/arch/amd64/boot/exord/efi \
          -I$(SRC_DIR)/arch/amd64/kernel/xeret \
          -I$(SRC_DIR)/arch/amd64/init/exit \
          -I$(SRC_DIR)/arch/amd64/drivers/storage \
          -I$(SRC_DIR)/arch/amd64/apps/xsh/utils \
          -I$(SRC_DIR)/arch/amd64/apps/xsh/images

# xerlibc must not turn its own loops into calls to memcpy/memset
$(BUILD)/obj/core/xerlibc/src/%.o: CFLAGS += -fno-builtin -mllvm -disable-loop-idiom-all

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

$(EFI_OUT): $(OBJS)
	@mkdir -p $(dir $@)
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(OBJS)

.PHONY: efi clean run disk

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

disk:
	dd if=/dev/zero of=xasmos.img bs=1M count=64 status=none

run: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive format=raw,file=$(IMG) -m 256M
