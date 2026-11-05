#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

#define EXAMPLE_LCD_H_RES              466
#define EXAMPLE_LCD_V_RES              466

#define LCD_BIT_PER_PIXEL              16

#define EXAMPLE_PIN_NUM_LCD_CS            12
#define EXAMPLE_PIN_NUM_LCD_PCLK          38
#define EXAMPLE_PIN_NUM_LCD_DATA0         4
#define EXAMPLE_PIN_NUM_LCD_DATA1         5
#define EXAMPLE_PIN_NUM_LCD_DATA2         6
#define EXAMPLE_PIN_NUM_LCD_DATA3         7
#define EXAMPLE_PIN_NUM_LCD_RST           39
#define EXAMPLE_PIN_NUM_BK_LIGHT          (-1)

#define EXAMPLE_LVGL_BUF_HEIGHT        (EXAMPLE_LCD_V_RES / 4)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2                          //Timer time
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500                        //LVGL Indicates the maximum time for a task to run
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1                          //LVGL Minimum time to run a task
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)                 //LVGL runs the task stack
#define EXAMPLE_LVGL_TASK_PRIORITY     2                          //LVGL Running task priority

#define EXAMPLE_PIN_NUM_TOUCH_SCL 14
#define EXAMPLE_PIN_NUM_TOUCH_SDA 15
#define EXAMPLE_PIN_NUM_TOUCH_INT 11
#define EXAMPLE_PIN_NUM_TOUCH_RST 40

#endif