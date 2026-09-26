/* Sharp MIP modifier glyph data. */
#include <stdint.h>
#include <lvgl.h>

const uint8_t sharp_mip_control_bitmap[28] = {
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x07, 0x80,
    0x0c, 0xc0, 0x18, 0x60, 0x30, 0x30, 0x20, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

const uint8_t sharp_mip_shift_bitmap[28] = {
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x07, 0x80,
    0x0c, 0xc0, 0x18, 0x60, 0x30, 0x30, 0x78, 0x78,
    0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x0f, 0xc0,
    0x00, 0x00, 0x00, 0x00,
};

const uint8_t sharp_mip_command_bitmap[28] = {
    0x00, 0x00, 0x00, 0x00, 0x18, 0x60, 0x24, 0x90,
    0x24, 0x90, 0x1f, 0xe0, 0x04, 0x80, 0x04, 0x80,
    0x1f, 0xe0, 0x24, 0x90, 0x24, 0x90, 0x18, 0x60,
    0x00, 0x00, 0x00, 0x00,
};

const uint8_t sharp_mip_option_bitmap[28] = {
    0x00, 0x00, 0x00, 0x00, 0x3c, 0xe0, 0x3c, 0xe0,
    0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x03, 0x00, 0x01, 0xe0, 0x01, 0xe0,
    0x00, 0x00, 0x00, 0x00,
};

/* Alt glyph: existing Option bitmap mirrored top to bottom. */
const uint8_t sharp_mip_alt_bitmap[28] = {
    0x00, 0x00,
    0x00, 0x00,
    0x01, 0xe0,
    0x01, 0xe0,
    0x03, 0x00,
    0x03, 0x00,
    0x03, 0x00,
    0x06, 0x00,
    0x06, 0x00,
    0x06, 0x00,
    0x3c, 0xe0,
    0x3c, 0xe0,
    0x00, 0x00,
    0x00, 0x00,
};

/* Windows logo bitmap adapted from mctechnology17/zmk-nice-oled. */
const uint8_t sharp_mip_windows_bitmap[28] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x0b, 0xf0,
    0x3b, 0xf0, 0x3b, 0xf0, 0x3b, 0xf0, 0x00, 0x00,
    0x3b, 0xf0, 0x3b, 0xf0, 0x3b, 0xf0, 0x03, 0xf0,
    0x00, 0x30, 0x00, 0x00,
};

#define CAPS_LOCK_GLYPH_WIDTH 14
#define CAPS_LOCK_GLYPH_HEIGHT 18

/* Build Caps Lock from the existing Shift glyph plus a key outline below it. */
static uint8_t caps_lock_glyph_bitmap[CAPS_LOCK_GLYPH_WIDTH * CAPS_LOCK_GLYPH_HEIGHT];
static bool caps_lock_glyph_bitmap_initialized;

static void init_caps_lock_glyph_bitmap(void) {
    if (caps_lock_glyph_bitmap_initialized) {
        return;
    }

    // Shorten the Shift arrow's shaft by two pixels while keeping its bottom bar visible.
    for (uint8_t row = 0; row < 9; row++) {
        for (uint8_t col = 0; col < 14; col++) {
            if (sharp_mip_shift_bitmap[row * 2 + col / 8] & (0x80 >> (col % 8))) {
                caps_lock_glyph_bitmap[row * CAPS_LOCK_GLYPH_WIDTH + col] = 255;
            }
        }
    }

    for (uint8_t col = 4; col <= 9; col++) {
        caps_lock_glyph_bitmap[9 * CAPS_LOCK_GLYPH_WIDTH + col] = 255;
        caps_lock_glyph_bitmap[11 * CAPS_LOCK_GLYPH_WIDTH + col] = 255;
        caps_lock_glyph_bitmap[13 * CAPS_LOCK_GLYPH_WIDTH + col] = 255;
    }
    caps_lock_glyph_bitmap[12 * CAPS_LOCK_GLYPH_WIDTH + 4] = 255;
    caps_lock_glyph_bitmap[12 * CAPS_LOCK_GLYPH_WIDTH + 9] = 255;
    caps_lock_glyph_bitmap_initialized = true;
}

static bool caps_lock_glyph_get_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *glyph,
                                    uint32_t letter, uint32_t letter_next) {
    (void)letter_next;
    if (letter != 0x21EA) {
        return false;
    }

    glyph->resolved_font = font;
    glyph->adv_w = CAPS_LOCK_GLYPH_WIDTH * 16;
    glyph->box_w = CAPS_LOCK_GLYPH_WIDTH;
    glyph->box_h = CAPS_LOCK_GLYPH_HEIGHT;
    glyph->ofs_x = 0;
    glyph->ofs_y = 0;
    glyph->bpp = 8;
    glyph->is_placeholder = 0;
    return true;
}

static const uint8_t *caps_lock_glyph_get_bitmap(const lv_font_t *font, uint32_t letter) {
    (void)font;
    if (letter != 0x21EA) {
        return NULL;
    }
    init_caps_lock_glyph_bitmap();
    return caps_lock_glyph_bitmap;
}

const lv_font_t sharp_mip_caps_lock_font = {
    .get_glyph_dsc = caps_lock_glyph_get_dsc,
    .get_glyph_bitmap = caps_lock_glyph_get_bitmap,
    .line_height = 18,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = NULL,
    .fallback = NULL,
};
