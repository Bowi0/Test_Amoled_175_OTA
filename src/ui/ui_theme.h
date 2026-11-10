#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include "device_settings.h"

#define UI_BACKGROUND_COLOR 0x000000
#define UI_TILE_COLOR 0x3A3A3A

typedef struct {
    uint32_t accent;
    uint32_t button;
    uint32_t light;
} ui_theme_colors_t;

const ui_theme_colors_t *ui_theme_colors(device_theme_t theme);
void ui_theme_style_button(lv_obj_t *button, uint32_t color);
void ui_theme_style_spinner(lv_obj_t *spinner);
lv_obj_t *ui_theme_create_wifi_arc(lv_obj_t *parent, lv_coord_t size,
                                   lv_coord_t x, lv_coord_t y);
void ui_theme_screen_init(void);
void ui_theme_screen_destroy(void);
void ui_theme_screen_show(void);

#endif
