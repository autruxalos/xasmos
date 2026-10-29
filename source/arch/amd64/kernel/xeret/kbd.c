/* XASMOS - PS/2 keyboard driver (i8042, polled, scancode set 1). */
typedef unsigned char  u8;
typedef unsigned short u16;

extern void xk_putchar(char c);          /* fb_console.c */

#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define ST_OUT_FULL 0x01
#define ST_AUX      0x20

#define SC_LCTRL    0x1D
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPS     0x3A
#define SC_TABLE_N  0x3A

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static const char map_plain[SC_TABLE_N] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};

static const char map_shift[SC_TABLE_N] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

static u8 st_shift, st_caps, st_ctrl;
static u8 st_ext;
static u8 st_skip;

void xk_init_keyboard(void) {
    for (int i = 0; i < 32 && (inb(KBD_STATUS) & ST_OUT_FULL); i++) {
        (void)inb(KBD_DATA);
    }
    st_shift = st_caps = st_ctrl = st_ext = st_skip = 0;
}

/* Non-blocking. Returns an ASCII code, or -1 if no key is available. */
int xk_pollkey(void) {
    for (int guard = 0; guard < 64; guard++) {
        u8 status = inb(KBD_STATUS);
        if (!(status & ST_OUT_FULL)) return -1;

        u8 sc = inb(KBD_DATA);
        if (status & ST_AUX) continue;

        if (st_skip) { st_skip--; continue; }
        if (sc == 0xE1) { st_skip = 5; continue; }
        if (sc == 0xE0) { st_ext = 1; continue; }

        int released = sc & 0x80;
        sc &= 0x7F;

        if (st_ext) {
            st_ext = 0;
            if (sc == SC_LCTRL) st_ctrl = !released;
            continue;
        }

        if (sc == SC_LSHIFT || sc == SC_RSHIFT) { st_shift = !released; continue; }
        if (sc == SC_LCTRL)                     { st_ctrl  = !released; continue; }
        if (sc == SC_CAPS)                      { if (!released) st_caps ^= 1; continue; }
        if (released || sc >= SC_TABLE_N)       continue;

        char plain = map_plain[sc];
        char c;
        if (plain >= 'a' && plain <= 'z') {
            c = (st_shift ^ st_caps) ? map_shift[sc] : plain;
            if (st_ctrl) c = plain & 0x1F;
        } else {
            c = st_shift ? map_shift[sc] : plain;
        }
        if (c != 0) return (unsigned char)c;
    }
    return -1;
}

int xk_getkey(void) {
    int c;
    while ((c = xk_pollkey()) < 0) {
        __asm__ volatile ("pause");
    }
    return c;
}

int xk_readline(char *buffer, int max_chars) {
    int idx = 0;
    while (1) {
        int c = xk_getkey();
        if (c == '\n') {
            buffer[idx] = '\0';
            xk_putchar('\n');
            return idx;
        }
        if (c == '\b') {
            if (idx > 0) {
                idx--;
                xk_putchar('\b');
            }
            continue;
        }
        if (c >= 32 && c <= 126 && idx < max_chars - 1) {
            buffer[idx++] = (char)c;
            xk_putchar((char)c);
        }
    }
}
EOF

cat > make/amd64/uefi.mk.tmp << 'EOF'
# make|amd64|uefi.mk
BUILD   := build/amd64-uefi
SRC_DIR := source
EFI_OUT := $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

SRCS := arch/amd64/boot/exord/efi/efi_main.c \
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
        arch/amd64/kernel/xeret/mem/physical.c \
        arch/amd64/kernel/xeret/mem/paging.c \
        arch/amd64/kernel/xeret/mem/token_ledger.c \
        arch/amd64/init/exit.c \
        arch/amd64/apps/xsh/core/shell.c \
        arch/amd64/apps/xsh/utils/form.c \
        arch/amd64/apps/xsh/utils/remove.c \
        arch/amd64/syslib/core/path.c \
        arch/amd64/drivers/storage/exfs.c
OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)

CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar \
          -mno-red-zone -fno-stack-protector -fno-stack-check -Wall -Wextra \
          -I$(SRC_DIR)/kerncall/include \
          -I$(SRC_DIR)/arch/amd64/include \
          -I$(SRC_DIR)/arch/amd64/boot/exord/efi \
          -I$(SRC_DIR)/arch/amd64/kernel/xeret

$(BUILD)/obj/%.o: $(SRC_DIR)/%.c
@@@mkdir -p $(dir $@)
@@clang $(CFLAGS) -c $< -o $@

$(EFI_OUT): $(OBJS)
@@@mkdir -p $(dir $@)
@@lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(OBJS)

TEST_EXFS_OBJS := $(filter-out $(BUILD)/obj/arch/amd64/kernel/xeret/main.o,$(OBJS)) \
                  $(BUILD)/obj/tests/arch/amd64/exfs/exfs_test_main.o

test-exfs: $(TEST_EXFS_OBJS)
@@lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib \
          /out:$(BUILD)/test-exfs.efi $^

.PHONY: efi clean run

efi: $(EFI_OUT)

clean:
@@rm -rf $(BUILD)

IMG  := $(BUILD)/esp.img
OVMF ?= /usr/share/edk2/x64/OVMF.4m.fd

$(IMG): $(EFI_OUT)
@@dd if=/dev/zero of=$@ bs=1M count=64 status=none
@@mkfs.fat -F 32 $@ >/dev/null
@@mmd   -i $@ ::/EFI ::/EFI/BOOT
@@mcopy -i $@ $(EFI_OUT) ::/EFI/BOOT/BOOTX64.EFI

run: $(IMG)
@@qemu-system-x86_64 -bios $(OVMF) -drive format=raw,file=$(IMG) -m 256M
