#include "ui_german_font.h"

extern const lv_font_t ui_font_montserrat_de_14;
extern const lv_font_t ui_font_montserrat_de_18;
extern const lv_font_t ui_font_montserrat_de_20;

const lv_font_t *ui_german_font_14(void)
{
    return &ui_font_montserrat_de_14;
}

const lv_font_t *ui_german_font_18(void)
{
    return &ui_font_montserrat_de_18;
}

const lv_font_t *ui_german_font_20(void)
{
    return &ui_font_montserrat_de_20;
}
