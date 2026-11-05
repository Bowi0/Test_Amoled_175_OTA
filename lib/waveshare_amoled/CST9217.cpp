#include "CST9217.h"

#include <Arduino.h>
#include <Wire.h>

#include "lcd_config.h"

namespace {

constexpr uint8_t TOUCH_ADDRESS = 0x5A;
constexpr uint8_t TOUCH_ACK = 0xAB;
constexpr uint8_t MAX_TOUCH_POINTS = 2;
constexpr uint8_t TOUCH_PACKET_SIZE = MAX_TOUCH_POINTS * 5 + 5;

bool write_register(uint8_t high, uint8_t low)
{
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(high);
  Wire.write(low);
  return Wire.endTransmission() == 0;
}

bool read_command(uint8_t high, uint8_t low, uint8_t *buffer, size_t length)
{
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(high);
  Wire.write(low);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDRESS, length, true) != length) return false;

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = Wire.read();
  }
  return true;
}

uint32_t read_little_endian_u32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

} // namespace

esp_err_t Touch_Init(void)
{
  if (!Wire.begin(EXAMPLE_PIN_NUM_TOUCH_SDA, EXAMPLE_PIN_NUM_TOUCH_SCL)) {
    Serial.println("CST9217 I2C initialization failed.");
    return ESP_FAIL;
  }
  Wire.setClock(300000);

  pinMode(EXAMPLE_PIN_NUM_TOUCH_RST, OUTPUT);
  digitalWrite(EXAMPLE_PIN_NUM_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(EXAMPLE_PIN_NUM_TOUCH_RST, HIGH);
  delay(30);

  if (!write_register(0xD1, 0x01)) {
    Serial.println("CST9217 command-mode initialization failed.");
    return ESP_FAIL;
  }
  delay(10);

  uint8_t data[8] = {};
  if (!read_command(0xD1, 0xFC, data, 4) ||
      (read_little_endian_u32(data) & 0xFFFF0000UL) != 0xCACA0000UL) {
    Serial.println("CST9217 firmware check failed.");
    return ESP_FAIL;
  }

  if (!read_command(0xD1, 0xF8, data, 4)) {
    Serial.println("CST9217 resolution query failed.");
    return ESP_FAIL;
  }

  if (!read_command(0xD2, 0x04, data, 4)) {
    Serial.println("CST9217 chip identification failed.");
    return ESP_FAIL;
  }
  const uint16_t chip_id =
      static_cast<uint16_t>(data[2]) |
      (static_cast<uint16_t>(data[3]) << 8);
  if (chip_id != 0x9217 && chip_id != 0x9220) {
    Serial.printf("Unsupported CST92xx touch controller: 0x%04X\n", chip_id);
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (!read_command(0xD2, 0x08, data, sizeof(data))) {
    Serial.println("CST9217 firmware query failed.");
    return ESP_FAIL;
  }
  if (read_little_endian_u32(data) == 0xA5A5A5A5UL) {
    Serial.println("CST9217 has no valid firmware.");
    return ESP_FAIL;
  }

  pinMode(EXAMPLE_PIN_NUM_TOUCH_INT, INPUT_PULLUP);
  Serial.printf("Touch controller initialized: 0x%04X\n", chip_id);
  return ESP_OK;
}

uint8_t getTouch(uint16_t *x, uint16_t *y)
{
  if (x == nullptr || y == nullptr) return 0;

  uint8_t packet[TOUCH_PACKET_SIZE] = {};
  if (!read_command(0xD0, 0x00, packet, sizeof(packet))) return 0;

  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(0xD0);
  Wire.write(0x00);
  Wire.write(TOUCH_ACK);
  if (Wire.endTransmission() != 0 || packet[6] != TOUCH_ACK) return 0;

  const uint8_t point_count = packet[5] & 0x7F;
  if (point_count == 0 || point_count > MAX_TOUCH_POINTS) return 0;

  const uint8_t *point = packet;
  const uint8_t point_id = point[0] >> 4;
  if ((point[0] & 0x0F) != 0x06 || point_id >= MAX_TOUCH_POINTS) return 0;

  uint16_t raw_x = (static_cast<uint16_t>(point[1]) << 4) | (point[3] >> 4);
  uint16_t raw_y = (static_cast<uint16_t>(point[2]) << 4) | (point[3] & 0x0F);

  raw_x = EXAMPLE_LCD_H_RES - raw_x;
  raw_y = EXAMPLE_LCD_V_RES - raw_y;
  *x = raw_x >= EXAMPLE_LCD_H_RES ? EXAMPLE_LCD_H_RES - 1 : raw_x;
  *y = raw_y >= EXAMPLE_LCD_V_RES ? EXAMPLE_LCD_V_RES - 1 : raw_y;
  return 1;
}
