#include "../ui.h"
#include "../ui_german_font.h"
#include "wifi_ota.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

lv_obj_t *ui_Screen4 = NULL;

static lv_obj_t *wifi_list;
static lv_obj_t *password_panel;
static lv_obj_t *password_network_label;
static lv_obj_t *password_input;
static lv_obj_t *password_error_label;
static lv_obj_t *keyboard;
static lv_obj_t *connect_button;
static lv_obj_t *cancel_button;
static lv_obj_t *password_ok_button;
static lv_obj_t *list_heading;
static lv_obj_t *wifi_arcs[4];
static uint32_t last_scan_generation = UINT32_MAX;
static uint32_t last_ui_update_ms;
static uint32_t last_auto_scan_ms;
#define WIFI_SCAN_INTERVAL_MS 15000
static bool keyboard_visible;
static bool previous_wifi_connected;
static bool wifi_ota_initialized;
static bool selected_network_secured;
static char selected_ssid[33];

static void set_button_style(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color),
                              LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 15, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_ANY);
}

static void create_wifi_icon(void)
{
    static const lv_coord_t arc_sizes[] = {10, 17, 24, 32};
    const lv_coord_t icon_center_x = 374;
    const lv_coord_t icon_bottom_y = 94;

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
        lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        wifi_arcs[index] = arc;
    }
}

static void set_password_view(bool visible)
{
    keyboard_visible = visible;
    if(visible) {
        lv_obj_add_flag(list_heading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
            lv_obj_add_flag(wifi_arcs[index], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, password_input);
    } else {
        lv_obj_clear_flag(list_heading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
            lv_obj_clear_flag(wifi_arcs[index], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void draw_signal_bars(lv_obj_t *row, int signal_percent)
{
    for(int index = 0; index < 4; index++) {
        const int threshold = (index + 1) * 25;
        const lv_coord_t height = (lv_coord_t)(5 + index * 3);
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 4, height);
        lv_obj_set_pos(bar, 268 + index * 7, 27 - height);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar,
                                  lv_color_hex(signal_percent >= threshold
                                                   ? 0x4FA8FF : 0x394550),
                                  LV_PART_MAIN);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void network_row_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    const int index = (int)(intptr_t)lv_event_get_user_data(event);
    char ssid[33] = {0};
    int signal_percent = 0;
    bool secured = false;
    if(!wifi_ota_get_network(index, ssid, sizeof(ssid), &signal_percent, &secured)) {
        return;
    }

    strlcpy(selected_ssid, ssid, sizeof(selected_ssid));
    selected_network_secured = secured;

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    int row_index = 0;
    for(int network_index = 0; network_index < wifi_ota_network_count(); network_index++) {
        char network_ssid[33] = {0};
        if(!wifi_ota_get_network(network_index, network_ssid,
                                 sizeof(network_ssid), NULL, NULL)) {
            continue;
        }

        lv_obj_t *row = lv_obj_get_child(wifi_list, row_index++);
        if(row == NULL) continue;
        const bool connected = snapshot.connected &&
                               strcmp(snapshot.ssid, network_ssid) == 0;
        const bool selected = strcmp(selected_ssid, network_ssid) == 0;
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(connected ? 0x102B46 : 0x17232C),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_width(row, connected || selected ? 2 : 1,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(connected || selected
                                                       ? 0x4FA8FF : 0x263540),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(row, 0),
                                    lv_color_hex(connected ? 0x78BDFF : 0xFFFFFF),
                                    LV_PART_MAIN);
    }
    lv_obj_clear_state(connect_button, LV_STATE_DISABLED);
}

static void refresh_network_list(bool scanning)
{
    char previous_selection[sizeof(selected_ssid)];
    strlcpy(previous_selection, selected_ssid, sizeof(previous_selection));
    lv_obj_clean(wifi_list);
    const int network_count = wifi_ota_network_count();
    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    if(network_count == 0) {
        lv_obj_t *empty_label = lv_label_create(wifi_list);
        lv_label_set_text(empty_label, scanning ? "Suche nach Netzwerken..."
                                                : "Keine Netzwerke gefunden");
        lv_obj_set_style_text_font(empty_label, ui_german_font_14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
        lv_obj_center(empty_label);
        selected_ssid[0] = '\0';
        selected_network_secured = false;
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
        return;
    }

    selected_ssid[0] = '\0';
    selected_network_secured = false;
    bool selection_found = false;

    for(int index = 0; index < network_count; index++) {
        char ssid[33] = {0};
        int signal_percent = 0;
        bool secured = false;
        if(!wifi_ota_get_network(index, ssid, sizeof(ssid),
                                 &signal_percent, &secured)) {
            continue;
        }

        const bool connected = snapshot.connected &&
                               strcmp(snapshot.ssid, ssid) == 0;
        const bool selected = previous_selection[0] != '\0' &&
                              strcmp(previous_selection, ssid) == 0;
        if(selected) {
            strlcpy(selected_ssid, ssid, sizeof(selected_ssid));
            selected_network_secured = secured;
            selection_found = true;
        }

        lv_obj_t *row = lv_btn_create(wifi_list);
        lv_obj_set_size(row, 302, 36);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(connected ? 0x102B46 : 0x17232C),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, connected || selected ? 2 : 1,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(connected || selected
                                                       ? 0x4FA8FF : 0x263540),
                                      LV_PART_MAIN);
        lv_obj_set_style_radius(row, 11, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
        lv_obj_add_event_cb(row, network_row_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)index);

        lv_obj_t *name_label = lv_label_create(row);
        if(connected) {
            lv_label_set_text_fmt(name_label, "Verbunden  %s", ssid);
        } else {
            lv_label_set_text_fmt(name_label, "%s%s", secured ? "" : "OPEN  ", ssid);
        }
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_label, 240);
        lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_font(name_label, ui_german_font_14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(name_label,
                                    lv_color_hex(connected ? 0x78BDFF : 0xFFFFFF),
                                    LV_PART_MAIN);
        lv_obj_clear_flag(name_label, LV_OBJ_FLAG_CLICKABLE);

        draw_signal_bars(row, signal_percent);
    }

    if(selection_found) {
        lv_obj_clear_state(connect_button, LV_STATE_DISABLED);
    } else {
        selected_ssid[0] = '\0';
        selected_network_secured = false;
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    }
}

static void cancel_password_entry(void)
{
    lv_textarea_set_text(password_input, "");
    lv_label_set_text(password_error_label, "");
    set_password_view(false);
}

static void submit_password(lv_event_t *event)
{
    (void)event;
    const char *password = lv_textarea_get_text(password_input);
    if(selected_network_secured && strlen(password) < 8) {
        lv_label_set_text(password_error_label, "Mindestens 8 Zeichen eingeben");
        return;
    }

    wifi_ota_connect(selected_ssid, password);
    lv_textarea_set_text(password_input, "");
    lv_label_set_text(password_error_label, "");
    set_password_view(false);
}

static void cancel_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) {
        cancel_password_entry();
    }
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY) {
        submit_password(event);
    } else if(code == LV_EVENT_CANCEL) {
        cancel_password_entry();
    }
}

static void connect_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if(selected_ssid[0] == '\0') {
        return;
    }

    char prompt[40];
    snprintf(prompt, sizeof(prompt), "Netzwerk: %.32s", selected_ssid);
    lv_label_set_text(password_network_label, prompt);
    lv_textarea_set_text(password_input, "");
    lv_label_set_text(password_error_label, "");
    set_password_view(true);
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen4) return;

    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        if(!wifi_ota_initialized) {
            wifi_ota_init();
            wifi_ota_initialized = true;
        }
        wifi_ota_screen_enter();
        wifi_ota_start_scan();
        refresh_network_list(true);
        last_auto_scan_ms = lv_tick_get();
        return;
    }

    if(lv_event_get_code(event) != LV_EVENT_GESTURE || keyboard_visible) return;

    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen3_screen_init);
    } else if(direction == LV_DIR_LEFT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen5, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen5_screen_init);
    }
}

void ui_Screen4_screen_init(void)
{
    ui_Screen4 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(0x070B10), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_Screen4, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_Screen4);
    lv_label_set_text(title, "WLAN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);
    create_wifi_icon();

    list_heading = lv_label_create(ui_Screen4);
    lv_label_set_text(list_heading, "VERFÜGBARE NETZWERKE");
    lv_obj_set_style_text_font(list_heading, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(list_heading, lv_color_hex(0x8294A1), LV_PART_MAIN);
    lv_obj_align(list_heading, LV_ALIGN_TOP_MID, 0, 91);

    wifi_list = lv_obj_create(ui_Screen4);
    lv_obj_set_size(wifi_list, 324, 206);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 113);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x0F171E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wifi_list, lv_color_hex(0x25343E), LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_list, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wifi_list, 5, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    connect_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(connect_button, 160, 38);
    lv_obj_align(connect_button, LV_ALIGN_TOP_MID, 0, 340);
    set_button_style(connect_button, 0x064A83);
    lv_obj_add_event_cb(connect_button, connect_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Verbinden");
    lv_obj_center(connect_label);
    lv_obj_add_state(connect_button, LV_STATE_DISABLED);

    password_panel = lv_obj_create(ui_Screen4);
    lv_obj_set_size(password_panel, 360, 144);
    lv_obj_align(password_panel, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(password_panel, lv_color_hex(0x101A22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(password_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(password_panel, lv_color_hex(0x4FA8FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(password_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(password_panel, 18, LV_PART_MAIN);
    lv_obj_clear_flag(password_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *password_title = lv_label_create(password_panel);
    lv_label_set_text(password_title, "Mit WLAN verbinden");
    lv_obj_set_style_text_font(password_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(password_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(password_title, LV_ALIGN_TOP_MID, 0, 7);

    password_network_label = lv_label_create(password_panel);
    lv_label_set_text(password_network_label, "Netzwerk");
    lv_label_set_long_mode(password_network_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(password_network_label, 320);
    lv_obj_set_style_text_font(password_network_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_network_label, lv_color_hex(0x78BDFF), LV_PART_MAIN);
    lv_obj_align(password_network_label, LV_ALIGN_TOP_MID, 0, 33);

    password_input = lv_textarea_create(password_panel);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_placeholder_text(password_input, "WLAN-Passwort");
    lv_textarea_set_max_length(password_input, 63);
    lv_obj_set_size(password_input, 318, 36);
    lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_text_font(password_input, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_input, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(password_input, lv_color_hex(0x202B33), LV_PART_MAIN);
    lv_obj_set_style_border_color(password_input, lv_color_hex(0x4FA8FF), LV_PART_MAIN);
    lv_obj_set_style_radius(password_input, 10, LV_PART_MAIN);

    password_error_label = lv_label_create(password_panel);
    lv_label_set_text(password_error_label, "");
    lv_obj_set_style_text_font(password_error_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_error_label, lv_color_hex(0xFF8A80), LV_PART_MAIN);
    lv_obj_align(password_error_label, LV_ALIGN_TOP_MID, 0, 93);

    cancel_button = lv_btn_create(password_panel);
    lv_obj_set_size(cancel_button, 112, 32);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_LEFT, 12, -5);
    set_button_style(cancel_button, 0x26343D);
    lv_obj_add_event_cb(cancel_button, cancel_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel_button);
    lv_label_set_text(cancel_label, "Abbrechen");
    lv_obj_center(cancel_label);

    password_ok_button = lv_btn_create(password_panel);
    lv_obj_set_size(password_ok_button, 112, 32);
    lv_obj_align(password_ok_button, LV_ALIGN_BOTTOM_RIGHT, -12, -5);
    set_button_style(password_ok_button, 0x0B4F8A);
    lv_obj_add_event_cb(password_ok_button, submit_password, LV_EVENT_CLICKED, NULL);
    lv_obj_t *password_ok_label = lv_label_create(password_ok_button);
    lv_label_set_text(password_ok_label, "OK");
    lv_obj_center(password_ok_label);

    keyboard = lv_keyboard_create(ui_Screen4);
    lv_obj_set_size(keyboard, 360, 190);
    lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 182);
    lv_obj_set_style_text_font(keyboard, ui_german_font_14(), LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    keyboard_visible = false;
    previous_wifi_connected = false;
    selected_network_secured = false;
    selected_ssid[0] = '\0';
    last_scan_generation = UINT32_MAX;
    last_ui_update_ms = 0;
    last_auto_scan_ms = 0;
    lv_obj_add_event_cb(ui_Screen4, screen_event, LV_EVENT_ALL, NULL);
}

void ui_Screen4_screen_destroy(void)
{
    if(ui_Screen4) lv_obj_del(ui_Screen4);
    ui_Screen4 = NULL;
    wifi_list = NULL;
    password_panel = NULL;
    password_network_label = NULL;
    password_input = NULL;
    password_error_label = NULL;
    keyboard = NULL;
    cancel_button = NULL;
    password_ok_button = NULL;
    connect_button = NULL;
    list_heading = NULL;
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        wifi_arcs[index] = NULL;
    }
    keyboard_visible = false;
    selected_ssid[0] = '\0';
}

void ui_screen4_show(void)
{
    _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen4_screen_init);
}

void ui_screen4_update(void)
{
    if(ui_Screen4 == NULL || lv_scr_act() != ui_Screen4) return;

    const uint32_t now = lv_tick_get();
    if(now - last_ui_update_ms < 250) return;
    last_ui_update_ms = now;

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        const bool active = snapshot.connected &&
                            snapshot.signal_percent >= (int)((index + 1) * 25);
        lv_obj_set_style_arc_color(wifi_arcs[index],
                                   lv_color_hex(active ? 0x4FA8FF : 0x394550),
                                   LV_PART_MAIN);
    }
    if(snapshot.scan_generation != last_scan_generation) {
        last_scan_generation = snapshot.scan_generation;
        refresh_network_list(snapshot.scanning);
        wifi_ota_networks_displayed();
    }
    if(snapshot.connected != previous_wifi_connected) {
        refresh_network_list(snapshot.scanning);
    }
    if(snapshot.connected && !previous_wifi_connected) {
        lv_textarea_set_text(password_input, "");
    }
    previous_wifi_connected = snapshot.connected;

    if(!keyboard_visible && !snapshot.scanning && !snapshot.updating &&
       now - last_auto_scan_ms >= WIFI_SCAN_INTERVAL_MS) {
        last_auto_scan_ms = now;
        wifi_ota_start_scan();
    }

    if(snapshot.updating) {
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    } else {
        if(selected_ssid[0] != '\0') {
            lv_obj_clear_state(connect_button, LV_STATE_DISABLED);
        }
    }
}
