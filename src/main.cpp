#include <Arduino.h>
#include "lvgl.h"
#include "ble_manager.h"
#include "lcd_bsp.h"   // lcd_lvgl_Init()
#include "ui/ui.h"     // ui_init()
#include "intro.h"
#include "wifi_ota.h"
#include "device_settings.h"
#include "uart_switch.h"
#include <nvs_flash.h>

// Touch driver (CST9217)
#include "CST9217.h"

static constexpr uint8_t BOOT_BUTTON_PIN = 0;
static constexpr uint32_t BOOT_BUTTON_DEBOUNCE_MS = 30;
static constexpr uint32_t BOOT_BUTTON_RESTART_MS = 1000;
static constexpr bool INTRO_ENABLED = true;

static void update_boot_button()
{
  static bool raw_pressed = false;
  static bool stable_pressed = false;
  static uint32_t raw_changed_ms = 0;
  static uint32_t pressed_ms = 0;

  const uint32_t now = millis();
  const bool pressed = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if(pressed != raw_pressed) {
    raw_pressed = pressed;
    raw_changed_ms = now;
  }
  if(raw_pressed != stable_pressed &&
     now - raw_changed_ms >= BOOT_BUTTON_DEBOUNCE_MS) {
    stable_pressed = raw_pressed;
    if(stable_pressed) {
      pressed_ms = raw_changed_ms;
    } else {
      if(raw_changed_ms - pressed_ms >= BOOT_BUTTON_RESTART_MS) {
        if(wifi_ota_is_busy()) {
          Serial.println("BOOT: restart ignored while firmware update is running");
        } else {
          Serial.println("BOOT: restarting");
          Serial.flush();
          ESP.restart();
        }
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  ESP_ERROR_CHECK(lcd_resume_from_sleep());

  // 1. NVS Speichersystem für Bluetooth initialisieren (WICHTIG gegen Absturz!)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

  if(!device_settings_init()) Serial.println("Settings: changes cannot be saved");
  // Initialize the I2C touch controller before the display.
  ESP_ERROR_CHECK(Touch_Init());

  // Panel + LVGL registration (flush cb, buffers, rotation, etc.)
  lcd_lvgl_Init();
  uart_switch_init();
  device_set_brightness(device_brightness(), false);

  if(INTRO_ENABLED) {
    play_intro();
    lcd_release_intro_buffer();
  }

  // SquareLine UI creation
  ui_init();
}

void loop()
{
  update_boot_button();
  uart_switch_loop();
  ui_init_step();
  wifi_ota_loop();
  ui_screen1_update();
  ui_screen4_update();
  ui_screen5_update();
  ui_screen6_update();
  ui_clock_update();
  ui_switches_update();

  float pressure_bar;
  if(uart_switch_take_pressure(&pressure_bar)) ui_set_pressure_bar(pressure_bar);
  lv_timer_handler();

  vTaskDelay(pdMS_TO_TICKS(5));
}