#ifndef UI_SCREEN6_H
#define UI_SCREEN6_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
extern lv_obj_t *ui_Screen6;
void ui_Screen6_screen_init(void);
void ui_Screen6_screen_destroy(void);
void ui_screen6_update(void);
void ui_settings_startup(void);
void ui_quick_settings_gesture(lv_event_t *event);
#ifdef __cplusplus
}
#endif
#endif
