#include "../ui.h"
#include "../ui_theme.h"
#include "../ui_german_font.h"
#include "uart_switch.h"
#include <stdint.h>

lv_obj_t *ui_Switches;
static lv_obj_t *buttons[4];
static lv_obj_t *status;
static lv_obj_t *connection_icon;
static uint32_t last_update;

static void switch_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED &&
       !uart_switch_toggle((unsigned)(uintptr_t)lv_event_get_user_data(event)) &&
       !uart_switch_online())
        lv_label_set_text(status, "Keine Verbindung");
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_get_act();
    if(!indev) return;
    lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        lv_scr_load(direction == LV_DIR_LEFT ? ui_Clock : ui_Screen1);
    }
}

void ui_switches_init(void)
{
    ui_Switches = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Switches, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Switches, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(ui_Switches);
    lv_label_set_text(title, "Ausgänge");
    lv_obj_set_style_text_font(title, ui_german_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);
    for(unsigned i = 0; i < 4; i++) {
        buttons[i] = lv_btn_create(ui_Switches);
        lv_obj_set_size(buttons[i], 112, 112);
        lv_obj_align(buttons[i], LV_ALIGN_TOP_MID, (i % 2 ? 62 : -62), 95 + (i / 2) * 124);
        lv_obj_set_style_radius(buttons[i], 8, LV_PART_MAIN);
        lv_obj_add_event_cb(buttons[i], switch_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(buttons[i]);
        lv_label_set_text_fmt(label, "GPIO %u", i + 1);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(label);
    }
    lv_obj_t *status_row = lv_obj_create(ui_Switches);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_row, LV_ALIGN_TOP_MID, 0, 343);
    connection_icon = lv_label_create(status_row);
    lv_obj_set_style_text_font(connection_icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(connection_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(connection_icon, lv_color_hex(0xFF5252), LV_PART_MAIN);
    status = lv_label_create(status_row);
    lv_obj_set_style_text_font(status, ui_german_font_18(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(status, "Keine Verbindung");
    lv_obj_add_event_cb(ui_Switches, screen_event, LV_EVENT_ALL, NULL);
}

void ui_switches_update(void)
{
    if(!ui_Switches || lv_scr_act() != ui_Switches) return;
    if(lv_tick_elaps(last_update) < 100) return;
    last_update = lv_tick_get();
    const bool online = uart_switch_online();
    const uint8_t state = uart_switch_state();
    lv_label_set_text(connection_icon, online ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(connection_icon,
                                lv_color_hex(online ? 0x4CAF50 : 0xFF5252),
                                LV_PART_MAIN);
    lv_label_set_text(status, !online ? "Keine Verbindung" :
                      uart_switch_pending() ? "Warte auf Bestätigung" : "Verbunden");
    for(unsigned i = 0; i < 4; i++)
        ui_theme_style_button(buttons[i], online && (state & (1U << i))
                              ? ui_theme_colors(device_theme())->button : UI_TILE_COLOR);
}

void ui_switches_destroy(void)
{
    if(ui_Switches) lv_obj_del(ui_Switches);
    ui_Switches = NULL;
}
