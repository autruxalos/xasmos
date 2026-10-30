# make|amd64|uefi.mk
BUILD   := build/amd64-uefi
SRC_DIR := source
EFI_OUT := $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

SRCS := arch/amd64/boot/exord/efi/efi_main.c \
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
        arch/amd64/kernel/xeret/mem/physical.c \
        arch/amd64/kernel/xeret/mem/paging.c \
        arch/amd64/kernel/xeret/mem/token_ledger.c \
        arch/amd64/init/exit.c \
        arch/amd64/apps/xsh/core/shell.c \
        arch/amd64/apps/xsh/utils/form.c \
        arch/amd64/apps/xsh/utils/remove.c \
        arch/amd64/syslib/core/path.c \
        arch/amd64/drivers/storage/exfs.c
OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
          -mno-red-zone -fno-stack-protector -fno-stack-check -Wall -Wextra \
          -I$(SRC_DIR)/kerncall/include \
          -I$(SRC_DIR)/arch/amd64/include \
          -I$(SRC_DIR)/arch/amd64/boot/exord/efi \
          -I$(SRC_DIR)/arch/amd64/kernel/xeret

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

$(EFI_OUT): $(OBJS)
	@mkdir -p $(dir $@)
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(OBJS)

TEST_EXFS_OBJS := $(filter-out $(BUILD)/obj/arch/amd64/kernel/xeret/main.o,$(OBJS)) \
                  $(BUILD)/obj/tests/arch/amd64/exfs/exfs_test_main.o

test-exfs: $(TEST_EXFS_OBJS)
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib \
          /out:$(BUILD)/test-exfs.efi $^

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
