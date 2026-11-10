#include "device_settings.h"
#include "lcd_bsp.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences settings;
static bool ready;
static bool wifi_enabled;
static bool bluetooth_enabled;
static uint8_t brightness = 82;
static device_theme_t theme = DEVICE_THEME_BLUE;

extern "C" bool device_settings_init(void)
{
    ready = settings.begin("settings", false);
    if(!ready) {
        Serial.println("Settings: failed to open storage");
        return false;
    }
    wifi_enabled = settings.getBool("wifi", false);
    bluetooth_enabled = settings.getBool("bluetooth", false);
    brightness = settings.getUChar("brightness", 82);
    if(brightness > DEVICE_BRIGHTNESS_MAX) {
        Serial.println("Settings: invalid stored brightness");
        brightness = 82;
    } else if(brightness < DEVICE_BRIGHTNESS_MIN) {
        brightness = DEVICE_BRIGHTNESS_MIN;
    }
    const uint8_t stored_theme = settings.getUChar("theme", DEVICE_THEME_BLUE);
    if(stored_theme < DEVICE_THEME_COUNT) {
        theme = static_cast<device_theme_t>(stored_theme);
    } else {
        Serial.println("Settings: invalid stored theme");
        theme = DEVICE_THEME_BLUE;
    }
    return true;
}

extern "C" bool device_wifi_enabled(void) { return wifi_enabled; }
extern "C" bool device_bluetooth_enabled(void) { return bluetooth_enabled; }
extern "C" uint8_t device_brightness(void) { return brightness; }
extern "C" device_theme_t device_theme(void) { return theme; }

extern "C" bool device_set_theme(device_theme_t value)
{
    if(value < DEVICE_THEME_BLUE || value >= DEVICE_THEME_COUNT) {
        Serial.println("Settings: invalid theme");
        return false;
    }
    if(!ready || settings.putUChar("theme", static_cast<uint8_t>(value)) == 0) {
        Serial.println("Settings: could not save theme");
        return false;
    }
    theme = value;
    return true;
}

extern "C" bool device_set_wifi_enabled(bool enabled)
{
    if(!ready || settings.putBool("wifi", enabled) == 0) {
        Serial.println("Settings: could not save Wi-Fi state");
        return false;
    }
    wifi_enabled = enabled;
    return true;
}

extern "C" bool device_set_bluetooth_enabled(bool enabled)
{
    if(!ready || settings.putBool("bluetooth", enabled) == 0) {
        Serial.println("Settings: could not save Bluetooth state");
        return false;
    }
    bluetooth_enabled = enabled;
    return true;
}

extern "C" bool device_set_brightness(uint8_t percent, bool save)
{
    if(percent < DEVICE_BRIGHTNESS_MIN || percent > DEVICE_BRIGHTNESS_MAX) {
        Serial.println("Settings: invalid brightness");
        return false;
    }
    if(save && (!ready || settings.putUChar("brightness", percent) == 0)) {
        Serial.println("Settings: could not save brightness");
        return false;
    }
    brightness = percent;
    lcd_set_brightness((uint8_t)((percent * 255U) / 100U));
    return true;
}
