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
BOOT_SRC      = source/arch/amd64/boot/multiboot2.s
MAIN_SRC      = source/arch/amd64/kernel/xeret/main.c
MEM_SRC       = source/arch/amd64/kernel/xeret/mem/physical.c
XQUEUE_SRC    = source/arch/amd64/syslib/core/xqueue.c
EXIT_SRC      = source/arch/amd64/init/exit.c
EXFS_SRC      = source/arch/amd64/drivers/storage/exfs.c
KERNELLIB_SRC = source/arch/amd64/kernel/xeret/kernel_lib.c

# Shell & Coreutils Sources
PATH_SRC   = source/arch/amd64/apps/xsh/core/path.c
SHELL_SRC  = source/arch/amd64/apps/xsh/core/shell.c
REMOVE_SRC = source/arch/amd64/apps/xsh/utils/remove.c
FORM_SRC   = source/arch/amd64/apps/xsh/utils/form.c
PATH_SRC      = source/arch/amd64/apps/xsh/core/path.c
SHELL_SRC     = source/arch/amd64/apps/xsh/core/shell.c
REMOVE_SRC    = source/arch/amd64/apps/xsh/utils/remove.c
FORM_SRC      = source/arch/amd64/apps/xsh/utils/form.c

LDSCRIPT = source/arch/amd64/boot/linker.ld
LDSCRIPT      = source/arch/amd64/boot/linker.ld

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
BOOT_OBJ      = bin/boot.o
MAIN_OBJ      = bin/main.o
MEM_OBJ       = bin/physical.o
XQUEUE_OBJ    = bin/xqueue.o
EXIT_OBJ      = bin/exit.o
EXFS_OBJ      = bin/exfs.o
KERNELLIB_OBJ = bin/kernel_lib.o
PATH_OBJ      = bin/path.o
SHELL_OBJ     = bin/shell.o
REMOVE_OBJ    = bin/remove.o
FORM_OBJ      = bin/form.o

KERNEL     = bin/xeret.bin
KERNEL        = bin/xeret.bin
IMAGE         = bin/xasmos.img

.PHONY: all clean run
.PHONY: all clean run image

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

$(KERNELLIB_OBJ): $(KERNELLIB_SRC) | bin
	@echo "[CLANG] Compiling kernel I/O and utility library..."
	$(CC) $(CFLAGS) -c $(KERNELLIB_SRC) -o $(KERNELLIB_OBJ)

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
# --- Unified Linking Rule (ELF Target) ---

$(KERNEL): $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) $(LDSCRIPT)
	@echo "[LLD] Linking freestanding Xeret binary..."
$(KERNEL): $(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(KERNELLIB_OBJ) $(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) $(LDSCRIPT)
	@echo "[LLD] Linking ELF Xeret binary..."
	$(LD) -T $(LDSCRIPT) \
		$(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) \
		$(BOOT_OBJ) $(MAIN_OBJ) $(MEM_OBJ) $(XQUEUE_OBJ) $(EXIT_OBJ) $(EXFS_OBJ) $(KERNELLIB_OBJ) \
		$(PATH_OBJ) $(SHELL_OBJ) $(REMOVE_OBJ) $(FORM_OBJ) \
		-o $(KERNEL)
	@echo "[OK] Xeret kernel, EXFS, and userland shell built successfully."
	@echo "[OK] Xeret kernel built successfully as an ELF target."

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
	@echo "[IMAGE] Writing Xeret kernel to disk blocks..."
	dd if=$(KERNEL) of=$(IMAGE) bs=512 seek=2 conv=notrunc
	@echo "[OK] Installation image ready at $(IMAGE)"

image: $(IMAGE)# =============================================================================
# efi_main is the only MS-ABI entry (attribute on the function).

LDS         := source/arch/amd64/boot/efi/linker-elf.lds
LDFLAGS     := -nostdlib -shared -Bsymbolic -T $(LDS)

EFI_SRC     := source/arch/amd64/boot/efi/efi_main.c
XERET_SRC   := source/arch/amd64/kernel/xeret/main.c \
               source/arch/amd64/kernel/xeret/fb_console.c \
               source/arch/amd64/kernel/xeret/mem/physical.c \
               source/arch/amd64/kernel/xeret/mem/token_ledger.c \
               source/arch/amd64/kernel/xeret/mem/paging.c

SRCS        := $(EFI_SRC) $(XERET_SRC)
OBJS        := $(patsubst source/%.c,$(BUILD)/obj/%.o,$(SRCS))

OVMF_CODE   ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS   ?= /usr/share/OVMF/OVMF_VARS.fd
QEMU        ?= qemu-system-x86_64

.PHONY: all clean dirs efi image run-uefi info help

all: efi

help:
	@echo "XASMOS UEFI (amd64)"
	@echo "  make efi        -> $(EFI_OUT)"
	@echo "  make image      -> $(BUILD)/xasmos-esp.img  (needs mkfs.vfat mtools)"
	@echo "  make run-uefi   -> QEMU + OVMF"
	@echo "  make clean"
	@echo "OVMF_CODE=$(OVMF_CODE)"

dirs:
	@mkdir -p $(BUILD)/obj/arch/amd64/boot/efi
	@mkdir -p $(BUILD)/obj/arch/amd64/kernel/xeret/mem
	@mkdir -p $(ESP)/EFI/BOOT

$(BUILD)/obj/%.o: source/%.c | dirs
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(SO_OUT): $(OBJS) $(LDS) | dirs
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(EFI_OUT): $(SO_OUT) | dirs
	@echo "[OBJCOPY] $@"
	@$(OBJCOPY) --target=efi-app-x86_64 $< $@
	@echo "      OK $$(wc -c < $@) bytes"

efi: $(EFI_OUT)

image: $(EFI_OUT)
	@echo "[IMG] $(BUILD)/xasmos-esp.img"
	@dd if=/dev/zero of=$(BUILD)/xasmos-esp.img bs=1M count=32 status=none
	@mkfs.vfat -F 32 -n XASMOS $(BUILD)/xasmos-esp.img >/dev/null
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI 2>/dev/null || true
	@mmd -i $(BUILD)/xasmos-esp.img ::/EFI/BOOT 2>/dev/null || true
	@mcopy -o -i $(BUILD)/xasmos-esp.img $(EFI_OUT) ::/EFI/BOOT/BOOTX64.EFI
	@echo "      OK"

run-uefi: image
	@test -f "$(OVMF_CODE)" || (echo "Missing OVMF_CODE=$(OVMF_CODE)"; exit 1)
	@test -f "$(OVMF_VARS)" || (echo "Missing OVMF_VARS=$(OVMF_VARS)"; exit 1)
	@cp -f "$(OVMF_VARS)" $(BUILD)/OVMF_VARS.fd
	$(QEMU) -machine q35 -m 512M -cpu qemu64 \
	  -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,file=$(BUILD)/OVMF_VARS.fd \
	  -drive format=raw,file=$(BUILD)/xasmos-esp.img \
	  -net none -serial stdio

info: efi
	@ls -la $(EFI_OUT) $(SO_OUT)
	@od -An -tx1 -N16 $(EFI_OUT)

clean:
	rm -rf build
	@echo "[CLEAN] OK"


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
clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf bin/
