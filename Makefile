# =============================================================================
# XASMOS Makefile -- Wayward Kernel + Sourcephilia
# =============================================================================

ASM       = nasm
ASMFLAGS  = -f bin -w+all -Werror=zeroing
DEBUG     ?= 0

ifeq ($(DEBUG),1)
    ASMFLAGS += -g
endif

UNAME_M = $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    LOCAL_ARCH = x86-64
else ifeq ($(UNAME_M),i686)
    LOCAL_ARCH = x86-32
else ifeq ($(UNAME_M),armv7l)
    LOCAL_ARCH = armv7h
else
    LOCAL_ARCH = unknown
endif

SRC_DIR      = src
BIN_DIR      = bin
BUILD_DIR    = build
PKG_DIR      = packages
SOURCEPHILIA = $(PKG_DIR)/sourcephilia

BOOT_SRC   = $(SRC_DIR)/boot/exord.asm
KERNEL_SRC = $(SRC_DIR)/kernel/xkernel.asm

# Solo dependencias (sin %include)
KERNEL_DEPS = $(KERNEL_SRC) \
              $(SRC_DIR)/drivers/exfs.asm \
              $(SRC_DIR)/init/exit.asm \
              $(SRC_DIR)/apps/xsh/ver.asm \
              $(SRC_DIR)/apps/xsh/clear.asm \
              $(SRC_DIR)/apps/xsh/list.asm \
              $(SRC_DIR)/apps/xsh/make.asm \
              $(SRC_DIR)/apps/xsh/remove.asm \
              $(SRC_DIR)/apps/xsh/read.asm \
              $(SRC_DIR)/apps/xsh/write.asm \
              $(SRC_DIR)/apps/xsh/locate.asm \
              $(SRC_DIR)/apps/xsh/whereami.asm \
              $(SRC_DIR)/apps/xsh/ubicate.asm \
              $(SRC_DIR)/apps/xsh/sprusr.asm \
              $(SRC_DIR)/apps/xsh/halt.asm \
              $(SRC_DIR)/apps/xsh/exofetch.asm \
              $(SRC_DIR)/apps/xsh.asm

BOOT_BIN   = $(BIN_DIR)/exord.bin
KERNEL_BIN = $(BIN_DIR)/xkernel.bin
IMAGE      = xasmos.img

IMAGE_SECTORS = 8192
SECTOR_SIZE   = 512

QEMU      = qemu-system-x86_64
QEMUFLAGS = -m 64M -no-reboot -no-shutdown

.PHONY: all run clean info bootstrap sources help

help:
	@echo "XASMOS Makefile -- Wayward Kernel"
	@echo "  make / make run / make clean / make info / make bootstrap"

all: $(IMAGE)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BOOT_BIN): $(BOOT_SRC) | $(BIN_DIR)
	@echo "[NASM] EXORD ($(BOOT_SRC))"
	@$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then echo "ERROR: EXORD must be 512 bytes"; exit 1; fi
	@echo "      OK (512 bytes)"

$(KERNEL_BIN): $(KERNEL_DEPS) | $(BIN_DIR)
	@echo "[NASM] Wayward Kernel ($(KERNEL_SRC))"
	@$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@sz=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$sz -gt $$((64 * $(SECTOR_SIZE))) ]; then \
		echo "ERROR: Kernel too large ($$sz bytes)"; exit 1; \
	fi
	@echo "      OK ($$(wc -c < $(KERNEL_BIN)) bytes)"

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG] Creating $(IMAGE)..."
	@dd if=/dev/zero of=$(IMAGE) bs=$(SECTOR_SIZE) count=$(IMAGE_SECTORS) status=none
	@dd if=$(BOOT_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=0 count=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none
	@echo "[OK] $(IMAGE) ready"

run: $(IMAGE)
	@echo "[QEMU] Starting xasmos..."
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

info:
	@echo "EXORD:  $$([ -f $(BOOT_BIN) ] && wc -c < $(BOOT_BIN) || echo missing) bytes"
	@echo "Kernel: $$([ -f $(KERNEL_BIN) ] && wc -c < $(KERNEL_BIN) || echo missing) bytes"
	@echo "Image:  $$([ -f $(IMAGE) ] && wc -c < $(IMAGE) || echo missing) bytes"
	@echo "Arch:   $(LOCAL_ARCH)"

clean:
	@rm -rf $(BIN_DIR) $(IMAGE) $(BUILD_DIR)
	@echo "[CLEAN] OK"

bootstrap:
	@mkdir -p $(SOURCEPHILIA)/{recipes,cache,$(LOCAL_ARCH)}
	@echo "[SOURCEPHILIA] structure ready"

sources:
	@echo "[SOURCEPHILIA] stub"
