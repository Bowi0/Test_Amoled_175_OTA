#include "uart_switch.h"
#include "uart_switch_protocol.h"
#include "uart_update_protocol.h"
#include <Arduino.h>
#include <freertos/semphr.h>
#include <stdio.h>

static HardwareSerial uart_link(1);
static switch_parser_t parser = {};
static uint8_t requested;
static uint8_t confirmed;
static uint8_t sequence;
static uint8_t sent_sequence;
static uint32_t last_sent;
static uint32_t last_ack;
static bool online;
static bool awaiting_ack;
static SemaphoreHandle_t uart_mutex;
static uint8_t version_parts[3];
static uint8_t version_pending_parts[3];
static uint8_t version_received;
static uint8_t version_sequence;
static bool version_waiting;
static bool version_valid;
static uint32_t version_last_request;
static uint32_t version_last_received;
static uint16_t pressure_centibar;
static bool pressure_pending;
static uart_measurements_t measurements;
static bool steam_pressure_pending;

extern "C" void uart_switch_init(void)
{
    uart_link.begin(115200, SERIAL_8N1, 44, 43);
    uart_mutex = xSemaphoreCreateMutex();
    if(!uart_mutex) Serial.println("UART: failed to create mutex");
    last_sent = millis() - 250;
}

extern "C" void uart_switch_loop(void)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return;
    const uint32_t now = millis();
    if(online && now - last_ack >= 1500) {
        Serial.println("UART: mainboard connection lost");
        online = false;
        requested = 0;
        confirmed = 0;
        awaiting_ack = false;
        version_valid = false;
        version_waiting = false;
        pressure_pending = false;
        measurements = {};
        steam_pressure_pending = false;
        last_sent = now - 250;
    }
    while(uart_link.available()) {
        if(!switch_parse(&parser, (uint8_t)uart_link.read())) continue;
        if(awaiting_ack && parser.data[2] == SWITCH_ACK &&
           parser.data[3] == sent_sequence && parser.data[4] == requested) {
            if(!online) Serial.println("UART: mainboard connected");
            confirmed = parser.data[4];
            last_ack = now;
            online = true;
        } else if(online && parser.data[2] == SWITCH_PRESSURE) {
            const uint16_t value = ((uint16_t)parser.data[3] << 8) | parser.data[4];
            if(value <= SWITCH_PRESSURE_MAX_CENTIBAR) {
                pressure_centibar = value;
                pressure_pending = true;
            }
        } else if(online && parser.data[2] >= SWITCH_STEAM_PRESSURE &&
                  parser.data[2] <= SWITCH_BREWHEAD_TEMPERATURE) {
            const uint16_t raw = ((uint16_t)parser.data[3] << 8) | parser.data[4];
            if(parser.data[2] == SWITCH_STEAM_PRESSURE) {
                measurements.steam_pressure_bar = raw / 100.0f;
                measurements.steam_pressure_valid = true;
                steam_pressure_pending = true;
            } else {
                const int32_t signed_raw = raw < 0x8000 ? raw : (int32_t)raw - 0x10000;
                if(parser.data[2] == SWITCH_BREW_TEMPERATURE) {
                    measurements.brew_temperature_c = signed_raw / 100.0f;
                    measurements.brew_temperature_valid = true;
                } else {
                    measurements.brewhead_temperature_c = signed_raw / 100.0f;
                    measurements.brewhead_temperature_valid = true;
                }
            }
        } else if(version_waiting && parser.data[3] == version_sequence &&
                  parser.data[2] >= SWITCH_VERSION_MAJOR &&
                  parser.data[2] <= SWITCH_VERSION_PATCH) {
            const unsigned index = parser.data[2] - SWITCH_VERSION_MAJOR;
            version_pending_parts[index] = parser.data[4];
            version_received |= 1U << index;
            if(version_received == 7) {
                for(unsigned i = 0; i < 3; i++)
                    version_parts[i] = version_pending_parts[i];
                version_last_received = now;
                version_valid = true;
                version_waiting = false;
            }
        }
    }
    if(now - last_sent >= 250) {
        uint8_t frame[SWITCH_FRAME_SIZE];
        sent_sequence = ++sequence;
        switch_encode(frame, SWITCH_SET, sent_sequence, requested);
        uart_link.write(frame, sizeof(frame));
        switch_encode(frame, SWITCH_PRESSURE_STREAM, 0, 1);
        uart_link.write(frame, sizeof(frame));
        awaiting_ack = true;
        last_sent = now;
    }
    if(online && (!version_valid || now - version_last_request >= 5000) &&
       now - version_last_request >= 1000) {
        uint8_t frame[SWITCH_FRAME_SIZE];
        version_sequence++;
        version_received = 0;
        version_waiting = true;
        version_last_request = now;
        switch_encode(frame, SWITCH_VERSION_REQUEST, version_sequence, 0);
        uart_link.write(frame, sizeof(frame));
    }
    xSemaphoreGive(uart_mutex);
}

extern "C" bool uart_switch_online(void)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return false;
    bool result = online;
    xSemaphoreGive(uart_mutex);
    return result;
}
extern "C" uint8_t uart_switch_state(void)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return 0;
    uint8_t result = confirmed;
    xSemaphoreGive(uart_mutex);
    return result;
}
extern "C" bool uart_switch_pending(void)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return false;
    bool result = requested != confirmed;
    xSemaphoreGive(uart_mutex);
    return result;
}
extern "C" bool uart_switch_version(char *version, size_t size)
{
    if(!version || !size || !uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE)
        return false;
    bool valid = online && version_valid && millis() - version_last_received < 6500;
    if(valid) snprintf(version, size, "v%u.%u.%u", version_parts[0],
                       version_parts[1], version_parts[2]);
    xSemaphoreGive(uart_mutex);
    return valid;
}
extern "C" bool uart_switch_take_pressure(float *pressure_bar)
{
    if(!pressure_bar || !uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE)
        return false;
    const bool available = online && pressure_pending;
    if(available) {
        *pressure_bar = pressure_centibar / 100.0f;
        pressure_pending = false;
    }
    xSemaphoreGive(uart_mutex);
    return available;
}
extern "C" bool uart_switch_take_steam_pressure(float *pressure_bar)
{
    if(!pressure_bar || !uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE)
        return false;
    const bool available = online && steam_pressure_pending;
    if(available) {
        *pressure_bar = measurements.steam_pressure_bar;
        steam_pressure_pending = false;
    }
    xSemaphoreGive(uart_mutex);
    return available;
}
extern "C" bool uart_switch_measurements(uart_measurements_t *result)
{
    if(!result || !uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE)
        return false;
    *result = measurements;
    xSemaphoreGive(uart_mutex);
    return true;
}
extern "C" bool uart_switch_toggle(unsigned channel)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return false;
    const bool allowed = online && requested == confirmed && channel < 4;
    if(allowed) {
        requested ^= (uint8_t)(1U << channel);
        last_sent = millis() - 250;
    }
    xSemaphoreGive(uart_mutex);
    return allowed;
}

extern "C" bool uart_update_acquire(void)
{
    if(!uart_mutex || xSemaphoreTake(uart_mutex, 0) != pdTRUE) return false;
    requested = 0;
    confirmed = 0;
    online = false;
    version_valid = false;
    version_waiting = false;
    awaiting_ack = false;
    pressure_pending = false;
    measurements = {};
    steam_pressure_pending = false;
    parser = {};
    while(uart_link.available()) uart_link.read();
    uint8_t frame[SWITCH_FRAME_SIZE];
    switch_encode(frame, SWITCH_PRESSURE_STREAM, 0, 0);
    uart_link.write(frame, sizeof(frame));
    const uint8_t seq = ++sequence;
    switch_encode(frame, SWITCH_SET, seq, 0);
    for(unsigned attempt = 0; attempt < 3; attempt++) {
        uart_link.write(frame, sizeof(frame));
        const uint32_t start = millis();
        while(millis() - start < 600) {
            while(uart_link.available()) {
                if(switch_parse(&parser, (uint8_t)uart_link.read()) &&
                   parser.data[2] == SWITCH_ACK && parser.data[3] == seq &&
                   parser.data[4] == 0) return true;
            }
            delay(1);
        }
    }
    Serial.println("Mainboard OTA: no acknowledgement for all-off command");
    uart_update_release();
    return false;
}

static bool update_exchange(uint8_t type, uint32_t offset,
                            const uint8_t *data, uint16_t length,
                            uint32_t timeout)
{
    uint8_t frame[UPDATE_FRAME_MAX];
    const size_t size = update_encode(frame, type, offset, data, length);
    if(!size) return false;
    update_parser_t response = {};
    for(unsigned attempt = 0; attempt < 3; attempt++) {
        uart_link.write(frame, size);
        const uint32_t start = millis();
        while(millis() - start < timeout) {
            while(uart_link.available()) {
                if(update_parse(&response, (uint8_t)uart_link.read()) &&
                   response.bytes[2] == UPDATE_REPLY &&
                   update_get_u32(response.bytes + 3) == offset &&
                   response.bytes[7] == 0 && response.bytes[8] == 2 &&
                   response.bytes[9] == type) {
                    if(response.bytes[10] == 0) return true;
                    Serial.printf("Mainboard OTA: command %u rejected at %lu\n",
                                  type, (unsigned long)offset);
                    return false;
                }
            }
            delay(1);
        }
        response.used = 0;
    }
    Serial.printf("Mainboard OTA: no reply for command %u at %lu\n",
                  type, (unsigned long)offset);
    return false;
}

extern "C" bool uart_update_transfer(const uint8_t *image, uint32_t size,
                                       const uint8_t digest[32],
                                       void (*progress)(uint32_t, uint32_t))
{
    uint8_t metadata[36];
    update_put_u32(metadata, size);
    memcpy(metadata + 4, digest, 32);
    bool ok = update_exchange(UPDATE_BEGIN, 0, metadata, sizeof(metadata), 10000);
    for(uint32_t offset = 0; ok && offset < size;) {
        uint16_t length = size - offset > UPDATE_MAX_PAYLOAD
                              ? UPDATE_MAX_PAYLOAD : (uint16_t)(size - offset);
        ok = update_exchange(UPDATE_DATA, offset, image + offset, length, 1500);
        if(ok) {
            offset += length;
            if(progress) progress(offset, size);
        }
    }
    if(ok) ok = update_exchange(UPDATE_FINISH, size, nullptr, 0, 10000);
    if(!ok) update_exchange(UPDATE_ABORT, 0, nullptr, 0, 1000);
    return ok;
}

extern "C" void uart_update_release(void)
{
    parser = {};
    while(uart_link.available()) uart_link.read();
    last_sent = millis() - 250;
    xSemaphoreGive(uart_mutex);
}
