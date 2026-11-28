# make/amd64/bios.mk
BUILD      := build/amd64-bios
SRC_DIR    := source
DISK       := xasmos.img
BIOS_IMG   := $(BUILD)/xasmos-bios.img
BOOT_C     := $(SRC_DIR)/arch/amd64/boot/exord/bios/exord_bios_boot.c
STAGE12_LD := $(SRC_DIR)/arch/amd64/boot/exord/bios/stage12.ld
BIOS_LD    := $(SRC_DIR)/arch/amd64/boot/exord/bios/bios.ld
ACCEL      ?= $(shell if [ -w /dev/kvm ]; then echo kvm; elif [ -w /dev/nvmm ]; then echo nvmm; else echo tcg; fi)
CLANG_RES  ?= $(if $(wildcard third_party/clang-res/21/include),third_party/clang-res/21,$(shell clang -print-resource-dir 2>/dev/null))

SRCS := \
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
	arch/amd64/kernel/xeret/xring.c \
	arch/amd64/kernel/xeret/xring_table.c \
	arch/amd64/kernel/xeret/mem/owner.c \
	arch/amd64/kernel/xeret/mem/derive_call.c \
	arch/amd64/kernel/xeret/mem/endpoint.c \
	arch/amd64/kernel/xeret/xring_boot.c \
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
	arch/amd64/drivers/storage/nvme/nvme_xq.c \
	arch/amd64/drivers/storage/nvme/nvme_q.c \
	arch/amd64/drivers/storage/storage.c \
	arch/amd64/drivers/storage/ahci/ahci_q.c \
	arch/amd64/drivers/storage/ahci/ahci.c \
	arch/amd64/drivers/storage/ide/ide_q.c \
	arch/amd64/drivers/storage/ide/ide.c \
	arch/amd64/drivers/usb/xhci/xhci_q.c \
	arch/amd64/drivers/usb/xhci/xhci.c \
	arch/amd64/drivers/usb/storage/usb_msc_q.c \
	arch/amd64/drivers/usb/storage/usb_msc.c \
	arch/amd64/drivers/usb/hid/usb_hid.c \
	arch/amd64/drivers/usb/hid/usb_hid_q.c \
	arch/amd64/drivers/net/e1000/e1000.c \
	arch/amd64/drivers/net/e1000/e1000_q.c \
	arch/amd64/drivers/audio/hda/hda.c \
	arch/amd64/drivers/audio/hda/hda_q.c \
	arch/amd64/drivers/iommu/ivrs_q.c \
	arch/amd64/boot/exord/bios/bios_handoff.c \
	arch/amd64/syslib/core/path.c \
	core/xerlibc/src/mem.c \
	core/xerlibc/src/str.c \
	core/xerlibc/src/fmt.c \
	core/blk/blk.c \
	core/blk/part.c \
	core/blk/md0.c \
	core/blk/ramdisk.c \
	core/dev/dev.c \
	core/irq/irqvec.c \
	core/xqueue/xqueue.c \
	core/exfs/exfs.c \
	core/exfs/gmap.c \
	core/exfs/sb.c \
	core/exb/exb.c \
	core/exb/load.c \
	arch/amd64/boot/exord/bios/exord_bios_main.c

OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

INCLUDES := \
	-Isource/user/include \
	-I$(NVME_SRC) \
	-I$(SRC_DIR)/arch/amd64/apps/services \
	-I$(SRC_DIR)/arch/amd64/apps/xsh/images \
	-I$(SRC_DIR)/arch/amd64/apps/xsh/utils \
	-I$(SRC_DIR)/arch/amd64/boot/exord/bios \
	-I$(SRC_DIR)/arch/amd64/boot/exord/efi \
	-I$(SRC_DIR)/arch/amd64/drivers/audio/hda \
	-I$(SRC_DIR)/arch/amd64/drivers/iommu \
	-I$(SRC_DIR)/arch/amd64/drivers/net/e1000 \
	-I$(SRC_DIR)/arch/amd64/drivers/pci \
	-I$(SRC_DIR)/arch/amd64/drivers/storage \
	-I$(SRC_DIR)/arch/amd64/drivers/storage/ahci \
	-I$(SRC_DIR)/arch/amd64/drivers/storage/ide \
	-I$(SRC_DIR)/arch/amd64/drivers/storage/nvme \
	-I$(SRC_DIR)/arch/amd64/drivers/usb/hid \
	-I$(SRC_DIR)/arch/amd64/drivers/usb/storage \
	-I$(SRC_DIR)/arch/amd64/drivers/usb/xhci \
	-I$(SRC_DIR)/arch/amd64/include \
	-I$(SRC_DIR)/arch/amd64/init/exit \
	-I$(SRC_DIR)/arch/amd64/kernel/xeret \
	-I$(SRC_DIR)/arch/amd64/kernel/xeret/mem \
	-I$(SRC_DIR)/core/blk \
	-I$(SRC_DIR)/core/crypto \
	-I$(SRC_DIR)/core/dev \
	-I$(SRC_DIR)/core/exfs \
	-I$(SRC_DIR)/core/irq \
	-I$(SRC_DIR)/core/xerlibc/include \
	-I$(SRC_DIR)/core/xqueue \
	-I$(SRC_DIR)/kerncall/include \
	-Itests/arch/amd64 \
	-Itests/core/xtok \
	-Itests/core/xtok/stubs

CFLAGS := -target x86_64-unknown-linux-gnu -ffreestanding -fshort-wchar -isystem $(CLANG_RES)/include \
	-mno-red-zone -mno-sse -mno-sse2 -mno-avx -mno-mmx -fno-stack-protector -fno-stack-check -O2 $(INCLUDES)
BOOT16_CFLAGS := -target i686-unknown-none-elf -m16 -ffreestanding -fno-pic -fno-stack-protector -Os

$(BUILD)/obj/core/xerlbc/src/%.o: CFLAGS += -fno-builtin -mllvm -disable-loop-idiom-all

all: $(BIOS_IMG)
bios: $(BIOS_IMG)

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

$(BIOS_IMG): $(BOOT_C) $(STAGE12_LD) $(BIOS_LD) $(OBJS)
	@mkdir -p $(BUILD)
	clang $(BOOT16_CFLAGS) -c $(BOOT_C) -o $(BUILD)/mbr.o
	ld.lld -m elf_i386 -T $(STAGE12_LD) $(BUILD)/mbr.o -o $(BUILD)/mbr.elf && llvm-objcopy -O binary --only-section=.boot --only-section=.boot.text --only-section=.text --only-section=.rodata $(BUILD)/mbr.elf $(BUILD)/mbr.bin
	clang -O2 tools/mkbiosimg.c -o $(BUILD)/mkbiosimg && $(BUILD)/mkbiosimg $(BUILD)/mbr.bin $(BUILD)/kern.bin $@
	ld.lld -m elf_x86_64 --oformat binary -T $(BIOS_LD) $(OBJS) -o $(BUILD)/kern.bin

clean:
	rm -rf $(BUILD)

qemu-run: $(BIOS_IMG)
	qemu-system-x86_64 -machine q35 -cpu qemu64 -accel $(ACCEL) -m 512M -boot order=c \
	  -drive file=$(BIOS_IMG),format=raw,if=none,id=bootdrv -device ide-hd,drive=bootdrv,bootindex=1 \
	  -drive file=$(DISK),format=raw,if=none,id=nvm \
	  -device nvme,drive=nvm,serial=xasmos \
	  -device qemu-xhci,id=xhci -serial stdio
