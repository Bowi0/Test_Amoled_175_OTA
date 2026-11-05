#include <Arduino.h>
#include "lvgl.h"
#include "ble_manager.h"
#include "lcd_bsp.h"   // lcd_lvgl_Init()
#include "ui/ui.h"     // ui_init()
#include "intro.h"
#include "wifi_ota.h"
#include <nvs_flash.h>

// Touch driver (CST9217)
#include "CST9217.h"

static constexpr uint8_t PRESSURE_SENSOR_PIN = 2;
static constexpr uint32_t PRESSURE_UPDATE_INTERVAL_MS = 16;
static constexpr bool PRESSURE_DEMO_ENABLED = true;
static constexpr uint32_t PRESSURE_DEMO_PERIOD_MS = 5000;
static constexpr uint32_t PRESSURE_DEMO_UPDATE_INTERVAL_MS = 16;

static void update_pressure_from_sensor()
{
  static uint32_t last_update_ms = 0;
  uint32_t now_ms = millis();
  if(now_ms - last_update_ms < PRESSURE_UPDATE_INTERVAL_MS) return;

  last_update_ms = now_ms;
  uint32_t voltage_mv_sum = 0;
  for(uint8_t sample = 0; sample < 16; sample++) {
    voltage_mv_sum += analogReadMilliVolts(PRESSURE_SENSOR_PIN);
  }

  float voltage_v = (voltage_mv_sum / 16.0f) / 1000.0f;
  float pressure_bar = (voltage_v - 0.5f) * 2.5f;
  ui_set_pressure_bar(pressure_bar);
}

static void update_pressure_demo()
{
  static uint32_t last_update_ms = 0;
  uint32_t now_ms = millis();
  if(now_ms - last_update_ms < PRESSURE_DEMO_UPDATE_INTERVAL_MS) return;

  last_update_ms = now_ms;
  uint32_t cycle_position_ms = now_ms % PRESSURE_DEMO_PERIOD_MS;
  if(cycle_position_ms > PRESSURE_DEMO_PERIOD_MS / 2) {
    cycle_position_ms = PRESSURE_DEMO_PERIOD_MS - cycle_position_ms;
  }
  float pressure_bar = (20.0f * cycle_position_ms) / PRESSURE_DEMO_PERIOD_MS;
  ui_set_pressure_bar(pressure_bar);
}


void setup()
{
  Serial.begin(115200);
  ESP_ERROR_CHECK(lcd_resume_from_sleep());

  // 1. NVS Speichersystem für Bluetooth initialisieren (WICHTIG gegen Absturz!)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

  analogReadResolution(12);
  analogSetPinAttenuation(PRESSURE_SENSOR_PIN, ADC_11db);

  // Initialize the I2C touch controller before the display.
  ESP_ERROR_CHECK(Touch_Init());

  // Panel + LVGL registration (flush cb, buffers, rotation, etc.)
  lcd_lvgl_Init();

  play_intro();

  // SquareLine UI creation
  ui_init();

  wifi_ota_init();
}

void loop()
{
  wifi_ota_loop();
  ui_screen4_update();

  if(PRESSURE_DEMO_ENABLED) update_pressure_demo();
  else update_pressure_from_sensor();
  lv_timer_handler();

  vTaskDelay(pdMS_TO_TICKS(5));
}