# make/amd64/uefi.mk
BUILD   := build/amd64-uefi
SRC_DIR := source
EFI_OUT := $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
IMG     := $(BUILD)/esp.img
DISK    := xasmos.img
OVMF    ?= /usr/share/edk2/x64/OVMF.4m.fd

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
        arch/amd64/kernel/xeret/xring.c \
        arch/amd64/kernel/xeret/xring_table.c \
        arch/amd64/kernel/xeret/mem/owner.c \
        arch/amd64/kernel/xeret/mem/derive_call.c \
        arch/amd64/kernel/xeret/mem/derive_call.c \
        arch/amd64/kernel/xeret/mem/derive_call.c \
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
        arch/amd64/syslib/core/path.c \
        core/xerlibc/src/mem.c \
        core/xerlibc/src/str.c \
        core/xerlibc/src/fmt.c \
        core/blk/blk.c \
        core/blk/ramdisk.c \
        core/dev/dev.c \
        core/irq/irqvec.c \
        core/xqueue/xqueue.c \
        core/exfs/exfs.c \
        core/exfs/gmap.c \
        core/exfs/sb.c \
        core/exb/exb.c \
        core/exb/load.c
OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

ACCEL ?= kvm
LLD_LINK ?= lld-link
CLANG_RES ?= $(shell clang -print-resource-dir)
CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
          -isystem $(CLANG_RES)/include \
          -mno-red-zone -fno-stack-protector -fno-stack-check -Wall -Wextra \
          -I$(SRC_DIR)/kerncall/include \
          -I$(SRC_DIR)/arch/amd64/include \
          -I$(SRC_DIR)/core/xerlibc/include \
          -I$(SRC_DIR)/core/blk \
          -I$(SRC_DIR)/core/dev \
          -I$(SRC_DIR)/core/irq \
          -I$(SRC_DIR)/core/xqueue \
          -I$(SRC_DIR)/core/exfs \
          -I$(SRC_DIR)/arch/amd64/boot/exord/efi \
          -I$(SRC_DIR)/arch/amd64/kernel/xeret \
          -I$(SRC_DIR)/arch/amd64/init/exit \
          -I$(SRC_DIR)/arch/amd64/drivers/storage \
          -I$(SRC_DIR)/arch/amd64/drivers/pci \
          -I$(SRC_DIR)/arch/amd64/apps/xsh/utils \
          -I$(SRC_DIR)/arch/amd64/apps/xsh/images

# images.c includes the embedded program headers: rebuild it when they change
$(BUILD)/obj/arch/amd64/apps/xsh/images/images.o: $(SRC_DIR)/arch/amd64/apps/xsh/images/prog_exb.h \
    $(SRC_DIR)/arch/amd64/apps/xsh/images/queue_prog_exb.h \
    $(SRC_DIR)/arch/amd64/apps/xsh/images/nd0_prog_exb.h \
    $(SRC_DIR)/arch/amd64/apps/xsh/images/derive_prog_exb.h

# xerlibc must not turn its own loops into calls to memcpy/memset
$(BUILD)/obj/core/xerlibc/src/%.o: CFLAGS += -fno-builtin -mllvm -disable-loop-idiom-all

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -c $< -o $@

$(EFI_OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(OBJS)

# ESP image: built from zero by tools/make-esp.sh, which also verifies it.
$(IMG): $(EFI_OUT) tools/make-esp.sh
	tools/make-esp.sh $(EFI_OUT) $(IMG)

# NVMe test disk. Created only if missing: its content is persistence data.
$(DISK):
	dd if=/dev/zero of=$@ bs=1M count=64 status=none

# Recreate the test disk on purpose (erases its content).
disk:
	dd if=/dev/zero of=$(DISK) bs=1M count=64 status=none

# Full boot: EFI image plus the NVMe test disk. Always use this, never a
# boot without -device nvme, or the result does not represent the system.
# NVRAM for UEFI: a writable copy, created empty on first use.
$(BUILD)/OVMF_VARS.fd:
	mkdir -p $(BUILD)
	dd if=/dev/zero of=$@ bs=1k count=528 status=none

qemu: $(IMG) $(DISK) $(BUILD)/OVMF_VARS.fd
	qemu-system-x86_64 -machine q35 -cpu qemu64 -accel $(ACCEL) -m 512M \
	  -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
	  -drive if=pflash,format=raw,file=$(BUILD)/OVMF_VARS.fd \
	  -drive format=raw,file=$(IMG) \
	  -drive file=$(DISK),format=raw,if=none,id=nvm \
	  -device nvme,drive=nvm,serial=xasmos \
	  -serial stdio

# Host tests for the portable modules and the kernel-side ring logic.
HOST_SAN ?= -fsanitize=address,undefined
HOST_CC  := clang -std=c11 -Wall -Wextra -g $(HOST_SAN)
HOST_BIN   := build/host

host-tests: $(HOST_BIN)/xtok_blk_test $(HOST_BIN)/xtok_pin_test $(HOST_BIN)/xring_test \
            $(HOST_BIN)/irqvec_test $(HOST_BIN)/irqvec_mt_test $(HOST_BIN)/xqueue_test $(HOST_BIN)/nvme_xq_test $(HOST_BIN)/nvme_q_test $(HOST_BIN)/nvme_submit_test $(HOST_BIN)/nvme_route_test $(HOST_BIN)/xqueue_mt_test $(HOST_BIN)/nvme_busy_test $(HOST_BIN)/xring_inflight_test $(HOST_BIN)/xring_close_test $(HOST_BIN)/xring_table_test $(HOST_BIN)/xring_reentry_test $(HOST_BIN)/grant_range_test $(HOST_BIN)/sha512_test $(HOST_BIN)/owner_test $(HOST_BIN)/give_test $(HOST_BIN)/boot_init_test $(HOST_BIN)/derive_call_test
	@for t in $^; do echo "== $$t"; ./$$t || exit 1; done

$(HOST_BIN):
	@mkdir -p $(HOST_BIN)

$(HOST_BIN)/xtok_blk_test: $(HOST_BIN) tests/core/xtok/xtok_blk_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c \
    $(wildcard tests/core/xtok/stubs/*.h)
	$(HOST_CC) -Itests/core/xtok/stubs -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk \
	  tests/core/xtok/xtok_blk_test.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c \
	  -o $@

$(HOST_BIN)/xtok_pin_test: $(HOST_BIN) tests/core/xtok/xtok_pin_test.c tests/core/xtok/host_stubs.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c
	$(HOST_CC) -Itests/core/xtok/stubs -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -Itests/core/xtok \
	  tests/core/xtok/xtok_pin_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c \
	  -o $@

$(HOST_BIN)/xring_test: $(HOST_BIN) tests/core/xring/xring_test.c tests/core/xtok/host_stubs.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/xring.c $(SRC_DIR)/core/xqueue/xqueue.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c
	$(HOST_CC) -Itests/core/xtok/stubs -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk \
	  -I$(SRC_DIR)/core/xqueue -Itests/core/xtok \
	  tests/core/xring/xring_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c \
	  -o $@

$(HOST_BIN)/irqvec_test: $(HOST_BIN) tests/core/irq/irqvec_test.c $(SRC_DIR)/core/irq/irqvec.c
	$(HOST_CC) -I$(SRC_DIR)/core/irq tests/core/irq/irqvec_test.c $(SRC_DIR)/core/irq/irqvec.c -o $@

$(HOST_BIN)/irqvec_mt_test: $(HOST_BIN) tests/core/irq/irqvec_mt_test.c $(SRC_DIR)/core/irq/irqvec.c
	clang -std=c11 -O2 -pthread -I$(SRC_DIR)/core/irq \
	  tests/core/irq/irqvec_mt_test.c $(SRC_DIR)/core/irq/irqvec.c -o $@

$(HOST_BIN)/xqueue_test: $(HOST_BIN) tests/core/xqueue/xqueue_test.c $(SRC_DIR)/core/xqueue/xqueue.c
	$(HOST_CC) -I$(SRC_DIR)/core/xqueue tests/core/xqueue/xqueue_test.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c -o $@


$(HOST_BIN)/nvme_xq_test: $(HOST_BIN) tests/core/nvme/nvme_xq_test.c \
    $(SRC_DIR)/arch/amd64/drivers/storage/nvme/nvme_xq.c $(SRC_DIR)/core/xqueue/xqueue.h
	$(HOST_CC) -I$(SRC_DIR)/arch/amd64/drivers/storage/nvme -I$(SRC_DIR)/core/xqueue \
	  tests/core/nvme/nvme_xq_test.c $(SRC_DIR)/arch/amd64/drivers/storage/nvme/nvme_xq.c \
	  -o $@

$(HOST_BIN)/nvme_q_test: $(HOST_BIN) tests/core/nvme/nvme_q_test.c \
    $(SRC_DIR)/arch/amd64/drivers/storage/nvme/nvme_q.c $(SRC_DIR)/arch/amd64/drivers/storage/nvme/nvme_q.h
	$(HOST_CC) -I$(SRC_DIR)/arch/amd64/drivers/storage/nvme \
	  tests/core/nvme/nvme_q_test.c $(SRC_DIR)/arch/amd64/drivers/storage/nvme/nvme_q.c \
	  -o $@

NVME_SRC := $(SRC_DIR)/arch/amd64/drivers/storage/nvme
NVME_TEST_INC := -I$(NVME_SRC) -I$(SRC_DIR)/arch/amd64/drivers/storage \
    -I$(SRC_DIR)/arch/amd64/drivers/pci -I$(SRC_DIR)/kerncall/include -I$(SRC_DIR)/arch/amd64/include \
    -I$(SRC_DIR)/core/xerlibc/include -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/core/dev -I$(SRC_DIR)/core/irq \
    -I$(SRC_DIR)/core/xqueue -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/arch/amd64/kernel/xeret \
    -Itests/core/xtok/stubs

# Every kernel symbol nvme.c needs becomes a stub that aborts. The test must
# never reach one: if it does, the run stops at once instead of lying.
$(HOST_BIN)/nvme_stubs.c: $(NVME_SRC)/nvme.c
	@mkdir -p $(HOST_BIN)
	clang -std=c11 -c $(NVME_TEST_INC) $< -o $(HOST_BIN)/nvme_probe.o
	rexx tools/gen-stubs.rexx $(HOST_BIN)/nvme_probe.o > $@

$(HOST_BIN)/nvme_submit_test: $(HOST_BIN) tests/core/nvme/nvme_submit_test.c $(HOST_BIN)/nvme_stubs.c \
    $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c
	$(HOST_CC) $(NVME_TEST_INC) tests/core/nvme/nvme_submit_test.c $(HOST_BIN)/nvme_stubs.c \
	  $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c -o $@

$(HOST_BIN)/nvme_route_test: $(HOST_BIN) tests/core/nvme/nvme_route_test.c $(HOST_BIN)/nvme_stubs.c \
    $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c
	$(HOST_CC) $(NVME_TEST_INC) tests/core/nvme/nvme_route_test.c $(HOST_BIN)/nvme_stubs.c \
	  $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c -o $@

$(HOST_BIN)/xqueue_mt_test: $(HOST_BIN) tests/core/xqueue/xqueue_mt_test.c $(SRC_DIR)/core/xqueue/xqueue.c
	clang -std=c11 -O2 -pthread -I$(SRC_DIR)/core/xqueue \
	  tests/core/xqueue/xqueue_mt_test.c $(SRC_DIR)/core/xqueue/xqueue.c -o $@

# Stubs for the busy test exclude what it links for real (ledger, block layer).
$(HOST_BIN)/nvme_stubs_busy.c: $(NVME_SRC)/nvme.c
	@mkdir -p $(HOST_BIN)
	clang -std=c11 -c $(NVME_TEST_INC) $< -o $(HOST_BIN)/nvme_probe_busy.o
	rexx tools/gen-stubs.rexx $(HOST_BIN)/nvme_probe_busy.o "xtok_ phys_ blk_" > $@

$(HOST_BIN)/nvme_busy_test: $(HOST_BIN) tests/core/nvme/nvme_busy_test.c $(HOST_BIN)/nvme_stubs_busy.c \
    tests/core/xtok/host_stubs.c $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c
	$(HOST_CC) $(NVME_TEST_INC) -Itests/core/xtok tests/core/nvme/nvme_busy_test.c \
	  $(HOST_BIN)/nvme_stubs_busy.c tests/core/xtok/host_stubs.c $(NVME_SRC)/nvme_q.c $(NVME_SRC)/nvme_xq.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/xring_inflight_test: $(HOST_BIN) tests/core/xring/xring_inflight_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/xring.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/core/xqueue \
	  tests/core/xring/xring_inflight_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/xring_close_test: $(HOST_BIN) tests/core/xring/xring_close_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/xring.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/core/xqueue \
	  tests/core/xring/xring_close_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/xring_table_test: $(HOST_BIN) tests/core/xring/xring_table_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/xring.c $(SRC_DIR)/arch/amd64/kernel/xeret/xring_table.c \
    tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/core/xqueue \
	  tests/core/xring/xring_table_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/xring_reentry_test: $(HOST_BIN) tests/core/xring/xring_reentry_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/xring.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/core/xqueue \
	  tests/core/xring/xring_reentry_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/core/xqueue/xqueue.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/grant_range_test: $(HOST_BIN) tests/core/xtok/grant_range_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk \
	  tests/core/xtok/grant_range_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/sha512_test: $(HOST_BIN) tests/core/crypto/sha512_test.c $(SRC_DIR)/core/crypto/sha512.c
	$(HOST_CC) -I$(SRC_DIR)/core/crypto tests/core/crypto/sha512_test.c \
	  $(SRC_DIR)/core/crypto/sha512.c -o $@

$(HOST_BIN)/owner_test: $(HOST_BIN) tests/core/xtok/owner_test.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/arch/amd64/kernel/xeret/mem \
	  tests/core/xtok/owner_test.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
	  tests/core/xtok/host_stubs.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/give_test: $(HOST_BIN) tests/core/xtok/give_test.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c tests/core/xtok/host_stubs.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/kernel/xeret/mem \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk \
	  tests/core/xtok/give_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/boot_init_test: $(HOST_BIN) tests/core/xtok/boot_init_test.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c \
    tests/core/xtok/host_stubs.c $(SRC_DIR)/core/blk/blk.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk \
	  -I$(SRC_DIR)/arch/amd64/kernel/xeret/mem \
	  tests/core/xtok/boot_init_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c \
	  $(SRC_DIR)/core/blk/blk.c -o $@

$(HOST_BIN)/derive_call_test: $(HOST_BIN) tests/core/xtok/derive_call_test.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/derive_call.c \
    $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c tests/core/xtok/host_stubs.c $(SRC_DIR)/core/blk/blk.c
	$(HOST_CC) -Itests/core/xtok/stubs -Itests/core/xtok -I$(SRC_DIR)/kerncall/include \
	  -I$(SRC_DIR)/arch/amd64/boot/exord/efi -I$(SRC_DIR)/core/blk -I$(SRC_DIR)/arch/amd64/kernel/xeret/mem \
	  tests/core/xtok/derive_call_test.c tests/core/xtok/host_stubs.c \
	  $(SRC_DIR)/arch/amd64/kernel/xeret/mem/derive_call.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/owner.c $(SRC_DIR)/arch/amd64/kernel/xeret/mem/token_ledger.c $(SRC_DIR)/core/blk/blk.c -o $@

.PHONY: efi clean disk qemu host-tests

efi: $(EFI_OUT)

clean:
	rm -rf $(BUILD) $(HOST_BIN)
