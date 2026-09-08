# =============================================================================
# XASMOS Makefile -- Wayward Kernel + Sourcephilia
# =============================================================================
# Uso:
#   make              Compilar imagen de disco
#   make run          Ejecutar en QEMU
#   make clean        Limpiar binarios
#   make info         Mostrar tamaños
#   DEBUG=1 make      Compilar con símbolos de depuración
#   make bootstrap    Preparar estructura Sourcephilia
# =============================================================================

# Herramientas
ASM       = nasm
ASMFLAGS  = -f bin -w+all -Werror=zeroing
DEBUG     ?= 0

ifeq ($(DEBUG),1)
    ASMFLAGS += -g
endif

# Detección de arquitectura local (Sourcephilia)
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

# Directorios
SRC_DIR      = src
BIN_DIR      = bin
BUILD_DIR    = build
PKG_DIR      = packages
SOURCEPHILIA = $(PKG_DIR)/sourcephilia

# Archivos fuente principales
BOOT_SRC   = $(SRC_DIR)/boot/exord.asm
KERNEL_SRC = $(SRC_DIR)/kernel/xkernel.asm

# Dependencias del kernel (orden importa para NASM %include)
# Los comandos modulares van ANTES de xsh.asm
KERNEL_DEPS = $(KERNEL_SRC) \
			%include "src/drivers/exfs.asm"
%include "src/init/exit.asm"

%include "src/apps/xsh/ver.asm"
%include "src/apps/xsh/clear.asm"
%include "src/apps/xsh/list.asm"
%include "src/apps/xsh/make.asm"
%include "src/apps/xsh/remove.asm"
%include "src/apps/xsh/read.asm"
%include "src/apps/xsh/write.asm"
%include "src/apps/xsh/locate.asm"
%include "src/apps/xsh/whereami.asm"
%include "src/apps/xsh/ubicate.asm"
%include "src/apps/xsh/sprusr.asm"
%include "src/apps/xsh/halt.asm"
%include "src/apps/xsh/exofetch.asm"

%include "src/apps/xsh.asm"
# Binarios
BOOT_BIN   = $(BIN_DIR)/exord.bin
KERNEL_BIN = $(BIN_DIR)/xkernel.bin
IMAGE      = xasmos.img

# Configuración de imagen
IMAGE_SECTORS = 8192
SECTOR_SIZE   = 512
MAX_KERNEL_BYTES = $$((64 * $(SECTOR_SIZE)))

# QEMU
QEMU      = qemu-system-x86_64
QEMUFLAGS = -m 64M -no-reboot -no-shutdown

# Targets
.PHONY: all run clean info bootstrap sources help disasm

help:
	@echo "XASMOS Makefile -- Wayward Kernel"
	@echo ""
	@echo "Basic targets:"
	@echo "  make              Build disk image"
	@echo "  make run          Run in QEMU"
	@echo "  make clean        Clean binaries"
	@echo "  make info         Show sizes"
	@echo ""
	@echo "Sourcephilia:"
	@echo "  make bootstrap    Prepare package structure"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1 make      Build with debug symbols"

all: $(IMAGE)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# --- EXORD (bootloader) ---
$(BOOT_BIN): $(BOOT_SRC) | $(BIN_DIR)
	@echo "[NASM] EXORD Bootloader ($(BOOT_SRC))"
	@$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then \
		echo "ERROR: EXORD must be exactly 512 bytes (got $$sz)"; exit 1; \
	fi
	@echo "      OK (512 bytes)"

# --- Wayward Kernel ---
$(KERNEL_BIN): $(KERNEL_DEPS) | $(BIN_DIR)
	@echo "[NASM] Wayward Kernel ($(KERNEL_SRC))"
	@$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@if [ ! -f $(KERNEL_BIN) ]; then \
		echo "ERROR: $(KERNEL_BIN) was not generated"; exit 1; \
	fi
	@sz=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$sz -gt $(MAX_KERNEL_BYTES) ]; then \
		echo "ERROR: Kernel too large ($$sz bytes, max $(MAX_KERNEL_BYTES))"; exit 1; \
	fi
	@echo "      OK ($$(wc -c < $(KERNEL_BIN)) bytes)"

# --- Disk image ---
$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG] Creating $(IMAGE) ($(IMAGE_SECTORS) sectors)..."
	@dd if=/dev/zero of=$(IMAGE) bs=$(SECTOR_SIZE) count=$(IMAGE_SECTORS) status=none
	@dd if=$(BOOT_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=0 count=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none
	@echo "[OK] Image ready: $(IMAGE)"

run: $(IMAGE)
	@echo "[QEMU] Starting xasmos..."
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

info:
	@echo "=== Sizes ==="
	@echo "EXORD:   $$([ -f $(BOOT_BIN) ] && wc -c < $(BOOT_BIN) || echo 'not built') bytes"
	@echo "Kernel:  $$([ -f $(KERNEL_BIN) ] && wc -c < $(KERNEL_BIN) || echo 'not built') bytes"
	@echo "Image:   $$([ -f $(IMAGE) ] && wc -c < $(IMAGE) || echo 'missing') bytes"
	@echo ""
	@echo "Local architecture: $(LOCAL_ARCH)"

clean:
	@echo "[CLEAN] Removing binaries..."
	@rm -rf $(BIN_DIR) $(IMAGE) $(BUILD_DIR)
	@echo "      OK"

# =========================================================================
# SOURCEPHILIA
# =========================================================================

bootstrap: | $(BIN_DIR)
	@echo "[SOURCEPHILIA] Preparing structure..."
	@mkdir -p $(SOURCEPHILIA)/{recipes,cache,$(LOCAL_ARCH)}
	@echo "      OK ($(SOURCEPHILIA))"

sources:
	@echo "[SOURCEPHILIA] Stub: download sources (not implemented yet)"

# =========================================================================
# Debug
# =========================================================================

disasm: $(BOOT_BIN)
	@echo "[DISASM] EXORD listing..."
	@ndisasm -b 16 $(BOOT_BIN) > exord.dis
	@echo "      Written to exord.dis ($$(wc -l < exord.dis) lines)"
