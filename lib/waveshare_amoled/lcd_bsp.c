#include "lcd_bsp.h"
#include "esp_lcd_sh8601.h"
#include "lcd_config.h"
#include "CST9217.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#define LCD_HOST    SPI2_HOST

//#define EXAMPLE_Rotate_90
#define SH8601_ID 0x86
#define CO5300_ID 0xff
#define INTRO_SOURCE_SIZE 233
#define INTRO_OUTPUT_SIZE (INTRO_SOURCE_SIZE * 2)
#define INTRO_CHUNK_SOURCE_ROWS 16
static uint8_t READ_LCD_ID = CO5300_ID;
static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;
static SemaphoreHandle_t lcd_intro_transfer_done = NULL;
static volatile bool lcd_intro_transfer_pending = false;
static uint16_t *lcd_intro_scaled_buffer = NULL;
static uint16_t lcd_intro_scaled_buffer_rows = 0;

// --- Forward Declarations ---
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                            esp_lcd_panel_io_event_data_t *edata,
                                            void *user_ctx);
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

// --- Init Commands ---
static const sh8601_lcd_init_cmd_t sh8601_lcd_init_cmds[] =
{
  {0x11, (uint8_t []){0x00}, 0, 120},
  {0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
  {0x35, (uint8_t []){0x00}, 1, 0},
  {0x53, (uint8_t []){0x20}, 1, 10},
  {0x51, (uint8_t []){0x00}, 1, 10},
  {0x29, (uint8_t []){0x00}, 0, 10},
  {0x51, (uint8_t []){0xFF}, 1, 0},
};

static const sh8601_lcd_init_cmd_t co5300_lcd_init_cmds[] =
{
  {0x11, (uint8_t []){0x00}, 0, 120},
  {0xFE, (uint8_t []){0x00}, 1, 0},
  {0xC4, (uint8_t []){0x80}, 1, 0},
  {0x3A, (uint8_t []){0x55}, 1, 0},
  {0x53, (uint8_t []){0x20}, 1, 0},
  {0x63, (uint8_t []){0xFF}, 1, 0},
};

void lcd_lvgl_Init(void)
{
  static lv_disp_draw_buf_t disp_buf;
  static lv_disp_drv_t disp_drv;

  lcd_intro_transfer_done = xSemaphoreCreateBinary();
  assert(lcd_intro_transfer_done != NULL);

  // --- 1. QSPI Bus Initialisierung ---
  const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                               EXAMPLE_PIN_NUM_LCD_DATA0,
                                                               EXAMPLE_PIN_NUM_LCD_DATA1,
                                                               EXAMPLE_PIN_NUM_LCD_DATA2,
                                                               EXAMPLE_PIN_NUM_LCD_DATA3,
                                                               EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);

  ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                               example_notify_lvgl_flush_ready,
                                                                               &disp_drv);
 
  sh8601_vendor_config_t vendor_config =
  {
    .flags =
    {
      .use_qspi_interface = 1,
    },
  };

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));
  lcd_io_handle = io_handle;

  const esp_lcd_panel_dev_config_t panel_config =
  {
    .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = LCD_BIT_PER_PIXEL,
    .vendor_config = &vendor_config,
  };

  vendor_config.init_cmds = (READ_LCD_ID == SH8601_ID) ? sh8601_lcd_init_cmds : co5300_lcd_init_cmds;
  vendor_config.init_cmds_size = (READ_LCD_ID == SH8601_ID)
                                 ? (sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]))
                                 : (sizeof(co5300_lcd_init_cmds) / sizeof(co5300_lcd_init_cmds[0]));

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &lcd_panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_reset(lcd_panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_init(lcd_panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));
  lcd_set_brightness(0xD0);

  // --- 2. LVGL & Speicher-Initialisierung ---
  lv_init();

  // 20 Zeilen Pufferhöhe für optimale SRAM-Nutzung & schnelle Partial-Updates
  #define MY_BUF_HEIGHT 40
  const uint32_t buf_pixels = EXAMPLE_LCD_H_RES * MY_BUF_HEIGHT;
  const size_t buf_size = buf_pixels * sizeof(lv_color_t);

  // Beide Puffer im schnellen internen SRAM verankern (MALLOC_CAP_INTERNAL)
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!buf1) buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  assert(buf1);

  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!buf2) buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  assert(buf2);

  // Registriere beide Puffer in LVGL (Echtes Asynchrones Double-Buffering)
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_pixels);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = EXAMPLE_LCD_H_RES;
  disp_drv.ver_res = EXAMPLE_LCD_V_RES;
  disp_drv.flush_cb = example_lvgl_flush_cb;
  disp_drv.rounder_cb = example_lvgl_rounder_cb;
  disp_drv.draw_buf = &disp_buf;
  disp_drv.user_data = lcd_panel_handle;

  // Auskommentiert lassen für Partial-Updates!
  // disp_drv.full_refresh = 1;
  // disp_drv.direct_mode = 1;

#ifdef EXAMPLE_Rotate_90
  disp_drv.sw_rotate = 1;
  disp_drv.rotated = LV_DISP_ROT_270;
#endif

  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

  // --- 3. Touch input ---
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.disp = disp;
  indev_drv.read_cb = example_lvgl_touch_cb;
  lv_indev_drv_register(&indev_drv);
}

void lcd_set_brightness(uint8_t brightness)
{
  if(lcd_io_handle != NULL) {
    const uint32_t command = (0x02UL << 24) | (0x51UL << 8);
    esp_lcd_panel_io_tx_param(lcd_io_handle, command, &brightness, 1);
  }
}

esp_err_t lcd_draw_intro_frame(uint16_t x, uint16_t y, uint16_t width,
                               uint16_t height, uint16_t *pixels)
{
  if (lcd_panel_handle == NULL || lcd_intro_transfer_done == NULL ||
      pixels == NULL || width == 0 || height == 0 ||
      x + width > EXAMPLE_LCD_H_RES || y + height > EXAMPLE_LCD_V_RES) {
    return ESP_ERR_INVALID_ARG;
  }

  while (xSemaphoreTake(lcd_intro_transfer_done, 0) == pdTRUE) {
  }

  const uint32_t pixel_count = (uint32_t)width * height;
  uint32_t *pixels32 = (uint32_t *)pixels;
  for (uint32_t i = 0; i < pixel_count / 2; i++) {
    const uint32_t value = pixels32[i];
    pixels32[i] = ((value & 0x00FF00FF) << 8) |
                  ((value & 0xFF00FF00) >> 8);
  }
  if (pixel_count & 1) {
    const uint16_t value = pixels[pixel_count - 1];
    pixels[pixel_count - 1] = (value << 8) | (value >> 8);
  }

  lcd_intro_transfer_pending = true;
  const int x_offset = READ_LCD_ID == SH8601_ID ? 0 : 0x06;
  esp_err_t err = esp_lcd_panel_draw_bitmap(
      lcd_panel_handle, x + x_offset, y, x + width + x_offset, y + height,
      pixels);
  if (err == ESP_OK &&
      xSemaphoreTake(lcd_intro_transfer_done, portMAX_DELAY) != pdTRUE) {
    err = ESP_ERR_TIMEOUT;
  }
  lcd_intro_transfer_pending = false;

  for (uint32_t i = 0; i < pixel_count / 2; i++) {
    const uint32_t value = pixels32[i];
    pixels32[i] = ((value & 0x00FF00FF) << 8) |
                  ((value & 0xFF00FF00) >> 8);
  }
  if (pixel_count & 1) {
    const uint16_t value = pixels[pixel_count - 1];
    pixels[pixel_count - 1] = (value << 8) | (value >> 8);
  }

  return err;
}

esp_err_t lcd_draw_intro_frame_2x(uint16_t *pixels, uint64_t *scale_time_us,
                                  uint64_t *transfer_time_us)
{
  if (lcd_panel_handle == NULL || lcd_intro_transfer_done == NULL ||
      pixels == NULL || scale_time_us == NULL || transfer_time_us == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *scale_time_us = 0;
  *transfer_time_us = 0;

  if (lcd_intro_scaled_buffer == NULL) {
    uint16_t rows = INTRO_CHUNK_SOURCE_ROWS;
    while (rows > 0) {
      lcd_intro_scaled_buffer = (uint16_t *)heap_caps_malloc(
          INTRO_OUTPUT_SIZE * rows * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (lcd_intro_scaled_buffer != NULL) {
        lcd_intro_scaled_buffer_rows = rows;
        break;
      }
      rows /= 2;
    }
    if (lcd_intro_scaled_buffer == NULL) return ESP_ERR_NO_MEM;
    if (lcd_intro_scaled_buffer_rows < INTRO_CHUNK_SOURCE_ROWS) {
      ESP_LOGI("lcd_bsp", "Intro display: using %u-row DMA chunks",
               lcd_intro_scaled_buffer_rows);
    }
  }

  const uint16_t chunk_source_rows = lcd_intro_scaled_buffer_rows;
  const int x_offset = READ_LCD_ID == SH8601_ID ? 0 : 0x06;
  for (uint16_t source_y = 0; source_y < INTRO_SOURCE_SIZE;
       source_y += chunk_source_rows) {
    const int64_t scaling_started_us = esp_timer_get_time();
    const uint16_t rows = (source_y + chunk_source_rows <= INTRO_SOURCE_SIZE)
                              ? chunk_source_rows
                              : INTRO_SOURCE_SIZE - source_y;

    for (uint16_t row = 0; row < rows; row++) {
      const uint16_t *source =
          pixels + (source_y + row) * INTRO_SOURCE_SIZE;
      uint16_t *output =
          lcd_intro_scaled_buffer + row * 2 * INTRO_OUTPUT_SIZE;
      uint32_t *output_words = (uint32_t *)output;
      uint32_t *output_words_second =
          (uint32_t *)(output + INTRO_OUTPUT_SIZE);
      for (uint16_t column = 0; column < INTRO_SOURCE_SIZE; column++) {
        const uint16_t pixel = source[column];
        const uint16_t swapped = (pixel << 8) | (pixel >> 8);
        const uint32_t repeated = swapped | ((uint32_t)swapped << 16);
        output_words[column] = repeated;
        output_words_second[column] = repeated;
      }
    }

    *scale_time_us += esp_timer_get_time() - scaling_started_us;
    while (xSemaphoreTake(lcd_intro_transfer_done, 0) == pdTRUE) {
    }
    lcd_intro_transfer_pending = true;
    const int64_t transfer_started_us = esp_timer_get_time();
    esp_err_t err = esp_lcd_panel_draw_bitmap(
        lcd_panel_handle, x_offset, source_y * 2,
        INTRO_OUTPUT_SIZE + x_offset, source_y * 2 + rows * 2,
        lcd_intro_scaled_buffer);
    if (err == ESP_OK &&
        xSemaphoreTake(lcd_intro_transfer_done, portMAX_DELAY) != pdTRUE) {
      err = ESP_ERR_TIMEOUT;
    }
    *transfer_time_us += esp_timer_get_time() - transfer_started_us;
    lcd_intro_transfer_pending = false;
    if (err != ESP_OK) return err;
  }

  return ESP_OK;
}

esp_err_t lcd_prepare_for_sleep(void)
{
  if (lcd_panel_handle != NULL && lcd_io_handle != NULL) {
    uint8_t brightness = 0;
    esp_err_t err = esp_lcd_panel_io_tx_param(
        lcd_io_handle, (0x02UL << 24) | (0x51UL << 8), &brightness, 1);
    if (err != ESP_OK) return err;

    err = esp_lcd_panel_disp_on_off(lcd_panel_handle, false);
    if (err != ESP_OK) return err;

    err = esp_lcd_panel_io_tx_param(
        lcd_io_handle, (0x02UL << 24) | (0x10UL << 8), NULL, 0);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(120));
  }

  const gpio_num_t reset_gpio = (gpio_num_t)EXAMPLE_PIN_NUM_LCD_RST;
  esp_err_t err = gpio_reset_pin(reset_gpio);
  if (err != ESP_OK) return err;
  err = gpio_set_direction(reset_gpio, GPIO_MODE_OUTPUT);
  if (err != ESP_OK) return err;
  err = gpio_set_level(reset_gpio, 0);
  if (err != ESP_OK) return err;
  err = gpio_hold_en(reset_gpio);
  if (err != ESP_OK) return err;
  gpio_deep_sleep_hold_en();
  return ESP_OK;
}

esp_err_t lcd_resume_from_sleep(void)
{
  const gpio_num_t reset_gpio = (gpio_num_t)EXAMPLE_PIN_NUM_LCD_RST;
  gpio_deep_sleep_hold_dis();
  esp_err_t err = gpio_hold_dis(reset_gpio);
  if (err != ESP_OK) return err;
  return gpio_reset_pin(reset_gpio);
}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx)
{
  if (lcd_intro_transfer_pending) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(lcd_intro_transfer_done,
                          &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
  } else {
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
  }
  return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    const int offsetx1 = (READ_LCD_ID == SH8601_ID) ? area->x1 : area->x1 + 0x06;
    const int offsetx2 = (READ_LCD_ID == SH8601_ID) ? area->x2 : area->x2 + 0x06;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    const uint32_t total_pixels = (uint32_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    // --- SCHNELLSTER IN-PLACE 32-BIT BITSWAP ---
    // Tauscht 2 Pixel (4 Bytes) gleichzeitig direkt im SRAM-Buffer
    uint32_t *buf32 = (uint32_t *)color_map;
    uint32_t count32 = total_pixels / 2;

    for (uint32_t i = 0; i < count32; i++) {
        uint32_t val = buf32[i];
        buf32[i] = ((val & 0x00FF00FF) << 8) | ((val & 0xFF00FF00) >> 8);
    }

    // Abfangen von ungeraden Pixelzahlen
    if (total_pixels & 1) {
        uint16_t *buf16 = (uint16_t *)color_map;
        uint16_t val16 = buf16[total_pixels - 1];
        buf16[total_pixels - 1] = (val16 << 8) | (val16 >> 8);
    }

    // Direkte Abgabe des bearbeiteten SRAM-Buffers an den DMA-Controller
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;
  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;

  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;
  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  uint16_t tp_x, tp_y;
  uint8_t win = getTouch(&tp_x, &tp_y);

  if(win) {
    data->point.x = tp_x;
    data->point.y = tp_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}