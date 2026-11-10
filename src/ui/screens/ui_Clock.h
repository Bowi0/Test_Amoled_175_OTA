#ifndef UI_CLOCK_H
#define UI_CLOCK_H

#include "lvgl.h"

extern lv_obj_t *ui_Clock;
void ui_clock_init(void);
void ui_clock_destroy(void);
void ui_clock_update(void);

#endif
