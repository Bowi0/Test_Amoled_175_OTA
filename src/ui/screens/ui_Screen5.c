#include "../ui.h"
#include "../ui_german_font.h"
#include "../ui_theme.h"
#include "device_settings.h"
#include "wifi_ota.h"
#include "uart_switch.h"
#include <stdio.h>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

lv_obj_t *ui_Screen5 = NULL;

static lv_obj_t *check_button;
static lv_obj_t *mainboard_button;
static lv_obj_t *install_button;
static lv_obj_t *later_button;
static lv_obj_t *back_button;
static lv_obj_t *status_label;
static lv_obj_t *download_bar;
static lv_obj_t *display_version_label;
static lv_obj_t *mainboard_version_label;
static lv_obj_t *loading_spinner;
static uint32_t last_update_ms;

static void install_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    if(!wifi_ota_confirm_update(true)) {
        lv_label_set_text(status_label, "Das Update ist nicht mehr verfügbar.");
    } else {
        lv_label_set_text(status_label, "Update wird vorbereitet...");
        lv_bar_set_value(download_bar, 0, LV_ANIM_OFF);
        lv_obj_clear_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void later_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    if(!wifi_ota_confirm_update(false)) {
        lv_label_set_text(status_label, "Das Update ist nicht mehr verfügbar.");
    } else {
        lv_label_set_text(status_label, "Update nicht installiert.");
    }
}

static void check_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    if(wifi_ota_request_update_check()) {
        lv_label_set_text(status_label, "Update-Prüfung wird gestartet...");
    }
}

static void mainboard_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED &&
       wifi_ota_request_mainboard_update_check())
        lv_label_set_text(status_label, "Mainboard-Update wird geprüft...");
}

static void back_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED)
        lv_scr_load(ui_Screen6);
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen5) return;
    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        const ui_theme_colors_t *colors = ui_theme_colors(device_theme());
        ui_theme_style_button(check_button, colors->button);
        ui_theme_style_button(mainboard_button, colors->button);
        ui_theme_style_button(install_button, colors->button);
        ui_theme_style_button(later_button, colors->button);
        ui_theme_style_button(back_button, colors->button);
        lv_obj_set_style_text_color(status_label, lv_color_hex(colors->light), LV_PART_MAIN);
        lv_obj_set_style_bg_color(download_bar, lv_color_hex(colors->accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(loading_spinner, lv_color_hex(colors->accent), LV_PART_INDICATOR);
        last_update_ms = lv_tick_get() - 250;
        ui_screen5_update();
        return;
    }
    if(lv_event_get_code(event) != LV_EVENT_GESTURE) return;

    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen6, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen6_screen_init);
    } else if(direction == LV_DIR_LEFT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen6, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen6_screen_init);
    }
}

void ui_Screen5_screen_init(void)
{
    ui_Screen5 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen5, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_Screen5, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_Screen5);
    lv_label_set_text(title, "Update");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *description = lv_label_create(ui_Screen5);
    lv_label_set_text(description, "Firmware auf neue Version prüfen");
    lv_obj_set_style_text_font(description, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(description, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 70);

    check_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(check_button, 278, 46);
    lv_obj_align(check_button, LV_ALIGN_TOP_MID, 0, 95);
    ui_theme_style_button(check_button, ui_theme_colors(device_theme())->button);
    lv_obj_set_style_border_width(check_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(check_button, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(check_button, check_button_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(check_button);
    lv_label_set_text(button_label, "Display-Update");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(button_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(button_label);

    display_version_label = lv_label_create(ui_Screen5);
    lv_label_set_text_fmt(display_version_label, "Stand: %s", APP_VERSION);
    lv_obj_set_style_text_font(display_version_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(display_version_label, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
    lv_obj_align(display_version_label, LV_ALIGN_TOP_MID, 0, 143);

    mainboard_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(mainboard_button, 278, 46);
    lv_obj_align(mainboard_button, LV_ALIGN_TOP_MID, 0, 164);
    ui_theme_style_button(mainboard_button, ui_theme_colors(device_theme())->button);
    lv_obj_set_style_border_width(mainboard_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(mainboard_button, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(mainboard_button, mainboard_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mainboard_label = lv_label_create(mainboard_button);
    lv_label_set_text(mainboard_label, "Mainboard-Update");
    lv_obj_set_style_text_font(mainboard_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(mainboard_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(mainboard_label);

    mainboard_version_label = lv_label_create(ui_Screen5);
    lv_label_set_text(mainboard_version_label, "Stand: unbekannt");
    lv_obj_set_style_text_font(mainboard_version_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(mainboard_version_label, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
    lv_obj_align(mainboard_version_label, LV_ALIGN_TOP_MID, 0, 212);

    install_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(install_button, 142, 46);
    lv_obj_align(install_button, LV_ALIGN_CENTER, -76, 22);
    ui_theme_style_button(install_button, ui_theme_colors(device_theme())->button);
    lv_obj_set_style_border_width(install_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(install_button, 15, LV_PART_MAIN);
    lv_obj_add_event_cb(install_button, install_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *install_label = lv_label_create(install_button);
    lv_label_set_text(install_label, "Installieren");
    lv_obj_set_style_text_font(install_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(install_label);
    lv_obj_add_flag(install_button, LV_OBJ_FLAG_HIDDEN);

    later_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(later_button, 142, 46);
    lv_obj_align(later_button, LV_ALIGN_CENTER, 76, 22);
    lv_obj_set_style_bg_color(later_button, lv_color_hex(0x26343D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(later_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(later_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(later_button, 15, LV_PART_MAIN);
    lv_obj_add_event_cb(later_button, later_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *later_label = lv_label_create(later_button);
    lv_label_set_text(later_label, "Nicht jetzt");
    lv_obj_set_style_text_font(later_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(later_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(later_label);
    lv_obj_add_flag(later_button, LV_OBJ_FLAG_HIDDEN);

    status_label = lv_label_create(ui_Screen5);
    lv_label_set_text(status_label, "Bereit");
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, 300);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xB8D7FF), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 238);

    loading_spinner = lv_spinner_create(ui_Screen5, 1000, 60);
    ui_theme_style_spinner(loading_spinner);
    lv_obj_set_size(loading_spinner, 42, 42);
    lv_obj_align_to(loading_spinner, status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);

    download_bar = lv_bar_create(ui_Screen5);
    lv_obj_set_size(download_bar, 250, 10);
    lv_bar_set_range(download_bar, 0, 100);
    lv_bar_set_value(download_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(download_bar, lv_color_hex(0x24323B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(download_bar, lv_color_hex(0x45D6A0), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(download_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(download_bar, lv_color_hex(0xB0BEC5), LV_PART_MAIN);
    lv_obj_set_style_border_opa(download_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_post(download_bar, true, LV_PART_MAIN);
    lv_obj_align(download_bar, LV_ALIGN_TOP_MID, 0, 330);
    lv_obj_add_flag(download_bar, LV_OBJ_FLAG_HIDDEN);

    back_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(back_button, 160, 52);
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, 0, 360);
    lv_obj_set_ext_click_area(back_button, 6);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x26343D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_button, 15, LV_PART_MAIN);
    lv_obj_add_event_cb(back_button, back_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, "Zurück");
    lv_obj_set_style_text_font(back_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(back_label);

    last_update_ms = 0;
    lv_obj_add_event_cb(ui_Screen5, screen_event, LV_EVENT_ALL, NULL);
}

void ui_Screen5_screen_destroy(void)
{
    if(ui_Screen5) lv_obj_del(ui_Screen5);
    ui_Screen5 = NULL;
    check_button = NULL;
    mainboard_button = NULL;
    install_button = NULL;
    later_button = NULL;
    back_button = NULL;
    status_label = NULL;
    download_bar = NULL;
    display_version_label = NULL;
    mainboard_version_label = NULL;
    loading_spinner = NULL;
}

void ui_screen5_update(void)
{
    if(ui_Screen5 == NULL || lv_scr_act() != ui_Screen5) return;

    const uint32_t now = lv_tick_get();
    if(now - last_update_ms < 250) return;
    last_update_ms = now;

    wifi_ota_snapshot_t snapshot = {0};
    wifi_ota_get_snapshot(&snapshot);
    lv_label_set_text(status_label, snapshot.update_status);
    lv_bar_set_value(download_bar, snapshot.progress, LV_ANIM_OFF);
    char version[16] = {0};
    if(uart_switch_version(version, sizeof(version)))
        lv_label_set_text_fmt(mainboard_version_label, "Stand: %s", version);
    else
        lv_label_set_text(mainboard_version_label, "Stand: unbekannt");
    if(snapshot.update_checking || snapshot.updating)
        lv_obj_clear_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);

    if(snapshot.update_available) {
        lv_obj_add_flag(check_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mainboard_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(display_version_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mainboard_version_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(later_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -48);
    } else {
        lv_obj_clear_flag(check_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mainboard_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(display_version_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mainboard_version_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(later_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 238);
    }
    if(snapshot.installing)
        lv_obj_clear_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(ui_Screen5);
    lv_obj_align_to(loading_spinner, status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    if(snapshot.update_checking || snapshot.updating) {
        lv_obj_update_layout(ui_Screen5);
        const lv_coord_t bar_y = LV_MAX(330, lv_obj_get_y(loading_spinner) + 46);
        lv_obj_align(download_bar, LV_ALIGN_TOP_MID, 0, bar_y);
    } else {
        lv_obj_align(download_bar, LV_ALIGN_TOP_MID, 0, 330);
    }

    if(snapshot.updating || snapshot.update_checking) {
        lv_obj_add_state(check_button, LV_STATE_DISABLED);
        lv_obj_add_state(mainboard_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(check_button, LV_STATE_DISABLED);
        lv_obj_clear_state(mainboard_button, LV_STATE_DISABLED);
    }
}
