#include "../ui.h"
#include "wifi_ota.h"
#include <stdio.h>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

lv_obj_t *ui_Screen4 = NULL;

static lv_obj_t *wifi_list;
static lv_obj_t *wifi_status_label;
static lv_obj_t *password_input;
static lv_obj_t *password_ok_button;
static lv_obj_t *keyboard;
static lv_obj_t *scan_button;
static lv_obj_t *connect_button;
static lv_obj_t *update_status_label;
static lv_obj_t *download_bar;
static lv_obj_t *firmware_version_label;
static lv_obj_t *wifi_arcs[4];
static uint32_t last_scan_generation = UINT32_MAX;
static uint32_t last_ui_update_ms;
static bool keyboard_visible;
static bool previous_wifi_connected;

static void create_wifi_icon(void)
{
    static const lv_coord_t arc_sizes[] = {14, 24, 34, 44};
    const lv_coord_t icon_center_x = 396;
    const lv_coord_t icon_bottom_y = 100;

    for(size_t index = 0; index < sizeof(arc_sizes) / sizeof(arc_sizes[0]); index++) {
        const lv_coord_t size = arc_sizes[index];
        lv_obj_t *arc = lv_arc_create(ui_Screen4);
        lv_obj_set_size(arc, size, size);
        lv_obj_set_pos(arc, icon_center_x - size / 2, icon_bottom_y - size);
        lv_arc_set_bg_angles(arc, 210, 330);
        lv_arc_set_angles(arc, 210, 330);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        wifi_arcs[index] = arc;
    }
}

static void set_keyboard_visible(bool visible)
{
    keyboard_visible = visible;
    if(visible) {
        lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scan_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(password_ok_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(update_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 64);
        lv_obj_align(password_ok_button, LV_ALIGN_TOP_MID, 0, 112);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, password_input);
    } else {
        lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scan_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(password_ok_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(update_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 206);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void scan_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) {
        wifi_ota_start_scan();
        lv_label_set_text(wifi_status_label, "Suche nach WLANs...");
    }
}

static void password_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) {
        set_keyboard_visible(true);
    }
}

static void password_ok_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) {
        set_keyboard_visible(false);
    }
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        set_keyboard_visible(false);
    }
}

static void connect_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    char selected_ssid[33] = {0};
    lv_dropdown_get_selected_str(wifi_list, selected_ssid, sizeof(selected_ssid));
    if(selected_ssid[0] == '\0' || selected_ssid[0] == '(') {
        lv_label_set_text(wifi_status_label, "Bitte zuerst ein WLAN auswählen.");
        return;
    }

    wifi_ota_connect(selected_ssid, lv_textarea_get_text(password_input));
    lv_label_set_text(wifi_status_label, "Verbindung wird hergestellt...");
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen4) {
        return;
    }

    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        wifi_ota_screen_enter();
        return;
    }

    if(lv_event_get_code(event) != LV_EVENT_GESTURE || keyboard_visible) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen3_screen_init);
    } else if(direction == LV_DIR_LEFT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen1_screen_init);
    }
}

void ui_Screen4_screen_init(void)
{
    ui_Screen4 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_Screen4, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_Screen4);
    lv_label_set_text(title, "WLAN / OTA");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    wifi_status_label = lv_label_create(ui_Screen4);
    lv_label_set_long_mode(wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(wifi_status_label, 270, 58);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xB8D7FF), LV_PART_MAIN);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 42);
    create_wifi_icon();

    wifi_list = lv_dropdown_create(ui_Screen4);
    lv_obj_set_width(wifi_list, 290);
    lv_obj_set_height(wifi_list, 42);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 108);
    lv_dropdown_set_options(wifi_list, "(Suche läuft...)");
    lv_obj_set_style_text_font(wifi_list, &lv_font_montserrat_14, LV_PART_MAIN);

    scan_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(scan_button, 142, 38);
    lv_obj_align(scan_button, LV_ALIGN_TOP_MID, 0, 156);
    lv_obj_add_event_cb(scan_button, scan_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "WLANs suchen");
    lv_obj_center(scan_label);

    password_input = lv_textarea_create(ui_Screen4);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, false);
    lv_textarea_set_placeholder_text(password_input, "WLAN-Passwort");
    lv_textarea_set_max_length(password_input, 63);
    lv_obj_set_size(password_input, 290, 42);
    lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 206);
    lv_obj_add_event_cb(password_input, password_event, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_font(password_input, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(password_input, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(password_input, lv_color_hex(0x202830), LV_PART_MAIN);
    lv_obj_set_style_border_color(password_input, lv_color_hex(0x69A7FF), LV_PART_MAIN);

    password_ok_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(password_ok_button, 80, 36);
    lv_obj_align(password_ok_button, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_add_event_cb(password_ok_button, password_ok_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *password_ok_label = lv_label_create(password_ok_button);
    lv_label_set_text(password_ok_label, "OK");
    lv_obj_center(password_ok_label);
    lv_obj_add_flag(password_ok_button, LV_OBJ_FLAG_HIDDEN);

    connect_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(connect_button, 160, 40);
    lv_obj_align(connect_button, LV_ALIGN_TOP_MID, 0, 254);
    lv_obj_add_event_cb(connect_button, connect_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Verbinden");
    lv_obj_center(connect_label);

    update_status_label = lv_label_create(ui_Screen4);
    lv_label_set_text(update_status_label, "OTA: wartet auf WLAN");
    lv_label_set_long_mode(update_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(update_status_label, 270, 38);
    lv_obj_set_style_text_align(update_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(update_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(update_status_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(update_status_label, LV_ALIGN_TOP_MID, 0, 322);

    download_bar = lv_bar_create(ui_Screen4);
    lv_obj_set_size(download_bar, 270, 14);
    lv_bar_set_range(download_bar, 0, 100);
    lv_bar_set_value(download_bar, 0, LV_ANIM_OFF);
    lv_obj_align(download_bar, LV_ALIGN_TOP_MID, 0, 364);

    firmware_version_label = lv_label_create(ui_Screen4);
    char version_text[48];
    snprintf(version_text, sizeof(version_text), "Installiert: %s", APP_VERSION);
    lv_label_set_text(firmware_version_label, version_text);
    lv_obj_set_style_text_font(firmware_version_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(firmware_version_label, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_align(firmware_version_label, LV_ALIGN_BOTTOM_MID, 0, -36);

    keyboard = lv_keyboard_create(ui_Screen4);
    lv_obj_set_size(keyboard, 300, 180);
    lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 164);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    keyboard_visible = false;
    previous_wifi_connected = false;
    last_scan_generation = UINT32_MAX;
    last_ui_update_ms = 0;
    lv_obj_add_event_cb(ui_Screen4, screen_event, LV_EVENT_ALL, NULL);
}

void ui_Screen4_screen_destroy(void)
{
    if(ui_Screen4) lv_obj_del(ui_Screen4);
    ui_Screen4 = NULL;
    wifi_list = NULL;
    wifi_status_label = NULL;
    password_input = NULL;
    keyboard = NULL;
    password_ok_button = NULL;
    scan_button = NULL;
    connect_button = NULL;
    update_status_label = NULL;
    download_bar = NULL;
    firmware_version_label = NULL;
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        wifi_arcs[index] = NULL;
    }
    keyboard_visible = false;
}

void ui_screen4_show(void)
{
    _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen4_screen_init);
}

void ui_screen4_update(void)
{
    if(ui_Screen4 == NULL) return;

    const uint32_t now = lv_tick_get();
    if(now - last_ui_update_ms < 250) return;
    last_ui_update_ms = now;

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        const bool active = snapshot.connected &&
                            snapshot.signal_percent >= (int)(index * 25);
        lv_obj_set_style_arc_color(wifi_arcs[index],
                                   lv_color_hex(active ? 0x55E58A : 0x505050),
                                   LV_PART_MAIN);
    }
    if(snapshot.connected) {
        char connected_text[96];
        snprintf(connected_text, sizeof(connected_text),
                 "WLAN VERBUNDEN (%d%%)\n%s\n%s",
                 snapshot.signal_percent, snapshot.ssid, snapshot.ip);
        lv_label_set_text(wifi_status_label, connected_text);
        lv_obj_set_style_text_color(wifi_status_label,
                                    lv_color_hex(0x55E58A), LV_PART_MAIN);
    } else {
        lv_label_set_text(wifi_status_label, snapshot.wifi_status);
        lv_obj_set_style_text_color(wifi_status_label,
                                    lv_color_hex(0xB8D7FF), LV_PART_MAIN);
    }
    lv_label_set_text(update_status_label, snapshot.update_status);
    lv_bar_set_value(download_bar, snapshot.progress, LV_ANIM_OFF);

    if(snapshot.scan_generation != last_scan_generation) {
        last_scan_generation = snapshot.scan_generation;
        const int network_count = wifi_ota_network_count();
        lv_dropdown_clear_options(wifi_list);
        if(network_count == 0) {
            lv_dropdown_add_option(wifi_list, "(keine WLANs gefunden)", LV_DROPDOWN_POS_LAST);
        } else {
            char ssid[33];
            for(int index = 0; index < network_count; index++) {
                if(wifi_ota_get_network(index, ssid, sizeof(ssid))) {
                    lv_dropdown_add_option(wifi_list, ssid, LV_DROPDOWN_POS_LAST);
                }
            }
        }
        lv_dropdown_set_selected(wifi_list, 0);
    }

    if(snapshot.connected && !previous_wifi_connected) {
        lv_textarea_set_text(password_input, "");
    }
    previous_wifi_connected = snapshot.connected;

    if(snapshot.updating) {
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(connect_button, LV_STATE_DISABLED);
    }
}
