#include "../ui.h"
#include "../ui_german_font.h"
#include "../ui_theme.h"
#include "wifi_ota.h"
#include "device_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

lv_obj_t *ui_Screen4 = NULL;

static lv_obj_t *wifi_list;
static lv_obj_t *password_panel;
static lv_obj_t *password_input;
static lv_obj_t *password_error_label;
static lv_obj_t *keyboard;
static lv_obj_t *key_preview;
static lv_obj_t *key_preview_label;
static lv_obj_t *connection_overlay;
static lv_obj_t *connection_panel;
static lv_obj_t *connection_spinner;
static lv_obj_t *connection_message;
static lv_obj_t *connection_done_button;
static lv_obj_t *connect_button;
static lv_obj_t *connect_label;
static lv_obj_t *back_button;
static lv_obj_t *cancel_button;
static lv_obj_t *password_ok_button;
static lv_obj_t *list_heading;
static lv_obj_t *screen_title;
static lv_obj_t *wifi_arcs[4];
static uint32_t last_scan_generation = UINT32_MAX;
static uint32_t last_ui_update_ms;
static uint32_t last_auto_scan_ms;
static bool scan_on_connect_finished;
static uint32_t connection_success_ms;
#define WIFI_SCAN_INTERVAL_MS 15000
#define CONNECTION_SUCCESS_DISPLAY_MS 1000
#define KEY_PREVIEW_SIZE 120
#define KEY_PREVIEW_OFFSET 40
static bool keyboard_visible;
static bool previous_wifi_connected;
static bool screen_start_pending;
static bool screen_initial_refresh_started;
static bool selected_network_secured;
static bool connection_dialog_active;
static bool connection_success_visible;
static bool saved_network_attempt;
static char selected_ssid[33];

static void set_button_style(lv_obj_t *button, uint32_t color)
{
    ui_theme_style_button(button, color);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 15, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_18, LV_PART_MAIN);
}

static void create_wifi_icon(void)
{
    static const lv_coord_t arc_sizes[] = {10, 17, 24, 32};
    const lv_coord_t icon_center_x = 374;
    const lv_coord_t icon_bottom_y = 94;

    for(size_t index = 0; index < sizeof(arc_sizes) / sizeof(arc_sizes[0]); index++) {
        const lv_coord_t size = arc_sizes[index];
        wifi_arcs[index] = ui_theme_create_wifi_arc(ui_Screen4, size,
                                                     icon_center_x - size / 2,
                                                     icon_bottom_y - size);
    }
}

static void set_password_view(bool visible)
{
    keyboard_visible = visible;
    if(visible) {
        lv_obj_add_flag(screen_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list_heading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
            lv_obj_add_flag(wifi_arcs[index], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, password_input);
    } else {
        lv_obj_clear_flag(screen_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(list_heading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(back_button, LV_OBJ_FLAG_HIDDEN);
        for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
            lv_obj_clear_flag(wifi_arcs[index], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_connection_result(const char *message)
{
    lv_obj_add_flag(connection_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(connection_message, message);
    lv_obj_clear_flag(connection_done_button, LV_OBJ_FLAG_HIDDEN);
}

static void update_key_preview(void)
{
    const uint16_t button_id = lv_btnmatrix_get_selected_btn(keyboard);
    if(button_id == LV_BTNMATRIX_BTN_NONE) {
        lv_obj_add_flag(key_preview, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const char *text = lv_btnmatrix_get_btn_text(keyboard, button_id);
    if(text == NULL || text[0] == '\0' || text[1] != '\0' ||
       !isprint((unsigned char)text[0]) || text[0] == ' ') {
        lv_obj_add_flag(key_preview, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(key_preview_label, text);
    lv_point_t point;
    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) {
        lv_obj_add_flag(key_preview, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_indev_get_point(indev, &point);

    const lv_coord_t preview_size = KEY_PREVIEW_SIZE;
    const lv_coord_t screen_width = lv_obj_get_width(ui_Screen4);
    const lv_coord_t screen_height = lv_obj_get_height(ui_Screen4);
    lv_coord_t x = point.x - preview_size / 2;
    lv_coord_t y = point.y - preview_size - KEY_PREVIEW_OFFSET;
    if(x < 0) x = 0;
    if(x + preview_size > screen_width) x = screen_width - preview_size;
    if(y + preview_size > screen_height) y = screen_height - preview_size;

    const float circle_center_x = screen_width / 2.0f;
    const float circle_center_y = screen_height / 2.0f;
    const float safe_radius = LV_MIN(screen_width, screen_height) / 2.0f -
                              preview_size / 2.0f - 2.0f;
    float preview_center_y = y + preview_size / 2.0f;
    float delta_y = preview_center_y - circle_center_y;
    if(fabsf(delta_y) > safe_radius) {
        delta_y = delta_y < 0 ? -safe_radius : safe_radius;
        preview_center_y = circle_center_y + delta_y;
        y = (lv_coord_t)(preview_center_y - preview_size / 2.0f);
    }

    const float max_delta_x = sqrtf(safe_radius * safe_radius - delta_y * delta_y);
    float preview_center_x = x + preview_size / 2.0f;
    const float min_center_x = circle_center_x - max_delta_x;
    const float max_center_x = circle_center_x + max_delta_x;
    if(preview_center_x < min_center_x) preview_center_x = min_center_x;
    if(preview_center_x > max_center_x) preview_center_x = max_center_x;
    x = (lv_coord_t)(preview_center_x - preview_size / 2.0f);

    lv_obj_set_pos(key_preview, x, y);
    lv_obj_clear_flag(key_preview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(key_preview);
}

static void hide_key_preview(void)
{
    lv_obj_add_flag(key_preview, LV_OBJ_FLAG_HIDDEN);
}

static void connection_done_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    connection_dialog_active = false;
    lv_obj_add_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void set_network_row_style(lv_obj_t *row, bool selected)
{
    lv_obj_set_style_bg_color(row, lv_color_hex(0x17232C), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, selected ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row,
                                  lv_color_hex(selected
                                                   ? ui_theme_colors(device_theme())->accent : 0x263540),
                                  LV_PART_MAIN);
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
                                                   ? ui_theme_colors(device_theme())->accent : 0x394550),
                                  LV_PART_MAIN);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void update_connect_button(const wifi_ota_snapshot_t *snapshot)
{
    const bool selected_connected = snapshot->connected &&
                                    strcmp(snapshot->ssid, selected_ssid) == 0;
    lv_label_set_text(connect_label, selected_connected ? "Trennen" : "Verbinden");
    if(selected_ssid[0] == '\0' || snapshot->updating) {
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(connect_button, LV_STATE_DISABLED);
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
        set_network_row_style(row, selected);
        lv_obj_set_style_text_color(lv_obj_get_child(row, 0),
                                    lv_color_hex(connected ? ui_theme_colors(device_theme())->light : 0xFFFFFF),
                                    LV_PART_MAIN);
    }
    update_connect_button(&snapshot);
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
        const bool scan_failed =
            strstr(snapshot.wifi_status, "fehlgeschlagen") != NULL ||
            strstr(snapshot.wifi_status, "abgebrochen") != NULL ||
            strstr(snapshot.wifi_status, "nicht gestartet") != NULL;
        lv_label_set_text(empty_label,
                          !device_wifi_enabled() ? "WLAN ist ausgeschaltet"
                          : scanning ? "Suche nach Netzwerken..."
                                   : scan_failed
                                         ? "Suche fehlgeschlagen — erneut versuchen"
                                         : "Keine Netzwerke gefunden");
        lv_obj_set_style_text_font(empty_label, ui_german_font_14(), LV_PART_MAIN);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
        lv_obj_center(empty_label);
        selected_ssid[0] = '\0';
        selected_network_secured = false;
        update_connect_button(&snapshot);
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
        set_network_row_style(row, selected);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
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
                                    lv_color_hex(connected ? ui_theme_colors(device_theme())->light : 0xFFFFFF),
                                    LV_PART_MAIN);
        lv_obj_clear_flag(name_label, LV_OBJ_FLAG_CLICKABLE);

        draw_signal_bars(row, signal_percent);
    }

    if(!selection_found) {
        selected_ssid[0] = '\0';
        selected_network_secured = false;
    }
    update_connect_button(&snapshot);
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
        lv_obj_clear_flag(password_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if(!wifi_ota_connect(selected_ssid, password)) {
        lv_label_set_text(password_error_label,
                          "Verbindung konnte nicht gestartet werden");
        lv_obj_clear_flag(password_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    saved_network_attempt = false;
    lv_textarea_set_text(password_input, "");
    lv_label_set_text(password_error_label, "");
    set_password_view(false);
    connection_dialog_active = true;
    connection_success_visible = false;
    lv_label_set_text_fmt(connection_message, "Verbinde mit %.32s...",
                          selected_ssid);
    lv_obj_clear_flag(connection_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(connection_done_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(connection_overlay);
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
    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_btnmatrix_set_btn_ctrl_all(
            keyboard, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    } else if(code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        update_key_preview();
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        hide_key_preview();
    } else if(code == LV_EVENT_READY) {
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

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    if(snapshot.connected && strcmp(snapshot.ssid, selected_ssid) == 0) {
        if(!wifi_ota_disconnect()) {
            wifi_ota_get_snapshot(&snapshot);
            show_connection_result(snapshot.wifi_status);
            lv_obj_clear_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(connection_overlay);
        }
        return;
    }

    if(wifi_ota_has_saved_network(selected_ssid)) {
        if(!wifi_ota_connect_saved(selected_ssid)) {
            show_connection_result("Verbindung konnte nicht gestartet werden");
            lv_obj_clear_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(connection_overlay);
            return;
        }
        saved_network_attempt = true;
        connection_dialog_active = true;
        connection_success_visible = false;
        lv_label_set_text_fmt(connection_message, "Verbinde mit %.32s...", selected_ssid);
        lv_obj_clear_flag(connection_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connection_done_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(connection_overlay);
        return;
    }

    lv_textarea_set_text(password_input, "");
    lv_label_set_text(password_error_label, "");
    lv_obj_add_flag(password_error_label, LV_OBJ_FLAG_HIDDEN);
    set_password_view(true);
}

static void back_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_scr_load(ui_Screen6);
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen4) return;

    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        const ui_theme_colors_t *colors = ui_theme_colors(device_theme());
        set_button_style(connect_button, colors->button);
        set_button_style(back_button, colors->button);
        set_button_style(cancel_button, colors->button);
        set_button_style(password_ok_button, colors->button);
        set_button_style(connection_done_button, colors->button);
        lv_obj_set_style_border_color(password_input, lv_color_hex(colors->accent), LV_PART_MAIN);
        lv_obj_set_style_border_color(password_input, lv_color_hex(colors->accent), LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(key_preview, lv_color_hex(colors->accent), LV_PART_MAIN);
        lv_obj_set_style_border_color(connection_panel, lv_color_hex(colors->accent), LV_PART_MAIN);
        lv_obj_set_style_arc_color(connection_spinner, lv_color_hex(colors->accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(colors->button), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(keyboard, lv_color_darken(lv_color_hex(colors->button), LV_OPA_20),
                                  LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(keyboard, lv_color_white(), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(wifi_list, lv_color_hex(colors->accent), LV_PART_SCROLLBAR);
        wifi_ota_snapshot_t snapshot = {0};
        wifi_ota_get_snapshot(&snapshot);
        refresh_network_list(snapshot.scanning);
        screen_start_pending = true;
        screen_initial_refresh_started = false;
        return;
    }

    if(lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOADED) {
        screen_start_pending = false;
        return;
    }

}

void ui_Screen4_screen_init(void)
{
    ui_Screen4 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_Screen4, LV_OPA_COVER, LV_PART_MAIN);

    screen_title = lv_label_create(ui_Screen4);
    lv_label_set_text(screen_title, "WLAN");
    lv_obj_set_style_text_font(screen_title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(screen_title, LV_ALIGN_TOP_MID, 0, 26);
    create_wifi_icon();

    list_heading = lv_label_create(ui_Screen4);
    lv_label_set_text(list_heading, "VERFÜGBARE NETZWERKE");
    lv_obj_set_style_text_font(list_heading, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(list_heading, lv_color_hex(0x8294A1), LV_PART_MAIN);
    lv_obj_align(list_heading, LV_ALIGN_TOP_MID, 0, 91);

    wifi_list = lv_obj_create(ui_Screen4);
    lv_obj_set_size(wifi_list, 324, 206);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 113);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
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

    back_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(back_button, 140, 52);
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, -76, 334);
    lv_obj_set_ext_click_area(back_button, 4);
    set_button_style(back_button, 0x26343D);
    lv_obj_add_event_cb(back_button, back_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_button);
    lv_obj_set_style_text_font(back_label, ui_german_font_14(), LV_PART_MAIN);
    lv_label_set_text(back_label, "Zurück");
    lv_obj_center(back_label);

    connect_button = lv_btn_create(ui_Screen4);
    lv_obj_set_size(connect_button, 140, 52);
    lv_obj_align(connect_button, LV_ALIGN_TOP_MID, 76, 334);
    set_button_style(connect_button, 0x064A83);
    lv_obj_add_event_cb(connect_button, connect_button_event, LV_EVENT_CLICKED, NULL);
    connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Verbinden");
    lv_obj_center(connect_label);
    lv_obj_add_state(connect_button, LV_STATE_DISABLED);

    password_panel = lv_obj_create(ui_Screen4);
    lv_obj_remove_style_all(password_panel);
    lv_obj_set_size(password_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(password_panel, 0, 0);
    lv_obj_clear_flag(password_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(password_panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *password_title = lv_label_create(password_panel);
    lv_label_set_text(password_title, "WLAN Passwort:");
    lv_obj_set_style_text_font(password_title, &lv_font_montserrat_18,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(password_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(password_title, LV_ALIGN_TOP_MID, 0, 40);

    password_input = lv_textarea_create(password_panel);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_placeholder_text(password_input, "Passwort eingeben");
    lv_textarea_set_max_length(password_input, 63);
    lv_obj_set_size(password_input, 320, 46);
    lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_text_font(password_input, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(password_input, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(password_input, lv_color_hex(0x202B33), LV_PART_MAIN);
    lv_obj_set_style_border_color(password_input, lv_color_hex(0x4FA8FF), LV_PART_MAIN);
    lv_obj_set_style_radius(password_input, 10, LV_PART_MAIN);

    password_error_label = lv_label_create(password_panel);
    lv_label_set_text(password_error_label, "");
    lv_obj_set_style_text_font(password_error_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_error_label, lv_color_hex(0xFF8A80), LV_PART_MAIN);
    lv_obj_align(password_error_label, LV_ALIGN_TOP_MID, 0, 338);
    lv_obj_add_flag(password_error_label, LV_OBJ_FLAG_HIDDEN);

    cancel_button = lv_btn_create(password_panel);
    lv_obj_set_size(cancel_button, 120, 42);
    lv_obj_set_pos(cancel_button, 101, 374);
    set_button_style(cancel_button, 0x26343D);
    lv_obj_add_event_cb(cancel_button, cancel_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel_button);
    lv_label_set_text(cancel_label, "Abbrechen");
    lv_obj_center(cancel_label);

    password_ok_button = lv_btn_create(password_panel);
    lv_obj_set_size(password_ok_button, 120, 42);
    lv_obj_set_pos(password_ok_button, 245, 374);
    set_button_style(password_ok_button, 0x0B4F8A);
    lv_obj_add_event_cb(password_ok_button, submit_password, LV_EVENT_CLICKED, NULL);
    lv_obj_t *password_ok_label = lv_label_create(password_ok_button);
    lv_label_set_text(password_ok_label, "OK");
    lv_obj_center(password_ok_label);

    keyboard = lv_keyboard_create(ui_Screen4);
    lv_obj_set_size(keyboard, 412, 200);
    lv_obj_align(keyboard, LV_ALIGN_CENTER, 0, 0);
    lv_keyboard_set_popovers(keyboard, false);
    lv_btnmatrix_set_btn_ctrl_all(
        keyboard, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_set_style_text_font(keyboard, ui_german_font_14(), LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(password_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    key_preview = lv_obj_create(ui_Screen4);
    lv_obj_remove_style_all(key_preview);
    lv_obj_set_size(key_preview, KEY_PREVIEW_SIZE, KEY_PREVIEW_SIZE);
    lv_obj_set_style_bg_color(key_preview, lv_color_hex(0x162B3A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(key_preview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(key_preview, lv_color_hex(0x73C3FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(key_preview, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(key_preview, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(key_preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(key_preview, LV_OBJ_FLAG_SCROLLABLE);
    key_preview_label = lv_label_create(key_preview);
    lv_obj_set_style_text_font(key_preview_label, &lv_font_montserrat_40,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(key_preview_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(key_preview_label);
    lv_obj_add_flag(key_preview, LV_OBJ_FLAG_HIDDEN);

    connection_overlay = lv_obj_create(ui_Screen4);
    lv_obj_remove_style_all(connection_overlay);
    lv_obj_set_size(connection_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(connection_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(connection_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(connection_overlay, LV_OBJ_FLAG_SCROLLABLE);
    connection_panel = lv_obj_create(connection_overlay);
    lv_obj_set_size(connection_panel, 320, 190);
    lv_obj_center(connection_panel);
    lv_obj_set_style_bg_color(connection_panel, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(connection_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(connection_panel, lv_color_hex(0x4FA8FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(connection_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(connection_panel, 18, LV_PART_MAIN);
    lv_obj_clear_flag(connection_panel, LV_OBJ_FLAG_SCROLLABLE);

    connection_spinner = lv_spinner_create(connection_panel, 1000, 60);
    ui_theme_style_spinner(connection_spinner);
    lv_obj_set_size(connection_spinner, 48, 48);
    lv_obj_align(connection_spinner, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_arc_color(connection_spinner, lv_color_hex(0x4FA8FF),
                               LV_PART_INDICATOR);
    connection_message = lv_label_create(connection_panel);
    lv_label_set_text(connection_message, "Verbindung wird hergestellt...");
    lv_obj_set_width(connection_message, 280);
    lv_label_set_long_mode(connection_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(connection_message, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(connection_message, ui_german_font_14(),
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(connection_message, lv_color_white(),
                                 LV_PART_MAIN);
    lv_obj_align(connection_message, LV_ALIGN_CENTER, 0, 17);
    connection_done_button = lv_btn_create(connection_panel);
    lv_obj_set_size(connection_done_button, 112, 36);
    lv_obj_align(connection_done_button, LV_ALIGN_BOTTOM_MID, 0, -12);
    set_button_style(connection_done_button, 0x0B4F8A);
    lv_obj_add_event_cb(connection_done_button, connection_done_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *connection_done_label = lv_label_create(connection_done_button);
    lv_label_set_text(connection_done_label, "OK");
    lv_obj_center(connection_done_label);
    lv_obj_add_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);

    keyboard_visible = false;
    previous_wifi_connected = false;
    selected_network_secured = false;
    connection_dialog_active = false;
    saved_network_attempt = false;
    connection_success_visible = false;
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
    password_input = NULL;
    password_error_label = NULL;
    keyboard = NULL;
    key_preview = NULL;
    key_preview_label = NULL;
    connection_overlay = NULL;
    connection_panel = NULL;
    connection_spinner = NULL;
    connection_message = NULL;
    connection_done_button = NULL;
    cancel_button = NULL;
    password_ok_button = NULL;
    connect_button = NULL;
    connect_label = NULL;
    back_button = NULL;
    list_heading = NULL;
    screen_title = NULL;
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        wifi_arcs[index] = NULL;
    }
    keyboard_visible = false;
    connection_dialog_active = false;
    connection_success_visible = false;
    screen_start_pending = false;
    selected_ssid[0] = '\0';
}

void ui_screen4_show(void)
{
    _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      &ui_Screen4_screen_init);
}

bool ui_screen4_can_open_quick_settings(void)
{
    return !keyboard_visible && lv_obj_has_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_screen4_update(void)
{
    if(ui_Screen4 == NULL || lv_scr_act() != ui_Screen4) return;

    if(connection_success_visible &&
       lv_tick_elaps(connection_success_ms) >= CONNECTION_SUCCESS_DISPLAY_MS) {
        lv_obj_add_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
        connection_success_visible = false;
    }

    if(screen_start_pending) {
        lv_disp_t *display = lv_obj_get_disp(ui_Screen4);
        if(!screen_initial_refresh_started) {
            lv_refr_now(display);
            screen_initial_refresh_started = true;
            return;
        }
        if(lv_disp_get_draw_buf(display)->flushing) return;
        screen_start_pending = false;
        wifi_ota_screen_enter();
        wifi_ota_snapshot_t snapshot = {0};
        wifi_ota_get_snapshot(&snapshot);
        scan_on_connect_finished = device_wifi_enabled() &&
                                   !snapshot.scanning &&
                                   (snapshot.connecting || snapshot.updating);
        if(device_wifi_enabled() && !snapshot.scanning && !scan_on_connect_finished)
            wifi_ota_start_scan();
        wifi_ota_get_snapshot(&snapshot);
        refresh_network_list(snapshot.scanning || scan_on_connect_finished);
        last_auto_scan_ms = lv_tick_get();
    }

    const uint32_t now = lv_tick_get();
    if(now - last_ui_update_ms < 250) return;
    last_ui_update_ms = now;

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    if(scan_on_connect_finished && device_wifi_enabled() &&
       !snapshot.connecting && !snapshot.updating && !snapshot.scanning) {
        scan_on_connect_finished = false;
        wifi_ota_start_scan();
        wifi_ota_get_snapshot(&snapshot);
        last_auto_scan_ms = now;
        refresh_network_list(snapshot.scanning);
    }
    for(size_t index = 0; index < sizeof(wifi_arcs) / sizeof(wifi_arcs[0]); index++) {
        const bool active = snapshot.connected &&
                            snapshot.signal_percent >= (int)((index + 1) * 25);
        lv_obj_set_style_arc_color(wifi_arcs[index],
                                   lv_color_hex(active ? ui_theme_colors(device_theme())->accent : 0x394550),
                                   LV_PART_MAIN);
    }
    if(snapshot.scan_generation != last_scan_generation) {
        last_scan_generation = snapshot.scan_generation;
        last_auto_scan_ms = now;
        refresh_network_list(snapshot.scanning);
    }
    if(snapshot.connected != previous_wifi_connected) {
        refresh_network_list(snapshot.scanning);
    }
    if(connection_dialog_active && !snapshot.connecting) {
        if(snapshot.connected) {
            char message[96];
            const bool save_failed = snapshot.credential_save_failed;
            if(save_failed)
                snprintf(message, sizeof(message), "Verbunden, aber WLAN-Daten nicht gespeichert");
            else
                snprintf(message, sizeof(message), "Verbunden mit %s\nIP: %s",
                         snapshot.ssid, snapshot.ip);
            show_connection_result(message);
            if(!save_failed) {
                lv_obj_add_flag(connection_done_button, LV_OBJ_FLAG_HIDDEN);
                connection_success_ms = lv_tick_get();
                connection_success_visible = true;
            }
        } else {
            if(saved_network_attempt) {
                lv_obj_add_flag(connection_overlay, LV_OBJ_FLAG_HIDDEN);
                lv_textarea_set_text(password_input, "");
                lv_label_set_text(password_error_label, "Passwort prüfen und erneut eingeben");
                lv_obj_clear_flag(password_error_label, LV_OBJ_FLAG_HIDDEN);
                set_password_view(true);
            } else {
                show_connection_result(snapshot.wifi_status);
            }
        }
        saved_network_attempt = false;
        connection_dialog_active = false;
    }
    if(snapshot.connected && !previous_wifi_connected) {
        lv_textarea_set_text(password_input, "");
    }
    previous_wifi_connected = snapshot.connected;

    if(device_wifi_enabled() && !keyboard_visible && !snapshot.scanning && !snapshot.connecting &&
       !snapshot.updating &&
       now - last_auto_scan_ms >= WIFI_SCAN_INTERVAL_MS) {
        last_auto_scan_ms = now;
        wifi_ota_start_scan();
    }

    update_connect_button(&snapshot);
}
