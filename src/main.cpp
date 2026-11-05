#include <Arduino.h>
#include "lvgl.h"
#include "ble_manager.h"
#include "lcd_bsp.h"   // lcd_lvgl_Init()
#include "ui/ui.h"     // ui_init()
#include "intro.h"
#include <nvs_flash.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"

// Touch driver (CST9217)
#include "CST9217.h"

static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;
static constexpr uint8_t PRESSURE_SENSOR_PIN = 2;
static constexpr uint32_t PRESSURE_UPDATE_INTERVAL_MS = 16;
static constexpr bool PRESSURE_DEMO_ENABLED = true;
static constexpr uint32_t PRESSURE_DEMO_PERIOD_MS = 5000;
static constexpr uint32_t PRESSURE_DEMO_UPDATE_INTERVAL_MS = 16;
static constexpr uint32_t BOOT_DEBOUNCE_MS = 40;

static void enter_deep_sleep()
{
  Serial.println("BOOT pressed: entering deep sleep.");
  Serial.flush();

  ESP_ERROR_CHECK(lcd_prepare_for_sleep());
  ESP_ERROR_CHECK(rtc_gpio_init(BOOT_BUTTON_GPIO));
  ESP_ERROR_CHECK(rtc_gpio_set_direction(BOOT_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK(rtc_gpio_pullup_en(BOOT_BUTTON_GPIO));
  ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO));
  ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BOOT_BUTTON_GPIO, 0));
  esp_deep_sleep_start();
}

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

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
    enter_deep_sleep();
  }

  ESP_ERROR_CHECK(lcd_resume_from_sleep());
  ESP_ERROR_CHECK(rtc_gpio_deinit(BOOT_BUTTON_GPIO));
  pinMode(static_cast<uint8_t>(BOOT_BUTTON_GPIO), INPUT_PULLUP);

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

    // 3. BLE DIREKT BEIM START TESTEN
  ble_init();

}

void loop()
{
  static bool boot_button_armed = false;
  static bool boot_press_timing = false;
  static uint32_t boot_press_started_ms = 0;
  const uint32_t now_ms = millis();

  if (digitalRead(static_cast<uint8_t>(BOOT_BUTTON_GPIO)) == HIGH) {
    boot_button_armed = true;
    boot_press_timing = false;
  } else if (boot_button_armed) {
    if (!boot_press_timing) {
      boot_press_timing = true;
      boot_press_started_ms = now_ms;
    } else if (now_ms - boot_press_started_ms >= BOOT_DEBOUNCE_MS) {
      enter_deep_sleep();
    }
  }

  if(PRESSURE_DEMO_ENABLED) update_pressure_demo();
  else update_pressure_from_sensor();
  lv_timer_handler();

  vTaskDelay(pdMS_TO_TICKS(5));
}