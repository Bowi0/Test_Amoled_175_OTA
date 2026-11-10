#include "../ui.h"
#include "../ui_german_font.h"
#include "../ui_theme.h"
#include "ble_manager.h"
#include "device_settings.h"
#include "wifi_ota.h"

lv_obj_t *ui_Screen6;
static lv_obj_t *previous_screen;
static lv_obj_t *wifi_arcs[4];
static lv_obj_t *wifi_dot;
static lv_obj_t *wifi_disabled_line;
static lv_obj_t *bluetooth_icon;
static lv_obj_t *brightness_slider;
static lv_obj_t *update_icon;
static lv_obj_t *theme_icon;
static lv_obj_t *status_label;
static bool wifi_long_pressed;
static bool startup_pending;
static bool startup_refresh_started;
static uint32_t last_update;

static void update_wifi_icon(void)
{
    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    const bool enabled = device_wifi_enabled();
    unsigned active = 0;
    if(enabled && snapshot.connected) {
        active = (unsigned)snapshot.signal_percent / 25;
    } else if(enabled && snapshot.connecting) {
        active = (lv_tick_get() / 300) % 5;
    }

    const uint32_t accent = ui_theme_colors(device_theme())->accent;
    for(unsigned i = 0; i < 4; i++) {
        lv_obj_set_style_arc_color(wifi_arcs[i],
                                   lv_color_hex(i < active ? accent : 0x52616C),
                                   LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(enabled ? accent : 0x52616C),
                              LV_PART_MAIN);
    if(enabled) lv_obj_add_flag(wifi_disabled_line, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(wifi_disabled_line, LV_OBJ_FLAG_HIDDEN);
}

enum {
    SETTINGS_TILE_SIZE = 104,
    SETTINGS_TILE_GAP = 12,
    SETTINGS_TILE_X = (SETTINGS_TILE_SIZE + SETTINGS_TILE_GAP) / 2,
    SETTINGS_TILE_TOP = 76,
    SETTINGS_TILE_ROW_STEP = SETTINGS_TILE_SIZE + SETTINGS_TILE_GAP
};

void ui_settings_startup(void)
{
    startup_pending = true;
    startup_refresh_started = false;
}

void ui_quick_settings_gesture(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_GESTURE ||
       lv_scr_act() == ui_Screen6) return;
    if(lv_scr_act() == ui_Screen4 && !ui_screen4_can_open_quick_settings()) return;
    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL || lv_indev_get_gesture_dir(indev) != LV_DIR_BOTTOM) return;
    previous_screen = lv_scr_act();
    lv_indev_wait_release(indev);
    lv_scr_load(ui_Screen6);
}

static void wifi_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) wifi_long_pressed = false;
    else if(code == LV_EVENT_LONG_PRESSED) {
        wifi_long_pressed = true;
        if(!device_wifi_enabled() && !wifi_ota_set_enabled(true)) {
            lv_label_set_text(status_label, "WLAN konnte nicht aktiviert werden");
            return;
        }
        ui_screen4_show();
    } else if(code == LV_EVENT_CLICKED && !wifi_long_pressed) {
        if(!wifi_ota_set_enabled(!device_wifi_enabled()))
            lv_label_set_text(status_label, "WLAN-Einstellung fehlgeschlagen");
        else lv_label_set_text(status_label, "");
    }
}

static void bluetooth_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if(!ble_set_enabled(!ble_is_active()))
        lv_label_set_text(status_label, "Bluetooth-Einstellung fehlgeschlagen");
    else lv_label_set_text(status_label, "");
}

static void update_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_scr_load(ui_Screen5);
}

static void theme_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED)
        ui_theme_screen_show();
}

static void brightness_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;
    const uint8_t value = (uint8_t)lv_slider_get_value(brightness_slider);
    if(!device_set_brightness(value, code == LV_EVENT_RELEASED))
        lv_label_set_text(status_label, "Helligkeit konnte nicht gespeichert werden");
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen6) return;
    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        const ui_theme_colors_t *colors = ui_theme_colors(device_theme());
        lv_slider_set_value(brightness_slider, device_brightness(), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(colors->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(colors->accent), LV_PART_KNOB);
        lv_obj_set_style_text_color(update_icon, lv_color_hex(colors->accent), LV_PART_MAIN);
        lv_obj_set_style_text_color(theme_icon, lv_color_hex(colors->accent), LV_PART_MAIN);
        update_wifi_icon();
        lv_obj_set_style_text_color(bluetooth_icon,
            lv_color_hex(ble_is_active() ? colors->accent : 0x52616C), LV_PART_MAIN);
        lv_label_set_text(status_label, "");
    } else if(lv_event_get_code(event) == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if(indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
            lv_indev_wait_release(indev);
            lv_scr_load(previous_screen ? previous_screen : ui_Screen1);
        }
    }
}

static lv_obj_t *create_icon_button(lv_coord_t x, lv_coord_t y, const char *symbol,
                                    const char *caption, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(ui_Screen6);
    lv_obj_set_size(button, SETTINGS_TILE_SIZE, SETTINGS_TILE_SIZE);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, y);
    ui_theme_style_button(button, UI_TILE_COLOR);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, NULL);
    lv_obj_t *icon = lv_label_create(button);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_label_set_text(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(label, caption);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    return icon;
}

void ui_Screen6_screen_init(void)
{
    ui_Screen6 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen6, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(ui_Screen6);
    lv_label_set_text(title, "Einstellungen");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_t *wifi_icon = create_icon_button(-SETTINGS_TILE_X, SETTINGS_TILE_TOP,
                                            LV_SYMBOL_WIFI, "WLAN", wifi_event);
    lv_obj_t *wifi_button = lv_obj_get_parent(wifi_icon);
    lv_obj_add_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN);
    static const lv_coord_t wifi_arc_sizes[] = {18, 30, 42, 54};
    const lv_coord_t wifi_center_x = (SETTINGS_TILE_SIZE - 12) / 2;
    const lv_coord_t wifi_center_y = 46;
    for(unsigned i = 0; i < 4; i++) {
        const lv_coord_t size = wifi_arc_sizes[i];
        wifi_arcs[i] = ui_theme_create_wifi_arc(wifi_button, size,
                                               wifi_center_x - size / 2,
                                               wifi_center_y - size / 2);
        lv_obj_set_style_arc_width(wifi_arcs[i], 3, LV_PART_MAIN);
    }
    wifi_dot = lv_obj_create(wifi_button);
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_size(wifi_dot, 6, 6);
    lv_obj_set_pos(wifi_dot, wifi_center_x - 3, wifi_center_y - 3);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(wifi_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    static lv_point_t wifi_disabled_points[] = {{0, 0}, {50, 40}};
    wifi_disabled_line = lv_line_create(wifi_button);
    lv_line_set_points(wifi_disabled_line, wifi_disabled_points, 2);
    lv_obj_set_pos(wifi_disabled_line, wifi_center_x - 25, 14);
    lv_obj_set_style_line_color(wifi_disabled_line, lv_color_hex(0x52616C), LV_PART_MAIN);
    lv_obj_set_style_line_width(wifi_disabled_line, 3, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(wifi_disabled_line, true, LV_PART_MAIN);
    lv_obj_clear_flag(wifi_disabled_line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    bluetooth_icon = create_icon_button(SETTINGS_TILE_X, SETTINGS_TILE_TOP, LV_SYMBOL_BLUETOOTH, "Bluetooth", bluetooth_event);
    update_icon = create_icon_button(-SETTINGS_TILE_X, SETTINGS_TILE_TOP + SETTINGS_TILE_ROW_STEP,
                                     LV_SYMBOL_REFRESH, "Update", update_event);
    theme_icon = create_icon_button(SETTINGS_TILE_X, SETTINGS_TILE_TOP + SETTINGS_TILE_ROW_STEP,
                                    LV_SYMBOL_TINT, "Farbthema", theme_event);

    lv_obj_t *brightness_label = lv_label_create(ui_Screen6);
    lv_label_set_text(brightness_label, "Helligkeit");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 315);
    brightness_slider = lv_slider_create(ui_Screen6);
    lv_obj_set_size(brightness_slider, 270, 18);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 354);
    lv_slider_set_range(brightness_slider, DEVICE_BRIGHTNESS_MIN, DEVICE_BRIGHTNESS_MAX);
    lv_obj_clear_flag(brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(brightness_slider, brightness_event, LV_EVENT_ALL, NULL);

    status_label = lv_label_create(ui_Screen6);
    lv_obj_set_width(status_label, 240);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF8A80), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 384);
    lv_label_set_text(status_label, "");
    lv_obj_add_event_cb(ui_Screen6, screen_event, LV_EVENT_ALL, NULL);
}

void ui_Screen6_screen_destroy(void)
{
    if(ui_Screen6) lv_obj_del(ui_Screen6);
    ui_Screen6 = NULL;
    previous_screen = NULL;
}

void ui_screen6_update(void)
{
    if(startup_pending) {
        lv_disp_t *display = lv_disp_get_default();
        if(!startup_refresh_started) {
            lv_refr_now(display);
            startup_refresh_started = true;
            return;
        }
        if(lv_disp_get_draw_buf(display)->flushing) return;
        startup_pending = false;
        if(device_wifi_enabled() && !wifi_ota_set_enabled(true))
            lv_label_set_text(status_label, "WLAN konnte nicht gestartet werden");
        if(device_bluetooth_enabled() && !ble_set_enabled(true))
            lv_label_set_text(status_label, "Bluetooth konnte nicht gestartet werden");
    }
    if(ui_Screen6 == NULL || lv_scr_act() != ui_Screen6) return;
    if(lv_tick_elaps(last_update) < 250) return;
    last_update = lv_tick_get();
    const uint32_t accent = ui_theme_colors(device_theme())->accent;
    update_wifi_icon();
    lv_obj_set_style_text_color(bluetooth_icon,
        lv_color_hex(ble_is_active() ? accent : 0x52616C), LV_PART_MAIN);
}
