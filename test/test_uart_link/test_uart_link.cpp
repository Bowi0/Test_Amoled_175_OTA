#include <cassert>
#include <cstdint>
#include <deque>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cmath>

#include "Arduino.h"
#include "firmware_version.h"
#include "uart_switch_protocol.h"
#include "uart_switch.h"
#include "ui/screens/steam_needle_geometry.h"

#define setup receiver_setup
#define loop receiver_loop
#include "..\..\..\ESP32-S3-Zero-KM\src\main.cpp"
#undef setup
#undef loop

static uint32_t clock_ms;
static int pin_values[5];
static std::deque<uint8_t> incoming[2];
static bool drop[2];
static int next_endpoint;
static uint8_t last_set[SWITCH_FRAME_SIZE];
static std::vector<uint8_t> ota_bytes;
static bool selected;
static bool restarted;
static bool pump_receiver;
static bool inside_receiver;
static bool drop_first_data_ack;
static bool drop_version_responses;
static uint8_t last_version_request;
static int adc_raw;
static bool adc_error;
static unsigned configured_channels;
TestSerial Serial;
TestESP ESP;

uint32_t millis() { return clock_ms; }
void delay(uint32_t ms)
{
    clock_ms += ms;
    if(pump_receiver && !inside_receiver) {
        inside_receiver = true;
        receiver_loop();
        inside_receiver = false;
    }
}
void digitalWrite(uint8_t pin, int value) { pin_values[pin] = value; }
void pinMode(uint8_t, int) {}
int gpio_set_level(gpio_num_t pin, int value) { digitalWrite(pin, value); return 0; }
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *config, adc_oneshot_unit_handle_t *handle)
{
    assert(config->unit_id == ADC_UNIT_1);
    *handle = &adc_raw;
    return ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t) { return ESP_OK; }
esp_err_t adc_oneshot_io_to_channel(int pin, adc_unit_t *unit, adc_channel_t *channel)
{
    assert(pin == 5 || pin == 6);
    *unit = ADC_UNIT_1;
    *channel = pin - 1;
    return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t channel,
                                      const adc_oneshot_chan_cfg_t *config)
{
    assert(config->atten == ADC_ATTEN_DB_12 && config->bitwidth == ADC_BITWIDTH_12);
    configured_channels |= 1U << channel;
    return ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t channel, int *value)
{
    assert(channel == ADC_CHANNEL_4);
    if(adc_error) return ESP_ERR_INVALID_ARG;
    *value = adc_raw;
    return ESP_OK;
}
void TestESP::restart() { restarted = true; }
static esp_partition_t mock_partition = {0x330000};
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return &mock_partition; }
int esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *handle)
{
    ota_bytes.clear();
    *handle = 1;
    return ESP_OK;
}
int esp_ota_write(esp_ota_handle_t, const void *data, size_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    ota_bytes.insert(ota_bytes.end(), bytes, bytes + size);
    return ESP_OK;
}
int esp_ota_end(esp_ota_handle_t) { return ESP_OK; }
int esp_ota_abort(esp_ota_handle_t) { return ESP_OK; }
int esp_ota_set_boot_partition(const esp_partition_t *) { selected = true; return ESP_OK; }
void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { std::memset(ctx, 0, sizeof(*ctx)); }
int mbedtls_sha256_starts(mbedtls_sha256_context *, int) { return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const uint8_t *data, size_t size)
{
    for(size_t i = 0; i < size; i++) ctx->digest[(ctx->offset++) % 32] ^= data[i];
    return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, uint8_t *out)
{
    std::memcpy(out, ctx->digest, 32);
    return 0;
}
void mbedtls_sha256_free(mbedtls_sha256_context *) {}

void HardwareSerial::begin(int, int, int rx, int tx)
{
    assert(rx == 44 && tx == 43);
    if(endpoint < 0) endpoint = next_endpoint++;
}
int HardwareSerial::available() { return static_cast<int>(incoming[endpoint].size()); }
int HardwareSerial::read()
{
    int value = incoming[endpoint].front();
    incoming[endpoint].pop_front();
    return value;
}
size_t HardwareSerial::write(const uint8_t *data, size_t size)
{
    if(endpoint == 1 && drop_first_data_ack && size >= 11 &&
       data[0] == 0xC3 && data[2] == UPDATE_REPLY && data[9] == UPDATE_DATA) {
        drop_first_data_ack = false;
        return size;
    }
    if(endpoint == 0 && size == SWITCH_FRAME_SIZE && data[2] == SWITCH_SET)
        for(size_t i = 0; i < size; i++) last_set[i] = data[i];
    if(endpoint == 0 && size == SWITCH_FRAME_SIZE && data[2] == SWITCH_VERSION_REQUEST)
        last_version_request = data[3];
    if(endpoint == 1 && drop_version_responses && size == SWITCH_FRAME_SIZE &&
       data[2] >= SWITCH_VERSION_MAJOR && data[2] <= SWITCH_VERSION_PATCH)
        return size;
    if(!drop[endpoint])
        for(size_t i = 0; i < size; i++) incoming[1 - endpoint].push_back(data[i]);
    return size;
}

static void advance(uint32_t duration)
{
    uint32_t until = clock_ms + duration;
    while(clock_ms < until) {
        uart_switch_loop();
        receiver_loop();
        clock_ms += 9;
    }
}

static void protocol_test()
{
    assert(steam_simulated_angle(0) == 219.0f);
    assert(steam_simulated_angle(2500) == 180.0f);
    assert(steam_simulated_angle(5000) == 141.0f);
    assert(steam_simulated_angle(7500) == 180.0f);
    assert(steam_simulated_angle(10000) == 219.0f);
    assert(steam_simulated_angle(12500) == 180.0f);
    assert(temperature_simulated_angle(0) == 230.0f);
    assert(temperature_simulated_angle(2500) == 360.0f);
    assert(temperature_simulated_angle(5000) == 490.0f);
    assert(temperature_simulated_angle(7500) == 360.0f);
    assert(temperature_simulated_angle(10000) == 230.0f);
    assert(brew_pressure_angle(-1.0f) == 254.0f);
    assert(brew_pressure_angle(0.0f) == 254.0f);
    assert(brew_pressure_angle(7.5f) == 360.0f);
    assert(brew_pressure_angle(15.0f) == 466.0f);
    assert(brew_pressure_angle(20.0f) == 466.0f);
    const auto brew_end = steam_needle_points(233, 233, PRESSURE_NEEDLE_INNER_RADIUS,
                                              233 - BREW_NEEDLE_EDGE_GAP, brew_pressure_angle(15.0f));
    const auto brew_106 = steam_needle_points(233, 233, PRESSURE_NEEDLE_INNER_RADIUS,
                                              233 - BREW_NEEDLE_EDGE_GAP, 106);
    assert(std::fabs(brew_end.outer_x - brew_106.outer_x) < 0.001f);
    assert(std::fabs(brew_end.outer_y - brew_106.outer_y) < 0.001f);
    assert(std::fabs(std::hypot(brew_end.inner_x - 233, brew_end.inner_y - 233) - 108) < 0.001f);
    assert(std::fabs(std::hypot(brew_end.outer_x - 233, brew_end.outer_y - 233) - 173) < 0.001f);
    const auto temperature_end = steam_needle_points(233, 233, 0, 80, temperature_simulated_angle(5000));
    const auto temperature_130 = steam_needle_points(233, 233, 0, 80, 130);
    assert(std::fabs(temperature_end.outer_x - temperature_130.outer_x) < 0.001f);
    assert(std::fabs(temperature_end.outer_y - temperature_130.outer_y) < 0.001f);
    for(float angle : {141.0f, 180.0f, 219.0f}) {
        const auto points = steam_needle_points(233, 233, PRESSURE_NEEDLE_INNER_RADIUS,
                                                233 - STEAM_NEEDLE_EDGE_GAP, angle);
        assert(std::fabs(std::hypot(points.inner_x - 233, points.inner_y - 233) - 108) < 0.001f);
        assert(std::fabs(std::hypot(points.outer_x - 233, points.outer_y - 233) - 203) < 0.001f);
    }
    const auto left = steam_needle_points(233, 233, 100, 223, 219);
    const auto right = steam_needle_points(233, 233, 100, 223, 141);
    assert(left.outer_x < 233 && right.outer_x > 233);
    for(int angle : {0, 90, 180, 270, 360}) {
        const auto points = steam_needle_points(233, 233, 0, 80, (float)angle);
        assert(std::fabs(points.inner_x - 233) < 0.001f);
        assert(std::fabs(points.inner_y - 233) < 0.001f);
        assert(std::fabs(std::hypot(points.outer_x - 233, points.outer_y - 233) - 80) < 0.001f);
        if(angle == 0 || angle == 360) assert(std::fabs(points.outer_y - 153) < 0.001f);
        if(angle == 90) assert(std::fabs(points.outer_x - 313) < 0.001f);
        if(angle == 180) assert(std::fabs(points.outer_y - 313) < 0.001f);
        if(angle == 270) assert(std::fabs(points.outer_x - 153) < 0.001f);
    }
    uint32_t current[3];
    uint32_t available[3];
    assert(parse_firmware_version("v0.2.14", current));
    assert(parse_firmware_version("0.2.14", available));
    assert(!is_newer_firmware_version(available, current));
    assert(parse_firmware_version("v0.2.13", available));
    assert(!is_newer_firmware_version(available, current));
    assert(parse_firmware_version("v0.2.15", available));
    assert(is_newer_firmware_version(available, current));
    assert(parse_firmware_version("V1.0.0", available));
    assert(is_newer_firmware_version(available, current));
    assert(parse_firmware_version("4294967295.0.0", available));
    assert(!parse_firmware_version("4294967296.0.0", available));
    assert(!parse_firmware_version("v1.2", available));
    assert(!parse_firmware_version("v1.2.3-beta", available));
    assert(!parse_firmware_version(nullptr, available));
    const uint8_t reference[] = "123456789";
    assert(switch_crc16(reference, 9) == 0x29B1);
    uint8_t frame[SWITCH_FRAME_SIZE];
    switch_encode(frame, SWITCH_SET, 42, 3);
    switch_parser_t p = {};
    for(unsigned i = 0; i < 3; i++) assert(!switch_parse(&p, frame[i]));
    for(unsigned i = 0; i < SWITCH_FRAME_SIZE - 1; i++) assert(!switch_parse(&p, frame[i]));
    assert(switch_parse(&p, frame[SWITCH_FRAME_SIZE - 1]));
    assert(p.data[2] == SWITCH_SET && p.data[3] == 42 && p.data[4] == 3);
    frame[4] ^= 1;
    for(uint8_t value : frame) assert(!switch_parse(&p, value));
    switch_encode(frame, SWITCH_ACK, 0xA5, 0x0F);
    for(unsigned i = 0; i < SWITCH_FRAME_SIZE - 1; i++) assert(!switch_parse(&p, frame[i]));
    assert(switch_parse(&p, frame[SWITCH_FRAME_SIZE - 1]));
}

static void assert_off()
{
    for(unsigned pin = 1; pin <= 4; pin++) assert(pin_values[pin] == LOW);
}

int main()
{
    protocol_test();
    uart_switch_init();
    receiver_setup();
    assert(configured_channels == ((1U << 4) | (1U << 5)));
    uint16_t measured;
    adc_raw = 0;
    assert(read_pressure(&measured) && measured == 0);
    for(unsigned i = 0; i < PRESSURE_AVERAGE_WINDOW - 1; i++) {
        adc_raw = (i & 1) ? 4095 : 0;
        assert(read_pressure(&measured));
    }
    assert(measured == 375);
    adc_raw = 4095;
    assert(read_pressure(&measured) && measured == 500);
    for(unsigned i = 1; i < PRESSURE_AVERAGE_WINDOW; i++)
        assert(read_pressure(&measured));
    assert(measured == 1000);
    reset_pressure_average();
    adc_raw = 0;
    assert_off();
    advance(300);
    assert(uart_switch_online() && uart_switch_state() == 0);
    float pressure_bar = -1.0f;
    assert(uart_switch_take_pressure(&pressure_bar));
    assert(pressure_bar >= 0.0f && pressure_bar <= 10.0f);
    assert(!uart_switch_take_pressure(&pressure_bar));
    uart_measurements_t measurements = {};
    assert(uart_switch_measurements(&measurements));
    assert(!measurements.steam_pressure_valid && !measurements.brew_temperature_valid &&
           !measurements.brewhead_temperature_valid);
    uint8_t sensor_frame[SWITCH_FRAME_SIZE];
    switch_encode(sensor_frame, SWITCH_STEAM_PRESSURE, 0, 150);
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    switch_encode(sensor_frame, SWITCH_BREW_TEMPERATURE, 0x24, 0x2a);
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    switch_encode(sensor_frame, SWITCH_BREWHEAD_TEMPERATURE, 0xff, 0x83);
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    uart_switch_loop();
    assert(uart_switch_take_steam_pressure(&pressure_bar) && pressure_bar == 1.5f);
    assert(!uart_switch_take_steam_pressure(&pressure_bar));
    assert(uart_switch_measurements(&measurements));
    assert(measurements.steam_pressure_valid && measurements.brew_temperature_valid &&
           measurements.brewhead_temperature_valid);
    assert(std::fabs(measurements.brew_temperature_c - 92.58f) < 0.001f);
    assert(std::fabs(measurements.brewhead_temperature_c - (-1.25f)) < 0.001f);
    switch_encode(sensor_frame, SWITCH_STEAM_PRESSURE, 0, 200);
    sensor_frame[6] ^= 1;
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    uart_switch_loop();
    assert(!uart_switch_take_steam_pressure(&pressure_bar));
    assert(uart_switch_measurements(&measurements) && measurements.steam_pressure_bar == 1.5f);
    switch_encode(sensor_frame, SWITCH_STEAM_PRESSURE, 0, 100);
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    switch_encode(sensor_frame, SWITCH_STEAM_PRESSURE, 1, 44);
    for(uint8_t value : sensor_frame) incoming[0].push_back(value);
    uart_switch_loop();
    assert(uart_switch_take_steam_pressure(&pressure_bar) && pressure_bar == 3.0f);
    advance(100);
    uart_switch_loop();
    assert(uart_switch_take_pressure(&pressure_bar));
    assert(pressure_bar == 0.0f);
    adc_raw = 2048;
    advance(200);
    uart_switch_loop();
    assert(uart_switch_take_pressure(&pressure_bar) && pressure_bar == 5.0f);
    adc_raw = 4095;
    advance(200);
    uart_switch_loop();
    assert(uart_switch_take_pressure(&pressure_bar) && pressure_bar == 10.0f);
    adc_error = true;
    advance(100);
    uart_switch_loop();
    assert(!uart_switch_take_pressure(&pressure_bar));
    adc_error = false;
    adc_raw = 0;
    advance(100);
    uart_switch_loop();
    assert(uart_switch_take_pressure(&pressure_bar) && pressure_bar == 0.0f);
    uint8_t pressure_frame[SWITCH_FRAME_SIZE];
    switch_encode(pressure_frame, SWITCH_PRESSURE, 0x03, 0xE8);
    for(uint8_t value : pressure_frame) incoming[0].push_back(value);
    uart_switch_loop();
    assert(uart_switch_take_pressure(&pressure_bar) && pressure_bar == 10.0f);
    const float last_pressure = pressure_bar;
    uint8_t invalid_pressure[SWITCH_FRAME_SIZE];
    switch_encode(invalid_pressure, SWITCH_PRESSURE, 0x04, 0x00);
    for(uint8_t value : invalid_pressure) incoming[0].push_back(value);
    uart_switch_loop();
    assert(!uart_switch_take_pressure(&pressure_bar));
    assert(pressure_bar == last_pressure);
    switch_encode(invalid_pressure, SWITCH_PRESSURE, 0, 0);
    invalid_pressure[6] ^= 1;
    for(uint8_t value : invalid_pressure) incoming[0].push_back(value);
    uart_switch_loop();
    assert(!uart_switch_take_pressure(&pressure_bar));
    char version[16] = {};
    assert(!uart_switch_version(version, sizeof(version)));
    advance(1000);
    assert(uart_switch_version(version, sizeof(version)));
    assert(std::strcmp(version, "v0.1.2") == 0);

    drop_version_responses = true;
    advance(5000);
    uint8_t version_frame[SWITCH_FRAME_SIZE];
    switch_encode(version_frame, SWITCH_VERSION_MAJOR, last_version_request, 9);
    for(uint8_t value : version_frame) incoming[0].push_back(value);
    uart_switch_loop();
    assert(uart_switch_version(version, sizeof(version)));
    assert(std::strcmp(version, "v0.1.2") == 0);
    for(uint8_t type = SWITCH_VERSION_MINOR; type <= SWITCH_VERSION_PATCH; type++) {
        switch_encode(version_frame, type, static_cast<uint8_t>(last_version_request - 1), 9);
        for(uint8_t value : version_frame) incoming[0].push_back(value);
    }
    uart_switch_loop();
    assert(uart_switch_version(version, sizeof(version)));
    assert(std::strcmp(version, "v0.1.2") == 0);
    advance(1600);
    assert(!uart_switch_version(version, sizeof(version)));
    drop_version_responses = false;
    advance(5000);
    assert(uart_switch_version(version, sizeof(version)));
    assert(std::strcmp(version, "v0.1.2") == 0);

    assert(uart_switch_toggle(0));
    assert(uart_switch_pending());
    assert(!uart_switch_toggle(0));
    assert(!uart_switch_toggle(1));
    advance(100);
    assert(pin_values[1] == HIGH && uart_switch_state() == 1 && !uart_switch_pending());

    // Simulate RAM reset as well as setup() on the receiver.
    parser = {};
    state = 0;
    last_command = 0;
    armed = false;
    receiver_setup();
    assert_off();
    advance(2200);
    assert(uart_switch_online() && uart_switch_state() == 0);
    assert_off();

    assert(uart_switch_toggle(1));
    advance(100);
    assert(pin_values[2] == HIGH);
    drop[0] = true;
    advance(2300);
    assert(!uart_switch_online() && uart_switch_state() == 0);
    assert(!uart_switch_take_pressure(&pressure_bar));
    assert(!uart_switch_take_steam_pressure(&pressure_bar));
    assert(uart_switch_measurements(&measurements));
    assert(!measurements.steam_pressure_valid && !measurements.brew_temperature_valid &&
           !measurements.brewhead_temperature_valid);
    assert(!pressure_streaming);
    assert(!uart_switch_version(version, sizeof(version)));
    assert_off();
    drop[0] = false;
    advance(500);
    assert(uart_switch_online() && uart_switch_state() == 0);
    assert_off();

    assert(uart_switch_toggle(2));
    advance(100);
    assert(pin_values[3] == HIGH);
    drop[1] = true;
    advance(1900);
    assert(!uart_switch_online() && uart_switch_state() == 0);
    assert_off();
    drop[1] = false;
    advance(500);
    assert(uart_switch_online() && uart_switch_state() == 0);
    assert_off();

    assert(uart_switch_toggle(3));
    advance(100);
    assert(pin_values[4] == HIGH);
    drop[0] = true;
    uint8_t bad_frame[SWITCH_FRAME_SIZE];
    switch_encode(bad_frame, SWITCH_SET, 3, 0x0F);
    bad_frame[6] ^= 1;
    for(unsigned i = 0; i < 12; i++) {
        for(uint8_t value : bad_frame) incoming[1].push_back(value);
        advance(200);
    }
    assert_off();
    assert(!uart_switch_online());
    drop[0] = false;
    advance(500);
    assert(uart_switch_online() && uart_switch_state() == 0);

    assert(uart_switch_toggle(0));
    advance(100);
    assert(pin_values[1] == HIGH);
    incoming[0].clear();
    uint8_t stale_ack[SWITCH_FRAME_SIZE];
    switch_encode(stale_ack, SWITCH_ACK, last_set[3], last_set[4]);
    for(uint8_t value : stale_ack) incoming[0].push_back(value);
    clock_ms += 1600;
    uart_switch_loop();
    assert(!uart_switch_online() && uart_switch_state() == 0);
    advance(500);
    assert(uart_switch_online() && uart_switch_state() == 0);
    assert_off();

    pump_receiver = true;
    assert(uart_update_acquire());
    assert(!pressure_streaming);
    assert_off();
    uint8_t image[800];
    for(unsigned i = 0; i < sizeof(image); i++) image[i] = static_cast<uint8_t>(i);
    uint8_t digest[32] = {};
    for(unsigned i = 0; i < sizeof(image); i++) digest[i % 32] ^= image[i];
    drop_first_data_ack = true;
    assert(uart_update_transfer(image, sizeof(image), digest, nullptr));
    assert(!drop_first_data_ack && selected && restarted);
    assert(ota_bytes.size() == sizeof(image));
    assert(std::memcmp(ota_bytes.data(), image, sizeof(image)) == 0);
    uart_update_release();
    pump_receiver = false;

    advance(500);
    assert(uart_switch_online());
    assert(pressure_streaming && uart_switch_take_pressure(&pressure_bar));
    pump_receiver = true;
    selected = false;
    restarted = false;
    assert(uart_update_acquire());
    digest[0] ^= 1;
    assert(!uart_update_transfer(image, sizeof(image), digest, nullptr));
    assert(!selected && !restarted);
    assert_off();
    uart_update_release();
    pump_receiver = false;

    advance(500);
    assert(uart_switch_online());
    assert(uart_switch_toggle(0));
    advance(100);
    assert(pin_values[1] == HIGH);
    uint8_t metadata[36] = {};
    update_put_u32(metadata, sizeof(image));
    uint8_t update_frame[UPDATE_FRAME_MAX];
    size_t frame_size = update_encode(update_frame, UPDATE_BEGIN, 0, metadata, sizeof(metadata));
    update_frame[frame_size - 1] ^= 1;
    for(size_t i = 0; i < frame_size; i++) incoming[1].push_back(update_frame[i]);
    receiver_loop();
    assert(!updating && pin_values[1] == HIGH);
    update_frame[frame_size - 1] ^= 1;
    for(size_t i = 0; i < frame_size; i++) incoming[1].push_back(update_frame[i]);
    receiver_loop();
    assert(updating);
    assert_off();
    clock_ms += 15001;
    receiver_loop();
    assert(!updating);
    assert_off();

    std::puts("UART protocol and failsafe tests passed");
}
