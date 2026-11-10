#include "ui.h"
#include "ui_german_font.h"
#include "ui_theme.h"
#include <stdint.h>

static const ui_theme_colors_t colors[DEVICE_THEME_COUNT] = {
    {0x4FA8FF, 0x064A83, 0xB8D7FF},
    {0xC68C59, 0x674127, 0xE8C4A3},
    {0xBFC8D0, 0x535F6B, 0xE1E7EC},
    {0xE4BB55, 0x755713, 0xF5DEA0},
    {0xCE8B5C, 0x754427, 0xEBC2A5}
};

static const char *names[DEVICE_THEME_COUNT] = {
    "Blau", "Braun", "Silber", "Gold", "Bronze"
};
static lv_obj_t *screen;
static lv_obj_t *buttons[DEVICE_THEME_COUNT];
static lv_obj_t *selection_marks[DEVICE_THEME_COUNT];
static lv_obj_t *status;
static lv_obj_t *back_button;

const ui_theme_colors_t *ui_theme_colors(device_theme_t theme)
{
    if(theme < DEVICE_THEME_BLUE || theme >= DEVICE_THEME_COUNT)
        theme = DEVICE_THEME_BLUE;
    return &colors[theme];
}

void ui_theme_style_button(lv_obj_t *button, uint32_t color)
{
    const lv_color_t base = lv_color_hex(color);
    lv_obj_set_style_bg_color(button, base, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_darken(base, LV_OPA_20),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_mix(base, lv_color_hex(0x30363C), LV_OPA_40),
                              LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DISABLED);
}

void ui_theme_style_spinner(lv_obj_t *spinner)
{
    const lv_coord_t background_width = lv_obj_get_style_arc_width(spinner, LV_PART_MAIN);
    const lv_coord_t indicator_width = lv_obj_get_style_arc_width(spinner, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, LV_MAX(1, (background_width * 7 + 5) / 10),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, LV_MAX(1, (indicator_width * 7 + 5) / 10),
                               LV_PART_INDICATOR);
}

lv_obj_t *ui_theme_create_wifi_arc(lv_obj_t *parent, lv_coord_t size,
                                   lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_bg_angles(arc, 210, 330);
    lv_arc_set_angles(arc, 210, 330);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return arc;
}

static void refresh_selection(void)
{
    ui_theme_style_button(back_button, ui_theme_colors(device_theme())->button);
    for(int i = 0; i < DEVICE_THEME_COUNT; i++) {
        const bool selected = device_theme() == (device_theme_t)i;
        lv_obj_set_style_border_width(buttons[i], selected ? 3 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(buttons[i],
                                     lv_color_hex(selected ? 0xFFFFFF : 0x52616C),
                                     LV_PART_MAIN);
        if(selected) lv_obj_clear_flag(selection_marks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(selection_marks[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void select_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const device_theme_t choice = (device_theme_t)(intptr_t)lv_event_get_user_data(event);
    if(!device_set_theme(choice)) {
        lv_label_set_text(status, "Thema konnte nicht gespeichert werden");
        return;
    }
    lv_label_set_text(status, "");
    refresh_selection();
}

static void back_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_scr_load(ui_Screen6);
}

void ui_theme_screen_init(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Farbthema");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);

    for(int i = 0; i < DEVICE_THEME_COUNT; i++) {
        const lv_coord_t x = i < 3 ? (i - 1) * 100 : (i == 3 ? -50 : 50);
        const lv_coord_t y = i < 3 ? 90 : 190;
        buttons[i] = lv_btn_create(screen);
        lv_obj_set_size(buttons[i], 88, 88);
        lv_obj_align(buttons[i], LV_ALIGN_TOP_MID, x, y);
        ui_theme_style_button(buttons[i], colors[i].accent);
        lv_obj_set_style_radius(buttons[i], 16, LV_PART_MAIN);
        lv_obj_set_style_pad_all(buttons[i], 6, LV_PART_MAIN);
        lv_obj_add_event_cb(buttons[i], select_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        selection_marks[i] = lv_label_create(buttons[i]);
        lv_label_set_text(selection_marks[i], LV_SYMBOL_OK);
        lv_obj_set_style_text_font(selection_marks[i], &lv_font_montserrat_30, LV_PART_MAIN);
        lv_obj_set_style_text_color(selection_marks[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_align(selection_marks[i], LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t *label = lv_label_create(buttons[i]);
        lv_label_set_text(label, names[i]);
        lv_obj_set_style_text_font(label, ui_german_font_14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    status = lv_label_create(screen);
    lv_obj_set_width(status, 260);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(status, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFF8A80), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 290);
    lv_label_set_text(status, "");

    back_button = lv_btn_create(screen);
    lv_obj_set_size(back_button, 160, 52);
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, 0, 351);
    lv_obj_set_ext_click_area(back_button, 6);
    lv_obj_set_style_radius(back_button, 15, LV_PART_MAIN);
    lv_obj_add_event_cb(back_button, back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(back_button);
    lv_label_set_text(label, "Zurück");
    lv_obj_set_style_text_font(label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    refresh_selection();
}

void ui_theme_screen_show(void)
{
    lv_label_set_text(status, "");
    refresh_selection();
    lv_scr_load(screen);
}

void ui_theme_screen_destroy(void)
{
    if(screen) lv_obj_del(screen);
    screen = NULL;
}
