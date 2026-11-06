#include "../ui.h"
#include "../ui_german_font.h"
#include "wifi_ota.h"
#include <stdio.h>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

lv_obj_t *ui_Screen5 = NULL;

static lv_obj_t *check_button;
static lv_obj_t *install_button;
static lv_obj_t *later_button;
static lv_obj_t *status_label;
static lv_obj_t *download_bar;
static lv_obj_t *firmware_label;
static uint32_t last_update_ms;

static void install_button_event(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    if(!wifi_ota_confirm_update(true)) {
        lv_label_set_text(status_label, "Das Update ist nicht mehr verfügbar.");
    } else {
        lv_label_set_text(status_label, "Update wird vorbereitet...");
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

    if(!wifi_ota_request_update_check()) {
        lv_label_set_text(status_label, "Bitte zuerst WLAN verbinden.");
    } else {
        lv_label_set_text(status_label, "Update-Prüfung wird gestartet...");
    }
}

static void screen_event(lv_event_t *event)
{
    if(lv_event_get_target(event) != ui_Screen5 ||
       lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) return;

    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen4_screen_init);
    } else if(direction == LV_DIR_LEFT) {
        lv_indev_wait_release(indev);
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                          &ui_Screen1_screen_init);
    }
}

void ui_Screen5_screen_init(void)
{
    ui_Screen5 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen5, lv_color_hex(0x070B10), LV_PART_MAIN);
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
    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 91);

    check_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(check_button, 278, 58);
    lv_obj_align(check_button, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_bg_color(check_button, lv_color_hex(0x064A83),
                              LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(check_button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_border_width(check_button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(check_button, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(check_button, check_button_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(check_button);
    lv_label_set_text(button_label, "Nach Updates suchen...");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(button_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(button_label);

    install_button = lv_btn_create(ui_Screen5);
    lv_obj_set_size(install_button, 142, 46);
    lv_obj_align(install_button, LV_ALIGN_CENTER, -76, 22);
    lv_obj_set_style_bg_color(install_button, lv_color_hex(0x064A83),
                              LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(install_button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_ANY);
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
    lv_obj_set_size(status_label, 300, 58);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xB8D7FF), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 55);

    download_bar = lv_bar_create(ui_Screen5);
    lv_obj_set_size(download_bar, 250, 10);
    lv_bar_set_range(download_bar, 0, 100);
    lv_bar_set_value(download_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(download_bar, lv_color_hex(0x24323B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(download_bar, lv_color_hex(0x45D6A0), LV_PART_INDICATOR);
    lv_obj_align(download_bar, LV_ALIGN_CENTER, 0, 100);

    firmware_label = lv_label_create(ui_Screen5);
    char firmware_text[48];
    snprintf(firmware_text, sizeof(firmware_text), "Firmware-Stand: %s", APP_VERSION);
    lv_label_set_text(firmware_label, firmware_text);
    lv_obj_set_style_text_font(firmware_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(firmware_label, lv_color_hex(0x788791), LV_PART_MAIN);
    lv_obj_align(firmware_label, LV_ALIGN_BOTTOM_MID, 0, -28);

    last_update_ms = 0;
    lv_obj_add_event_cb(ui_Screen5, screen_event, LV_EVENT_ALL, NULL);
}

void ui_Screen5_screen_destroy(void)
{
    if(ui_Screen5) lv_obj_del(ui_Screen5);
    ui_Screen5 = NULL;
    check_button = NULL;
    install_button = NULL;
    later_button = NULL;
    status_label = NULL;
    download_bar = NULL;
    firmware_label = NULL;
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

    if(snapshot.update_available) {
        lv_obj_add_flag(check_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(later_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -48);
    } else {
        lv_obj_clear_flag(check_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(install_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(later_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(download_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 55);
    }

    if(snapshot.updating) {
        lv_obj_add_state(check_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(check_button, LV_STATE_DISABLED);
    }
}
