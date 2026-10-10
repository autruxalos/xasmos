# =============================================================================
# XASMOS / XERET - Master LLVM Build Script
# =============================================================================

CC       = clang
AS       = llvm-mc
LD       = ld.lld

CFLAGS   = -target x86_64-unknown-none-elf -ffreestanding -fno-builtin -nostdlib -O3 -mno-red-zone
ASFLAGS  = -filetype=obj -triple=x86_64-unknown-none-elf

BOOT_SRC = source/arch/amd64/boot/multiboot2.s
MAIN_SRC = source/arch/amd64/kernel/xeret/main.c
MEM_SRC  = source/arch/amd64/kernel/xeret/mem/physical.c
LDSCRIPT = source/arch/amd64/boot/linker.ld

BOOT_OBJ = bin/boot.o
MAIN_OBJ = bin/main.o
MEM_OBJ  = bin/physical.o
KERNEL   = bin/xeret.bin

.PHONY: all clean run

all: $(KERNEL)

bin:
	@mkdir -p bin

$(BOOT_OBJ): $(BOOT_SRC) | bin
	@echo "[LLVM-MC] Assembling boot stub..."
	$(AS) $(ASFLAGS) $(BOOT_SRC) -o $(BOOT_OBJ)

$(MAIN_OBJ): $(MAIN_SRC) | bin
	@echo "[CLANG] Compiling main kernel..."
	$(CC) $(CFLAGS) -c $(MAIN_SRC) -o $(MAIN_OBJ)

$(MEM_OBJ): $(MEM_SRC) | bin
	@echo "[CLANG] Compiling physical memory allocator..."
	$(CC) $(CFLAGS) -c $(MEM_SRC) -o $(MEM_OBJ)

$(KERNEL): $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(LDSCRIPT)
	@echo "[LLD] Linking with custom linker script..."
	$(LD) -T $(LDSCRIPT) $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) -o $(KERNEL)
	@echo "[OK] Xeret kernel built successfully."

run: $(KERNEL)
	@echo "[QEMU] Booting Xeret Exokernel..."
	qemu-system-x86_64 -kernel $(KERNEL)

clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf bin/
