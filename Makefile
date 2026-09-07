# =============================================================================
# XASMOS Makefile — Wayward Kernel + Sourcephilia
# Versión 2: Tolerante, detecta estructura real
# =============================================================================

ASM       = nasm
ASMFLAGS  = -f bin -w+all -Werror=zeroing
DEBUG     ?= 0

ifeq ($(DEBUG),1)
    ASMFLAGS += -g
endif

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

SRC_DIR       = src
BIN_DIR       = bin
BUILD_DIR     = build
PKG_DIR       = packages
SOURCEPHILIA  = $(PKG_DIR)/sourcephilia

BOOT_SRC   = $(SRC_DIR)/boot/exord.asm
KERNEL_SRC = $(SRC_DIR)/kernel/xkernel.asm

# Archivos OBLIGATORIOS (deben existir)
KERNEL_CORE = $(KERNEL_SRC) \
              $(SRC_DIR)/drivers/exfs.asm \
              $(SRC_DIR)/init/exit.asm \
              $(SRC_DIR)/apps/xsh.asm

# Archivos OPCIONALES (si existen, se incluyen; si no, se ignoran)
KERNEL_COMMANDS = \
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
    $(SRC_DIR)/apps/xsh/exofetch.asm

# Filtrar solo los que existen
KERNEL_COMMANDS_EXIST = $(wildcard $(KERNEL_COMMANDS))

KERNEL_DEPS = $(KERNEL_CORE) $(KERNEL_COMMANDS_EXIST)

BOOT_BIN   = $(BIN_DIR)/exord.bin
KERNEL_BIN = $(BIN_DIR)/xkernel.bin
IMAGE      = xos.img

IMAGE_SECTORS = 8192
SECTOR_SIZE   = 512

QEMU       = qemu-system-x86_64
QEMUFLAGS  = -m 64M -no-reboot -no-shutdown

.PHONY: all run clean info bootstrap sources help check-deps

help:
	@echo "XASMOS Makefile — Wayward Kernel"
	@echo ""
	@echo "Targets:"
	@echo "  make              Compilar imagen de disco"
	@echo "  make run          Ejecutar en QEMU"
	@echo "  make clean        Limpiar binarios"
	@echo "  make info         Mostrar tamanos"
	@echo "  make check-deps   Verificar archivos necesarios"
	@echo ""
	@echo "Opciones:"
	@echo "  DEBUG=1 make      Con simbolos de depuracion"

check-deps:
	@echo "[CHECK] Archivos requeridos:"
	@for f in $(KERNEL_CORE); do \
		if [ -f "$$f" ]; then \
			echo "  [OK] $$f"; \
		else \
			echo "  [MISSING] $$f"; \
		fi; \
	done
	@echo ""
	@echo "[CHECK] Comandos XSH disponibles:"
	@for f in $(KERNEL_COMMANDS); do \
		if [ -f "$$f" ]; then \
			echo "  [OK] $$f"; \
		else \
			echo "  [skip] $$f"; \
		fi; \
	done

all: $(IMAGE)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(KERNEL_BIN): $(KERNEL_DEPS) | $(BIN_DIR)
	@echo "[NASM] Wayward Kernel"
	@$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@if [ ! -f $(KERNEL_BIN) ]; then \
		echo "ERROR: no se genero $(KERNEL_BIN)"; exit 1; \
	fi
	@sz=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$sz -gt $$(( 64 * $(SECTOR_SIZE) )) ]; then \
		echo "ERROR: Kernel demasiado grande ($$sz bytes, max 32KB)"; exit 1; \
	fi
	@echo "      OK ($$sz bytes)"

$(BOOT_BIN): $(BOOT_SRC) | $(BIN_DIR)
	@echo "[NASM] Exord Bootloader"
	@$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then \
		echo "ERROR: Exord debe ser 512 bytes (tiene $$sz)"; exit 1; \
	fi
	@echo "      OK (512 bytes)"

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG] Creando disco xos.img..."
	@dd if=/dev/zero of=$(IMAGE) bs=$(SECTOR_SIZE) count=$(IMAGE_SECTORS) status=none
	@dd if=$(BOOT_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=0 count=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$(IMAGE) bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none
	@echo "[OK] Imagen lista: $(IMAGE)"

run: $(IMAGE)
	@echo "[QEMU] Iniciando..."
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

info:
	@echo "=== Tamanos ==="
	@echo "Exord:   $$([ -f $(BOOT_BIN) ] && wc -c < $(BOOT_BIN) || echo 'no compilado') bytes"
	@echo "Kernel:  $$([ -f $(KERNEL_BIN) ] && wc -c < $(KERNEL_BIN) || echo 'no compilado') bytes"
	@echo "Imagen:  $$([ -f $(IMAGE) ] && wc -c < $(IMAGE) || echo 'no existe') bytes"
	@echo ""
	@echo "Arquitectura: $(LOCAL_ARCH)"

clean:
	@echo "[CLEAN] Eliminando binarios..."
	@rm -rf $(BIN_DIR) $(IMAGE) $(BUILD_DIR)
	@echo "      OK"

bootstrap:
	@echo "[SOURCEPHILIA] Preparando estructura..."
	@mkdir -p $(SOURCEPHILIA)/{recipes,cache,{x86-16,x86-32,x86-64,armv7h,aarch64}}
	@echo "      OK (estructura en $(SOURCEPHILIA))"

sources:
	@echo "[SOURCEPHILIA] Stub: download sources (not implemented)"

disasm:
	@echo "[DISASM] Generando listado de Exord..."
	@ndisasm -b 16 $(BOOT_BIN) > exord.dis 2>/dev/null || echo "ndisasm no disponible"
	@[ -f exord.dis ] && wc -l exord.dis || true
