# =============================================================================
# XASMOS / XERET - Master LLVM Build Script
# =============================================================================

CC       = clang
AS       = llvm-mc
LD       = ld.lld

CFLAGS   = -target x86_64-unknown-none-elf -ffreestanding -fno-builtin -nostdlib -O3 -mno-red-zone
ASFLAGS  = -filetype=obj -triple=x86_64-unknown-none-elf

# --- Source Files Mapping ---
BOOT_SRC   = source/arch/amd64/boot/multiboot2.s
MAIN_SRC   = source/arch/amd64/kernel/xeret/main.c
MEM_SRC    = source/arch/amd64/kernel/xeret/mem/physical.c
XQUEUE_SRC = source/arch/amd64/syslib/core/xqueue.c
EXIT_SRC   = source/arch/amd64/init/exit.c
EXFS_SRC   = source/arch/amd64/drivers/storage/exfs.c

# Shell & Coreutils Sources
PATH_SRC   = source/arch/amd64/apps/xsh/core/path.c
SHELL_SRC  = source/arch/amd64/apps/xsh/core/shell.c
REMOVE_SRC = source/arch/amd64/apps/xsh/utils/remove.c
FORM_SRC   = source/arch/amd64/apps/xsh/utils/form.c

LDSCRIPT = source/arch/amd64/boot/linker.ld

# --- Object Files Mapping ---
BOOT_OBJ   = bin/boot.o
MAIN_OBJ   = bin/main.o
MEM_OBJ    = bin/physical.o
XQUEUE_OBJ = bin/xqueue.o
EXIT_OBJ   = bin/exit.o
EXFS_OBJ   = bin/exfs.o
PATH_OBJ   = bin/path.o
SHELL_OBJ  = bin/shell.o
REMOVE_OBJ = bin/remove.o
FORM_OBJ   = bin/form.o

KERNEL     = bin/xeret.bin

.PHONY: all clean run

all: $(KERNEL)

bin:
	@mkdir -p bin

# --- Compilation Rules ---

$(BOOT_OBJ): $(BOOT_SRC) | bin
	@echo "[LLVM-MC] Assembling boot stub..."
	$(AS) $(ASFLAGS) $(BOOT_SRC) -o $(BOOT_OBJ)

$(MAIN_OBJ): $(MAIN_SRC) | bin
	@echo "[CLANG] Compiling kernel entry..."
	$(CC) $(CFLAGS) -c $(MAIN_SRC) -o $(MAIN_OBJ)

$(MEM_OBJ): $(MEM_SRC) | bin
	@echo "[CLANG] Compiling physical memory allocator..."
	$(CC) $(CFLAGS) -c $(MEM_SRC) -o $(MEM_OBJ)

$(XQUEUE_OBJ): $(XQUEUE_SRC) | bin
	@echo "[CLANG] Compiling xqueue protocol..."
	$(CC) $(CFLAGS) -c $(XQUEUE_SRC) -o $(XQUEUE_OBJ)

$(EXIT_OBJ): $(EXIT_SRC) | bin
	@echo "[CLANG] Compiling system bootstrap coordinator (EXIT)..."
	$(CC) $(CFLAGS) -c $(EXIT_SRC) -o $(EXIT_OBJ)

$(EXFS_OBJ): $(EXFS_SRC) | bin
	@echo "[CLANG] Compiling EXFS storage driver..."
	$(CC) $(CFLAGS) -c $(EXFS_SRC) -o $(EXFS_OBJ)

$(PATH_OBJ): $(PATH_SRC) | bin
	@echo "[CLANG] Compiling SuperDir path resolver..."
	$(CC) $(CFLAGS) -c $(PATH_SRC) -o $(PATH_OBJ)

$(SHELL_OBJ): $(SHELL_SRC) | bin
	@echo "[CLANG] Compiling core modular shell..."
	$(CC) $(CFLAGS) -c $(SHELL_SRC) -o $(SHELL_OBJ)

$(REMOVE_OBJ): $(REMOVE_SRC) | bin
	@echo "[CLANG] Compiling coreutil: remove..."
	$(CC) $(CFLAGS) -c $(REMOVE_SRC) -o $(REMOVE_OBJ)

$(FORM_OBJ): $(FORM_SRC) | bin
	@echo "[CLANG] Compiling coreutil: form..."
	$(CC) $(CFLAGS) -c $(FORM_SRC) -o $(FORM_OBJ)

# --- Linking Rule ---

$(KERNEL): $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) $(LDSCRIPT)
	@echo "[LLD] Linking freestanding Xeret binary..."
	$(LD) -T $(LDSCRIPT) \
		$(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) \
		$(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) \
		-o $(KERNEL)
	@echo "[OK] Xeret kernel, EXFS, and userland shell built successfully."

# --- QEMU Execution ---

run: $(KERNEL)
	@echo "[QEMU] Booting Xeret Exokernel..."
	qemu-system-x86_64 -kernel $(KERNEL)

clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf bin/

# --- Installation Image Rule ---
IMAGE = bin/xasmos.img

$(IMAGE): $(KERNEL)
	@echo "[IMAGE] Creating 64MB raw XASMOS installation disk..."
	dd if=/dev/zero of=$(IMAGE) bs=1M count=64
	@echo "[IMAGE] Writing Xeret (Merlin) kernel to disk blocks..."
	dd if=$(KERNEL) of=$(IMAGE) bs=512 seek=2 conv=notrunc
	@echo "[OK] Installation image ready at $(IMAGE)"

image: $(IMAGE)

# --- Add this to your source files list ---
KERNELLIB_SRC = source/arch/amd64/kernel/xeret/kernel_lib.c
KERNELLIB_OBJ = bin/kernel_lib.o

# --- Add this compilation rule ---
$(KERNELLIB_OBJ): $(KERNELLIB_SRC) | bin
	@echo "[CLANG] Compiling kernel I/O and utility library..."
	$(CC) $(CFLAGS) -c $(KERNELLIB_SRC) -o $(KERNELLIB_OBJ)

# --- Include $(KERNELLIB_OBJ) in the $(KERNEL) linking target rule ---
$(KERNEL): $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(KERNELLIB_OBJ) $(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) $(LDSCRIPT)
	@echo "[LLD] Linking freestanding Xeret binary..."
	$(LD) -T $(LDSCRIPT) \
		$(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(KERNELLIB_OBJ) \
		$(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) \
		-o $(KERNEL)
	@echo "[OK] Xeret kernel built successfully."
