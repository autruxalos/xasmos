# =============================================================================
# MAKEFILE - XASMOS EXOKERNEL OPERATING SYSTEM
# =============================================================================
# IMPORTANT — build architecture:
#    src/kernel/xkernel.asm is the ONLY file assembled as the kernel.
#    It integrates src/drivers/exfs.asm, src/init/exit.asm, and src/apps/xsh.asm
#    via %include, because NASM's `-f bin` format has no linker:
#    exit.asm/xsh.asm/exfs.asm CANNOT be assembled separately; doing so
#    fails with "symbol not defined" because each .bin file is isolated.
#
# Disk Map:
#    Sector 0      -> XBOOT   (MBR, exactly 512 bytes)
#    Sector 1..64  -> XKERNEL (up to 32 KB, includes EXFS+EXIT+XSH)
#    Sector 38+    -> EXFS Data (hot-formatted by the kernel)
# =============================================================================

ASM      = nasm
ASMFLAGS = -f bin -w+all -Werror=zeroing

BOOT_SRC   = src/boot/xboot.asm
KERNEL_SRC = src/kernel/xkernel.asm

# Dependencies: if any of these change, the kernel must be recompiled
KERNEL_DEPS = $(KERNEL_SRC) \
              src/drivers/exfs.asm \
              src/init/exit.asm \
              src/apps/xsh.asm

BOOT_BIN   = bin/xboot.bin
KERNEL_BIN = bin/xkernel.bin

IMAGE         = xos.img
IMAGE_SECTORS = 8192          # 4 MB total image size

QEMU      = qemu-system-i386
QEMUFLAGS = -m 16M -no-reboot -no-shutdown

.PHONY: all run run-nographic debug clean info

all: $(IMAGE)

bin:
	@mkdir -p bin

# --- XBOOT: must be exactly 512 bytes with 0xAA55 signature ---
$(BOOT_BIN): $(BOOT_SRC) | bin
	@echo "[NASM] XBOOT..."
	$(ASM) $(ASMFLAGS) $(BOOT_SRC) -o $(BOOT_BIN)
	@sz=$$(wc -c < $(BOOT_BIN)); \
	if [ $$sz -ne 512 ]; then \
		echo "ERROR: XBOOT is $$sz bytes (must be 512)"; exit 1; \
	fi
	@sig=$$(od -An -tx1 -j 510 -N 2 $(BOOT_BIN) | tr -d ' '); \
	if [ "$$sig" != "55aa" ]; then \
		echo "ERROR: incorrect MBR signature ($$sig, expected 55aa)"; exit 1; \
	fi
	@echo "      XBOOT OK (512 bytes, signature 0xAA55)"

# --- XKERNEL: includes EXFS + EXIT + XSH in a single flat binary ---
$(KERNEL_BIN): $(KERNEL_DEPS) | bin
	@echo "[NASM] XKERNEL (+ EXFS + EXIT + XSH via %include)..."
	$(ASM) $(ASMFLAGS) $(KERNEL_SRC) -o $(KERNEL_BIN)
	@sz=$$(wc -c < $(KERNEL_BIN)); \
	maxsz=$$((64 * 512)); \
	if [ $$sz -gt $$maxsz ]; then \
		echo "ERROR: XKERNEL size is $$sz bytes, exceeds reserved $$maxsz bytes"; \
		echo "       (increase KERNEL_SECTORS in Makefile and XBOOT)"; exit 1; \
	fi
	@echo "      XKERNEL OK ($$(wc -c < $(KERNEL_BIN)) bytes)"

# --- Final Disk Image ---
$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo ""
	@echo "[IMG] Creating $(IMAGE) ($(IMAGE_SECTORS) sectors = $$(( $(IMAGE_SECTORS)*512/1024/1024 )) MB)..."
	dd if=/dev/zero of=$(IMAGE) bs=512 count=$(IMAGE_SECTORS) status=none
	dd if=$(BOOT_BIN)   of=$(IMAGE) bs=512 seek=0 count=1 conv=notrunc status=none
	@echo "      [sector 0] XBOOT"
	dd if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=1 conv=notrunc status=none
	@echo "      [sector 1] XKERNEL ($$(( ($$(wc -c < $(KERNEL_BIN)) + 511) / 512 )) sectors)"
	@echo ""
	@echo "[OK] $(IMAGE) ready. Use 'make run' to execute."

# --- Run in QEMU (16-bit 8086 / i386 mode compatible) ---
run: $(IMAGE)
	@echo "[QEMU] Starting XOS..."
	$(QEMU) -cpu 486 -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display sdl

# --- Run in text mode (no SDL, useful for servers/SSH) ---
run-nographic: $(IMAGE)
	@echo "[QEMU] Starting XASMOS (serial/console mode)..."
	$(QEMU) -cpu 486 -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) -display curses

# --- Debug: QEMU monitor + interrupt/reset logs ---
debug: $(IMAGE)
	@echo "[QEMU] Debug mode -- Press Ctrl+Alt+2 for the monitor"
	$(QEMU) -cpu 486 -drive format=raw,file=$(IMAGE),if=ide,media=disk $(QEMUFLAGS) \
		-monitor stdio -d int,cpu_reset -D qemu_debug.log -display sdl

info:
	@echo "XBOOT:   $$(wc -c < $(BOOT_BIN) 2>/dev/null || echo '(not compiled)') bytes"
	@echo "XKERNEL: $$(wc -c < $(KERNEL_BIN) 2>/dev/null || echo '(not compiled)') bytes"
	@echo "IMAGE:   $$(wc -c < $(IMAGE) 2>/dev/null || echo '(not generated)') bytes"

clean:
	@echo "[CLEAN] Deleting binaries and image..."
	rm -rf bin/
	rm -f $(IMAGE) qemu_debug.log
	@echo "      Done."
