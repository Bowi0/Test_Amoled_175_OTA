#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_BRIGHTNESS_MIN 10
#define DEVICE_BRIGHTNESS_MAX 100

#ifdef __cplusplus
extern "C" {
#endif

bool device_settings_init(void);
bool device_wifi_enabled(void);
bool device_set_wifi_enabled(bool enabled);
bool device_bluetooth_enabled(void);
bool device_set_bluetooth_enabled(bool enabled);
uint8_t device_brightness(void);
bool device_set_brightness(uint8_t percent, bool save);
typedef enum {
    DEVICE_THEME_BLUE,
    DEVICE_THEME_BROWN,
    DEVICE_THEME_SILVER,
    DEVICE_THEME_GOLD,
    DEVICE_THEME_BRONZE,
    DEVICE_THEME_COUNT
} device_theme_t;
device_theme_t device_theme(void);
bool device_set_theme(device_theme_t theme);

#ifdef __cplusplus
}
#endif
#endif
