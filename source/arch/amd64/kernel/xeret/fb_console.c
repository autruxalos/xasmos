/******************************************************************************
 * XASMOS - Linear framebuffer text console (replaces 0xB8000 VGA text mode)
 * Path: source|arch|amd64|kernel|xeret|fb_console.c
 *
 * Required because GOP never provides VGA text mode -- everything is pixels.
 * Must be linked against font8x16.h (auto-generated, see gen script).
 ******************************************************************************/
#include "efi_types.h"
#include "font8x16.h"

/* FramebufferInfo comes from efi_types.h (single shared definition).
 * pixel_format: 0 = RGB8, 1 = BGR8, 2 = bitmask (bitmask not handled here). */
static FramebufferInfo fb_info;
static UINT32 cols, rows;
static UINT32 cursor_col = 0, cursor_row = 0;

#define FB_COLOR_WHITE 0x00FFFFFFu
#define FB_COLOR_BLACK 0x00000000u

/* Packs 0xRRGGBB into the 32-bit word layout this panel's PixelFormat wants.
 * x86_64 is little-endian, so the LOW byte of the returned UINT32 lands at
 * the LOWEST memory address when stored. Getting this wrong is a classic
 * silent UEFI bug: colors render with red/blue swapped instead of crashing.
 *   PixelRedGreenBlueReserved8BitPerColor (0): byte0=R,1=G,2=B -> R | G<<8 | B<<16
 *   PixelBlueGreenRedReserved8BitPerColor (1): byte0=B,1=G,2=R -> B | G<<8 | R<<16 */
static UINT32 pack_pixel(UINT32 rgb) {
    UINT8 r = (rgb >> 16) & 0xFF;
    UINT8 g = (rgb >> 8) & 0xFF;
    UINT8 b = rgb & 0xFF;
    if (fb_info.pixel_format == 1) {
        return (UINT32)b | (UINT32)g << 8 | (UINT32)r << 16;
    }
    return (UINT32)r | (UINT32)g << 8 | (UINT32)b << 16;
}

static void put_pixel(UINT32 x, UINT32 y, UINT32 rgb) {
    if (x >= fb_info.width || y >= fb_info.height) return;
    UINT32 *row = (UINT32 *)(fb_info.base + (UINT64)y * fb_info.pixels_per_scanline * 4);
    row[x] = pack_pixel(rgb);
}

static void scroll_fb(void) {
    UINT8 *base = (UINT8 *)fb_info.base;
    UINT32 stride = fb_info.pixels_per_scanline * 4;
    UINT32 row_px = FONT_CELL_H;
    UINT64 move_bytes = (UINT64)(fb_info.height - row_px) * stride;

    /* Shift the whole framebuffer up by one text row. */
    for (UINT64 i = 0; i < move_bytes; i++) {
        base[i] = base[i + (UINT64)row_px * stride];
    }
    /* Clear the newly exposed bottom row. */
    for (UINT32 y = fb_info.height - row_px; y < fb_info.height; y++) {
        for (UINT32 x = 0; x < fb_info.width; x++) {
            put_pixel(x, y, FB_COLOR_BLACK);
        }
    }
}

static void draw_glyph(UINT32 cell_col, UINT32 cell_row, char c, UINT32 fg, UINT32 bg) {
    UINT32 ox = cell_col * FONT_CELL_W;
    UINT32 oy = cell_row * FONT_CELL_H;

    const UINT8 *glyph;
    static const UINT8 blank[16] = {0};
    if (c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR) {
        glyph = font8x16[(int)c - FONT_FIRST_CHAR];
    } else {
        glyph = blank;
    }

    for (UINT32 ry = 0; ry < FONT_CELL_H; ry++) {
        UINT8 bits = glyph[ry];
        for (UINT32 rx = 0; rx < FONT_CELL_W; rx++) {
            UINT32 on = (bits >> (7 - rx)) & 1;
            put_pixel(ox + rx, oy + ry, on ? fg : bg);
        }
    }
}

void fb_console_init(FramebufferInfo *fb) {
    fb_info = *fb;
    cols = fb_info.width / FONT_CELL_W;
    rows = fb_info.height / FONT_CELL_H;
    cursor_col = 0;
    cursor_row = 0;

    for (UINT32 y = 0; y < fb_info.height; y++)
        for (UINT32 x = 0; x < fb_info.width; x++)
            put_pixel(x, y, FB_COLOR_BLACK);
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
    for (int i = 0; str[i] != '\0'; i++) xk_putchar(str[i]);
}

int xk_println(const char *str) {
    xk_print(str);
    xk_putchar('\n');
    return 0;
}
