# =============================================================================
# MAKEFILE — XASMOS
# =============================================================================
ASM      = nasm
ASMFLAGS = -f bin -w+all -Werror=zeroing

BOOT_SRC   = src/boot/xboot.asm
KERNEL_SRC = src/kernel/xkernel.asm

KERNEL_DEPS = $(KERNEL_SRC) \
              src/drivers/exfs.asm \
              src/init/exit.asm \
              src/apps/xsh/xsh.asm \
              src/apps/xsh/ver.asm \
              src/apps/xsh/clear.asm \
              src/apps/xsh/list.asm \
              src/apps/xsh/makedir.asm \
              src/apps/xsh/makefile.asm \
              src/apps/xsh/remove.asm \
              src/apps/xsh/read.asm \
              src/apps/xsh/write.asm \
              src/apps/xsh/cd.asm \
              src/apps/xsh/pwd.asm \
              src/apps/xsh/sprusr.asm \
              src/apps/xsh/halt.asm \
              src/apps/xsh/exofetch/exofetch.asm

BOOT_BIN   = bin/xboot.bin
KERNEL_BIN = bin/xkernel.bin
IMAGE      = xos.img
IMAGE_SECTORS = 8192

QEMU      = qemu-system-x86_64
QEMUFLAGS = -m 64M -no-reboot -no-shutdown

.PHONY: all run debug clean info

all: $(IMAGE)

bin:
	@mkdir -p bin

$(BOOT_BIN): $(BOOT_SRC) | bin
	@echo "[NASM] XBOOT..."
	$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then echo "ERROR: XBOOT tiene $$sz bytes"; exit 1; fi
	@echo "      XBOOT OK (512 bytes)"

$(KERNEL_BIN): $(KERNEL_DEPS) | bin
	@echo "[NASM] XKERNEL (+ EXFS + EXIT + XSH modular)..."
	$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@echo "      XKERNEL OK ($$(wc -c < $(KERNEL_BIN)) bytes)"

$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "[IMG] Creando $(IMAGE)..."
	dd if=/dev/zero of=$(IMAGE) bs=512 count=$(IMAGE_SECTORS) status=none
	dd if=$(BOOT_BIN)   of=$(IMAGE) bs=512 seek=0 count=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=1 conv=notrunc status=none
	@echo "[OK] $(IMAGE) listo."

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

info:
	@echo "XBOOT:   $$(wc -c < $(BOOT_BIN) 2>/dev/null || echo '?') bytes"
	@echo "XKERNEL: $$(wc -c < $(KERNEL_BIN) 2>/dev/null || echo '?') bytes"

clean:
	rm -rf bin/ $(IMAGE)
