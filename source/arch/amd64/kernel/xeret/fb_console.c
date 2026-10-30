/******************************************************************************
 * XASMOS - Linear framebuffer console (GOP)
 * Path: source|arch|amd64|kernel|xeret|fb_console.c
 ******************************************************************************/
#include "efi_types.h"
#include "font8x16.h"

static FramebufferInfo fb_info;
static UINT32 cols, rows;
static UINT32 cursor_col, cursor_row;

#define FB_COLOR_WHITE 0x00FFFFFFu
#define FB_COLOR_BLACK 0x00000000u

static UINT32 pack_pixel(UINT32 rgb) {
    UINT8 r = (UINT8)((rgb >> 16) & 0xFF);
    UINT8 g = (UINT8)((rgb >> 8) & 0xFF);
    UINT8 b = (UINT8)(rgb & 0xFF);
    if (fb_info.pixel_format == 1) {
        return (UINT32)b | ((UINT32)g << 8) | ((UINT32)r << 16);
    }
    return (UINT32)r | ((UINT32)g << 8) | ((UINT32)b << 16);
}

static void put_pixel(UINT32 x, UINT32 y, UINT32 rgb) {
    UINT32 *row;
    if (x >= fb_info.width || y >= fb_info.height) {
        return;
    }
    row = (UINT32 *)(fb_info.base + (UINT64)y * fb_info.pixels_per_scanline * 4);
    row[x] = pack_pixel(rgb);
}

static void scroll_fb(void) {
    UINT8 *base = (UINT8 *)(UINTN)fb_info.base;
    UINT32 stride = fb_info.pixels_per_scanline * 4;
    UINT32 row_px = FONT_CELL_H;
    UINT64 move_bytes = (UINT64)(fb_info.height - row_px) * stride;
    UINT64 i;
    UINT32 y, x;

    for (i = 0; i < move_bytes; i++) {
        base[i] = base[i + (UINT64)row_px * stride];
    }
    for (y = fb_info.height - row_px; y < fb_info.height; y++) {
        for (x = 0; x < fb_info.width; x++) {
            put_pixel(x, y, FB_COLOR_BLACK);
        }
    }
}

static void draw_glyph(UINT32 cell_col, UINT32 cell_row, char c, UINT32 fg, UINT32 bg) {
    UINT32 ox = cell_col * FONT_CELL_W;
    UINT32 oy = cell_row * FONT_CELL_H;
    const UINT8 *glyph;
    static const UINT8 blank[16] = {0};
    UINT32 ry, rx;

    if (c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR) {
        glyph = font8x16[(int)c - FONT_FIRST_CHAR];
    } else {
        glyph = blank;
    }
    for (ry = 0; ry < FONT_CELL_H; ry++) {
        UINT8 bits = glyph[ry];
        for (rx = 0; rx < FONT_CELL_W; rx++) {
            UINT32 on = (bits >> (7 - rx)) & 1u;
            put_pixel(ox + rx, oy + ry, on ? fg : bg);
        }
    }
}

void fb_console_init(FramebufferInfo *fb) {
    UINT32 y, x;
    fb_info = *fb;
    cols = fb_info.width / FONT_CELL_W;
    rows = fb_info.height / FONT_CELL_H;
    cursor_col = 0;
    cursor_row = 0;
    for (y = 0; y < fb_info.height; y++) {
        for (x = 0; x < fb_info.width; x++) {
            put_pixel(x, y, FB_COLOR_BLACK);
        }
    }
}

static void advance_cursor(void) {
    cursor_col++;
    if (cursor_col >= cols) {
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= rows) {
        scroll_fb();
        cursor_row = rows - 1;
    }
}

/* Clears the whole framebuffer and homes the cursor. */
void xk_clear(void) {
    for (UINT32 y = 0; y < fb_info.height; y++)
        for (UINT32 x = 0; x < fb_info.width; x++)
            put_pixel(x, y, FB_COLOR_BLACK);
    cursor_col = 0;
    cursor_row = 0;
}

void xk_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= rows) {
            scroll_fb();
            cursor_row = rows - 1;
        }
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = cols - 1;
        }
        draw_glyph(cursor_col, cursor_row, ' ', FB_COLOR_WHITE, FB_COLOR_BLACK);
        return;
    }
    draw_glyph(cursor_col, cursor_row, c, FB_COLOR_WHITE, FB_COLOR_BLACK);
    advance_cursor();
}

void xk_print(const char *str) {
    int i;
    for (i = 0; str[i] != '\0'; i++) {
        xk_putchar(str[i]);
    }
}

int xk_println(const char *str) {
    xk_print(str);
    xk_putchar('\n');
    return 0;
}
