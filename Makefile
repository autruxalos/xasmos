# =============================================================================
# XASMOS Makefile -- Wayward Kernel + Sourcephilia Integration
# =============================================================================
# Uso:
#   make              -- compila imagen de disco completa (DEBUG=0)
#   make run          -- ejecuta en QEMU
#   make clean        -- limpia binarios
#   make info         -- muestra tama?os
#   DEBUG=1 make      -- con s?mbolos de depuraci?n NASM
#
# Sourcephilia integration:
#   make bootstrap    -- prepara estructura para sourcephilia
#   make sources      -- descarga fuentes de paquetes (stub)
# =============================================================================

# Herramientas
ASM       = nasm
ASMFLAGS  = -f bin -w+all -Werror=zeroing
DEBUG     ?= 0

ifeq ($(DEBUG),1)
    ASMFLAGS += -g
endif

# Detecci?n de arquitectura local (para compilaci?n de paquetes sourcephilia)
UNAME_M   = $(shell uname -m)
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
SRC_DIR       = src
BIN_DIR       = bin
BUILD_DIR     = build
PKG_DIR       = packages
SOURCEPHILIA  = $(PKG_DIR)/sourcephilia

# Archivos fuente
BOOT_SRC   = $(SRC_DIR)/boot/exord.asm
KERNEL_SRC = $(SRC_DIR)/kernel/xkernel.asm

# Dependencias del kernel (todo lo que se %include en xkernel.asm)
KERNEL_DEPS = $(KERNEL_SRC) \
            $(SRC_DIR)/drivers/exfs.asm \
            $(SRC_DIR)/init/exit.asm \
            $(SRC_DIR)/apps/xsh.asm \
            $(SRC_DIR)/apps/xsh/ver.asm \
            $(SRC_DIR)/apps/xsh/clear.asm \
            $(SRC_DIR)/apps/xsh/list.asm \
            $(SRC_DIR)/apps/xsh/make.asm \
            $(SRC_DIR)/apps/xsh/remove.asm \
            $(SRC_DIR)/apps/xsh/read.asm \
            $(SRC_DIR)/apps/xsh/write.asm \
            $(SRC_DIR)/apps/xsh/locate.asm \
            $(SRC_DIR)/apps/xsh/whereami.asm \
            $(SRC_DIR)/apps/xsh/sprusr.asm \
            $(SRC_DIR)/apps/xsh/halt.asm \
            $(SRC_DIR)/apps/xsh/exofetch/exofetch.asm
			$(SRC_DIR)/apps/xsh/exofetch.asm

# Binarios
BOOT_BIN   = $(BIN_DIR)/exord.bin
KERNEL_BIN = $(BIN_DIR)/xkernel.bin
IMAGE      = xos.img

# Configuraci?n de imagen
IMAGE_SECTORS = 8192
SECTOR_SIZE   = 512

# QEMU
QEMU       = qemu-system-x86_64
QEMUFLAGS  = -m 64M -no-reboot -no-shutdown

# Targets
.PHONY: all run clean info bootstrap sources help

help:
	@echo "XASMOS Makefile -- Wayward Kernel"
	@echo ""
	@echo "Targets b?sicos:"
	@echo "  make              Compilar imagen de disco"
	@echo "  make run          Ejecutar en QEMU"
	@echo "  make clean        Limpiar binarios"
	@echo "  make info         Mostrar tama?os"
	@echo ""
	@echo "Sourcephilia (gestor de paquetes):"
	@echo "  make bootstrap    Preparar estructura para sourcephilia"
	@echo ""
	@echo "Opciones:"
	@echo "  DEBUG=1 make      Compilar con s?mbolos de depuraci?n"
	@echo "  ARCH=armv7h make  Target espec?fico (stub, falta implementar)"

all: $(IMAGE)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(KERNEL_BIN): $(KERNEL_DEPS) | $(BIN_DIR)
	@echo "[NASM] Wayward Kernel ($(KERNEL_SRC))"
	@$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@if [ ! -f $(KERNEL_BIN) ]; then \
		echo "ERROR: no se gener? $(KERNEL_BIN)"; exit 1; \
	fi
	@sz=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$sz -gt $$(( 64 * $(SECTOR_SIZE) )) ]; then \
		echo "ERROR: Kernel demasiado grande ($$sz bytes, m?x 32KB)"; exit 1; \
	fi
	@echo "      OK ($$(wc -c < $(KERNEL_BIN)) bytes)"

$(BOOT_BIN): $(BOOT_SRC) | $(BIN_DIR)
	@echo "[NASM] Exord Bootloader ($(BOOT_SRC))"
	@$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then \
		echo "ERROR: Exord debe ser exactamente 512 bytes (tiene $$sz)"; exit 1; \
	fi
	@echo "      OK (512 bytes)"

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG] Creating disc xasmos.img ($(IMAGE_SECTORS) sectores)..."
	@dd if=/dev/zero of=$(IMAGE) bs=$(SECTOR_SIZE) count=$(IMAGE_SECTORS) status=none
	@dd if=$(BOOT_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=0 count=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none
	@echo "[OK] Imagen lista: $(IMAGE)"

run: $(IMAGE)
	@echo "[QEMU] Iniciando..."
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

info:
	@echo "=== Tama?os ==="
	@echo "Exord:   $$([ -f $(BOOT_BIN) ] && wc -c < $(BOOT_BIN) || echo 'no compilado') bytes"
	@echo "Kernel:  $$([ -f $(KERNEL_BIN) ] && wc -c < $(KERNEL_BIN) || echo 'no compilado') bytes"
	@echo "Imagen:  $$([ -f $(IMAGE) ] && wc -c < $(IMAGE) || echo 'no existe') bytes"
	@echo ""
	@echo "Arquitectura local detectada: $(LOCAL_ARCH)"

clean:
	@echo "[CLEAN] Eliminando binarios..."
	@rm -rf $(BIN_DIR) $(IMAGE) $(BUILD_DIR)
	@echo "      OK"

# =========================================================================
# SOURCEPHILIA: Gestor de paquetes compilados desde fuente
# =========================================================================

bootstrap: | $(BIN_DIR)
	@echo "[SOURCEPHILIA] Preparando estructura..."
	@mkdir -p $(SOURCEPHILIA)/{recipes,cache,$(LOCAL_ARCH)}
	@echo "      OK (estructura en $(SOURCEPHILIA))"

sources:
	@echo "[SOURCEPHILIA] Stub: download sources (not implemented yet)"
	@echo "              This is where: curl, git clone, checksum verification, etc. goes"

# =========================================================================
# Debug targets (future)
# =========================================================================

disasm:
	@echo "[DISASM] Generando listado de Exord..."
	@ndisasm -b 16 $(BOOT_BIN) > exord.dis
	@wc -l exord.dis
	@echo "        (archivo: exord.dis)"
