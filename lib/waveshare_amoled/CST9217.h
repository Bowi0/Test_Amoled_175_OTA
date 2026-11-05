#ifndef CST9217_H
#define CST9217_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t Touch_Init(void);
uint8_t getTouch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif
