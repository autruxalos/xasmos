/******************************************************************************
 * XASMOS - Kernel Utility & I/O Library (Xeret Support Functions)
 * Path: source|arch|amd64|kernel|xeret|kernel_lib.c
 ******************************************************************************/

static unsigned short *vga_buffer = (unsigned short*) 0xB8000;
static int cursor_row = 2; // Leave space for the boot banner on row 0-1
static int cursor_col = 0;

// Simple string printing to VGA buffer
void xk_print(const char *str) {
    int i = 0;
    while (str[i] != '\0') {
        if (str[i] == '\n') {
            cursor_row++;
            cursor_col = 0;
        } else {
            int index = cursor_row * 80 + cursor_col;
            vga_buffer[index] = (unsigned short)0x0F00 | (unsigned short)str[i]; // White on black
            cursor_col++;
            if (cursor_col >= 80) {
                cursor_col = 0;
                cursor_row++;
            }
        }
        i++;
    }
}

int xk_println(const char *str) {
    xk_print(str);
    cursor_row++;
    cursor_col = 0;
    return 0;
}

// Standard string comparison
int xk_strcmp(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
        i++;
    }
    return s1[i] - s2[i];
}

// Basic port input inline helper for x86_64
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Basic PS/2 keyboard scancode to ASCII map (minimal set)
static char scancode_to_ascii(unsigned char scancode) {
    // Basic mapping for alphanumeric keys
    static const char ascii_table[128] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    if (scancode < 128) {
        return ascii_table[scancode];
    }
    return 0;
}

// Minimal keyboard polling read line implementation
int xk_readline(char *buffer, int max_chars) {
    int idx = 0;
    while (idx < max_chars - 1) {
        // Poll keyboard status port 0x64
        while ((inb(0x64) & 0x01) == 0) {
            // Wait for key press
        }
        unsigned char scancode = inb(0x60);
        if (scancode & 0x80) {
            continue; // Ignore key release events
        }

        char c = scancode_to_ascii(scancode);
        if (c == '\n') {
            buffer[idx] = '\0';
            xk_print("\n");
            return idx;
        } else if (c == '\b') {
            if (idx > 0) {
                idx--;
                cursor_col--;
                int index = cursor_row * 80 + cursor_col;
                vga_buffer[index] = (unsigned short)0x0F00 | ' ';
            }
        } else if (c >= 32 && c <= 126) {
            buffer[idx++] = c;
            char tmp[2] = {c, '\0'};
            xk_print(tmp);
        }
    }
    buffer[idx] = '\0';
    return idx;
}
