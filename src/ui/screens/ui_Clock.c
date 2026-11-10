#include "../ui.h"
#include "../ui_german_font.h"
#include "../ui_theme.h"
#include "wifi_ota.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

lv_obj_t *ui_Clock;
static lv_obj_t *hands[3];
static lv_point_t points[3][2];
static lv_obj_t *date_label;
static lv_obj_t *status_label;
static uint32_t last_update;

static void set_hand(unsigned index, float value, float divisions, int length)
{
    const float angle = value * 6.28318530718f / divisions;
    lv_obj_invalidate(hands[index]);
    points[index][0].x = 233;
    points[index][0].y = 210;
    points[index][1].x = (lv_coord_t)(233 + lroundf(sinf(angle) * length));
    points[index][1].y = (lv_coord_t)(210 - lroundf(cosf(angle) * length));
    lv_line_set_points(hands[index], points[index], 2);
}

void ui_clock_update(void)
{
    if(ui_Clock == NULL || lv_scr_act() != ui_Clock) return;
    if(lv_tick_elaps(last_update) < 250) return;
    last_update = lv_tick_get();
    time_t now = time(NULL);
    struct tm local;
    if(!wifi_ota_time_valid() || localtime_r(&now, &local) == NULL) {
        for(unsigned i = 0; i < 3; i++) lv_obj_add_flag(hands[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(date_label, "--.--.----");
        lv_label_set_text(status_label, "Warte auf Uhrzeit");
        return;
    }

    lv_label_set_text(status_label, "");
    lv_label_set_text_fmt(date_label, "%02d.%02d.%04d",
                          local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
    set_hand(0, (local.tm_hour % 12) + local.tm_min / 60.0f, 12, 72);
    set_hand(1, local.tm_min + local.tm_sec / 60.0f, 60, 110);
    set_hand(2, local.tm_sec, 60, 122);
    for(unsigned i = 0; i < 3; i++) lv_obj_clear_flag(hands[i], LV_OBJ_FLAG_HIDDEN);
}

static void clock_event(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        last_update = lv_tick_get() - 250;
        ui_clock_update();
        return;
    }
    if(lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_get_act();
    if(indev == NULL) return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if(direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT) {
        lv_indev_wait_release(indev);
        lv_scr_load(ui_Switches);
    }
}

void ui_clock_init(void)
{
    ui_Clock = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Clock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Clock, lv_color_hex(UI_BACKGROUND_COLOR), LV_PART_MAIN);

    for(int i = 0; i < 12; i++) {
        const float angle = i * 6.28318530718f / 12;
        lv_obj_t *mark = lv_obj_create(ui_Clock);
        const int size = i % 3 == 0 ? 8 : 5;
        lv_obj_set_size(mark, size, size);
        lv_obj_set_pos(mark, 233 + (int)lroundf(sinf(angle) * 145) - size / 2,
                       210 - (int)lroundf(cosf(angle) * 145) - size / 2);
        lv_obj_set_style_bg_color(mark, lv_color_hex(0xD6DBDF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(mark, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    for(unsigned i = 0; i < 3; i++) {
        hands[i] = lv_line_create(ui_Clock);
        lv_obj_set_style_line_width(hands[i], i == 0 ? 6 : (i == 1 ? 4 : 2), LV_PART_MAIN);
        lv_obj_set_style_line_color(hands[i], lv_color_hex(i == 2 ? 0xD98A72 : 0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(hands[i], true, LV_PART_MAIN);
        lv_obj_add_flag(hands[i], LV_OBJ_FLAG_HIDDEN);
    }

    date_label = lv_label_create(ui_Clock);
    lv_label_set_text(date_label, "--.--.----");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(date_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 372);

    status_label = lv_label_create(ui_Clock);
    lv_label_set_text(status_label, "Warte auf Uhrzeit");
    lv_obj_set_style_text_font(status_label, ui_german_font_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x9BAAB5), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 407);
    lv_obj_add_event_cb(ui_Clock, clock_event, LV_EVENT_ALL, NULL);
}

void ui_clock_destroy(void)
{
    if(ui_Clock) lv_obj_del(ui_Clock);
    ui_Clock = NULL;
}
