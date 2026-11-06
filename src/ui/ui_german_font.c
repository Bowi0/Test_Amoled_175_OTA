#include "ui_german_font.h"

#include <string.h>

#define UMLAUT_EXTRA_HEIGHT 3
#define UMLAUT_BITMAP_SIZE 512

static uint8_t umlaut_bitmap[UMLAUT_BITMAP_SIZE];

static uint32_t base_letter(uint32_t letter)
{
    switch(letter) {
        case 0x00C4: return 'A';
        case 0x00D6: return 'O';
        case 0x00DC: return 'U';
        case 0x00E4: return 'a';
        case 0x00F6: return 'o';
        case 0x00FC: return 'u';
        default: return 0;
    }
}

static bool get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                          uint32_t letter, uint32_t letter_next)
{
    (void)font;
    const uint32_t base = base_letter(letter);
    if(base == 0) {
        if(!lv_font_get_glyph_dsc(&lv_font_montserrat_14, dsc,
                                  letter, letter_next)) {
            return false;
        }
        dsc->ofs_y += UMLAUT_EXTRA_HEIGHT;
        return true;
    }

    uint32_t next_base = base_letter(letter_next);
    if(next_base == 0) next_base = letter_next;
    if(!lv_font_get_glyph_dsc(&lv_font_montserrat_14, dsc, base, next_base)) {
        return false;
    }
    dsc->box_h += UMLAUT_EXTRA_HEIGHT;
    return true;
}

static void set_pixel(uint32_t width, uint32_t x, uint32_t y, uint8_t alpha)
{
    const uint32_t pixel = y * width + x;
    const uint32_t byte_index = pixel >> 1;
    if((pixel & 1U) == 0) {
        umlaut_bitmap[byte_index] =
            (uint8_t)((umlaut_bitmap[byte_index] & 0x0F) | (alpha << 4));
    } else {
        umlaut_bitmap[byte_index] =
            (uint8_t)((umlaut_bitmap[byte_index] & 0xF0) | alpha);
    }
}

static const uint8_t *get_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    (void)font;
    const uint32_t base = base_letter(letter);
    if(base == 0) {
        return lv_font_get_glyph_bitmap(&lv_font_montserrat_14, letter);
    }

    lv_font_glyph_dsc_t dsc;
    if(!lv_font_get_glyph_dsc(&lv_font_montserrat_14, &dsc, base, 0) ||
       dsc.bpp != 4 || dsc.box_w == 0 ||
       (uint32_t)dsc.box_w * (dsc.box_h + UMLAUT_EXTRA_HEIGHT) * 2 >
           UMLAUT_BITMAP_SIZE) {
        return NULL;
    }

    const uint8_t *source = lv_font_get_glyph_bitmap(&lv_font_montserrat_14, base);
    if(source == NULL) return NULL;

    memset(umlaut_bitmap, 0, sizeof(umlaut_bitmap));
    const uint32_t width = dsc.box_w;
    for(uint32_t y = 0; y < dsc.box_h; y++) {
        for(uint32_t x = 0; x < width; x++) {
            const uint32_t source_pixel = y * width + x;
            const uint8_t source_byte = source[source_pixel >> 1];
            const uint8_t alpha = (source_pixel & 1U)
                                      ? source_byte & 0x0F
                                      : source_byte >> 4;
            set_pixel(width, x, y + UMLAUT_EXTRA_HEIGHT, alpha);
        }
    }

    const uint32_t dot_width = width >= 8 ? 2 : 1;
    const uint32_t gap = 2;
    const uint32_t dots_width = dot_width * 2 + gap;
    const uint32_t dot_start = width > dots_width ? (width - dots_width) / 2 : 0;
    for(uint32_t y = 0; y < 2; y++) {
        for(uint32_t x = 0; x < dot_width; x++) {
            set_pixel(width, dot_start + x, y, 0x0F);
            set_pixel(width, dot_start + dot_width + gap + x, y, 0x0F);
        }
    }
    return umlaut_bitmap;
}

static lv_font_t german_font = {
    .get_glyph_dsc = get_glyph_dsc,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = 19,
    .base_line = 3,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = NULL,
    .fallback = NULL,
};

const lv_font_t *ui_german_font_14(void)
{
    return &german_font;
}
