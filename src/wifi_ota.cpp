#include "wifi_ota.h"
#include "device_settings.h"
#include "firmware_version.h"
#include "uart_switch.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include <strings.h>
#include <time.h>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

static constexpr char MANIFEST_URL[] =
    "https://raw.githubusercontent.com/Bowi0/Test_Amoled_175_OTA/main/ota/manifest.json";
static constexpr char FIRMWARE_URL_PREFIX[] =
    "https://github.com/Bowi0/Test_Amoled_175_OTA/releases/";
static constexpr char MAINBOARD_MANIFEST_URL[] =
    "https://raw.githubusercontent.com/Bowi0/ESP32-S3-Zero-KM/main/ota/manifest.json";
static constexpr char MAINBOARD_FIRMWARE_PREFIX[] =
    "https://github.com/Bowi0/ESP32-S3-Zero-KM/releases/";
static constexpr size_t MAINBOARD_MAX_FIRMWARE_SIZE = 0x330000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 20000;
static constexpr time_t MINIMUM_VALID_UNIX_TIME = 1704067200;
static constexpr char LOCAL_TIME_ZONE[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
static constexpr size_t MANIFEST_MAX_SIZE = 2048;
static constexpr unsigned MAX_SAVED_NETWORKS = 10;

struct SavedNetwork {
    char ssid[33];
    char password[64];
};

struct ScannedNetwork {
    char ssid[33];
    int signal_percent;
    bool secured;
};

static ScannedNetwork *scanned_networks;
static int scanned_network_count;

static Preferences preferences;
static bool preferences_ready;
static bool have_credentials;
static bool user_connection_pending;
static bool scan_in_progress;
static bool wifi_was_connected;
static bool connection_started;
static bool time_sync_started;
static bool time_was_synchronized;
static uint32_t connection_started_ms;
static uint32_t scan_started_ms;
static uint32_t last_wifi_update_ms;
static uint32_t time_sync_started_ms;
static uint32_t scan_generation;
static char pending_ssid[33];
static char pending_password[64];
static char wifi_status[64] = "WLAN wird initialisiert...";
static char update_status[96] = "Bereit";
static volatile int download_progress;
static volatile bool ota_busy;
static bool update_check_requested;
static uint32_t update_check_started_ms;
static bool update_available;
static bool update_decision_pending;
static bool update_install_requested;
static bool update_installing;
static bool mainboard_selected;
static bool manually_disconnected;
static bool wifi_initialized;
static bool ota_worker_ready;
static bool credential_save_failed;
static int auto_network_index = -1;
static uint16_t auto_tried;
static char pending_firmware_url[256];
static char pending_firmware_sha256[65];
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static void set_wifi_status(const char *message);

static void network_key(unsigned index, char *key, size_t size)
{
    snprintf(key, size, "net%u", index);
}

static bool read_network(unsigned index, SavedNetwork &network)
{
    if(!preferences_ready || index >= MAX_SAVED_NETWORKS) return false;
    char key[8];
    network_key(index, key, sizeof(key));
    if(!preferences.isKey(key)) return false;
    if(preferences.getBytesLength(key) != sizeof(network) ||
       preferences.getBytes(key, &network, sizeof(network)) != sizeof(network) ||
       network.ssid[0] == '\0' ||
       !memchr(network.ssid, '\0', sizeof(network.ssid)) ||
       !memchr(network.password, '\0', sizeof(network.password))) {
        Serial.printf("WiFi: saved network slot %u is invalid\n", index);
        return false;
    }
    return true;
}

static int find_network(const char *ssid)
{
    if(!ssid || !*ssid) return -1;
    SavedNetwork network = {};
    for(unsigned i = 0; i < MAX_SAVED_NETWORKS; i++) {
        if(read_network(i, network) && strcmp(network.ssid, ssid) == 0) return i;
    }
    return -1;
}

static bool store_network(const char *ssid, const char *password)
{
    if(!preferences_ready) return false;
    int index = find_network(ssid);
    if(index < 0) {
        for(unsigned i = 0; i < MAX_SAVED_NETWORKS; i++) {
            char key[8];
            network_key(i, key, sizeof(key));
            if(!preferences.isKey(key)) { index = i; break; }
        }
    }
    if(index < 0) {
        Serial.println("WiFi: saved network storage is full");
        return false;
    }
    SavedNetwork network = {};
    strlcpy(network.ssid, ssid, sizeof(network.ssid));
    strlcpy(network.password, password, sizeof(network.password));
    char key[8];
    network_key(index, key, sizeof(key));
    if(preferences.putBytes(key, &network, sizeof(network)) != sizeof(network) ||
       preferences.putUChar("last", static_cast<uint8_t>(index)) == 0) {
        Serial.println("WiFi: could not save network");
        return false;
    }
    return true;
}

static bool start_saved_attempt(unsigned index)
{
    SavedNetwork network = {};
    if(!read_network(index, network)) return false;
    auto_network_index = static_cast<int>(index);
    auto_tried |= static_cast<uint16_t>(1U << index);
    strlcpy(pending_ssid, network.ssid, sizeof(pending_ssid));
    strlcpy(pending_password, network.password, sizeof(pending_password));
    connection_started = true;
    connection_started_ms = millis();
    set_wifi_status("Verbinde mit gespeichertem WLAN...");
    WiFi.begin(pending_ssid, pending_password);
    return true;
}

static void *ota_mbedtls_calloc(size_t count, size_t size)
{
    return heap_caps_calloc_prefer(count, size, 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

struct ManifestResponse {
    char data[MANIFEST_MAX_SIZE + 1];
    size_t length;
    bool overflow;
};

struct DownloadContext {
    const esp_partition_t *partition;
    esp_ota_handle_t ota_handle;
    bool ota_started;
    bool failed;
    int64_t content_length;
    int64_t bytes_received;
    mbedtls_sha256_context sha256;
};

static void set_wifi_status(const char *message)
{
    portENTER_CRITICAL(&status_mux);
    strlcpy(wifi_status, message, sizeof(wifi_status));
    portEXIT_CRITICAL(&status_mux);
}

static void set_update_status(const char *message, int progress)
{
    portENTER_CRITICAL(&status_mux);
    strlcpy(update_status, message, sizeof(update_status));
    download_progress = progress;
    portEXIT_CRITICAL(&status_mux);
}

static bool system_time_is_valid(void)
{
    return time(nullptr) >= MINIMUM_VALID_UNIX_TIME;
}

static bool cache_scan_results(int count)
{
    if(count > 256) {
        Serial.printf("WiFi: scan returned %d networks; only 256 are addressable\n", count);
        count = 256;
    }
    ScannedNetwork *results = nullptr;
    if(count > 0) {
        results = static_cast<ScannedNetwork *>(calloc(count, sizeof(ScannedNetwork)));
        if(!results) {
            Serial.println("WiFi: insufficient memory to store scan results");
            return false;
        }
        for(int i = 0; i < count; i++) {
            String ssid;
            uint8_t encryption;
            int32_t rssi;
            uint8_t *bssid;
            int32_t channel;
            if(!WiFi.getNetworkInfo(static_cast<uint8_t>(i), ssid, encryption,
                                    rssi, bssid, channel)) {
                Serial.printf("WiFi: cannot read scan result %d\n", i);
                free(results);
                return false;
            }
            ssid.toCharArray(results[i].ssid, sizeof(results[i].ssid));
            int signal = (rssi + 100) * 2;
            results[i].signal_percent = signal < 0 ? 0 : signal > 100 ? 100 : signal;
            results[i].secured = encryption != WIFI_AUTH_OPEN;
        }
    }
    free(scanned_networks);
    scanned_networks = results;
    scanned_network_count = count;
    return true;
}

extern "C" bool wifi_ota_time_valid(void)
{
    return system_time_is_valid();
}

static esp_err_t manifest_event_handler(esp_http_client_event_t *event)
{
    if(event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 ||
       event->user_data == nullptr) {
        return ESP_OK;
    }

    auto *response = static_cast<ManifestResponse *>(event->user_data);
    const size_t data_size = static_cast<size_t>(event->data_len);
    if(response->length + data_size > MANIFEST_MAX_SIZE) {
        response->overflow = true;
        return ESP_FAIL;
    }

    memcpy(response->data + response->length, event->data, data_size);
    response->length += data_size;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static bool fetch_manifest(ManifestResponse *response, const char *manifest_url)
{
    char url[256];
    snprintf(url, sizeof(url), "%s?t=%lu", manifest_url,
             static_cast<unsigned long>(millis()));

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = manifest_event_handler;
    config.user_data = response;
    config.max_redirection_count = 3;
    config.buffer_size = 2048;
    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(client == nullptr) {
        Serial.println("OTA: failed to initialize manifest HTTP client");
        return false;
    }
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "Pragma", "no-cache");

    const esp_err_t result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if(result != ESP_OK || status_code != 200 || response->overflow ||
       response->length == 0) {
        Serial.printf("OTA: manifest request failed (err=%s, HTTP=%d)\n",
                      esp_err_to_name(result), status_code);
        return false;
    }
    return true;
}

static bool is_valid_sha256(const char *sha256)
{
    if(sha256 == nullptr || strlen(sha256) != 64) return false;
    for(size_t index = 0; index < 64; index++) {
        if(!isxdigit(static_cast<unsigned char>(sha256[index]))) return false;
    }
    return true;
}

static esp_err_t download_event_handler(esp_http_client_event_t *event)
{
    if(event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 ||
       event->user_data == nullptr) {
        return ESP_OK;
    }

    auto *download = static_cast<DownloadContext *>(event->user_data);
    const int status_code = esp_http_client_get_status_code(event->client);
    if(status_code >= 300 && status_code < 400) return ESP_OK;
    if(status_code != 200) {
        download->failed = true;
        return ESP_FAIL;
    }

    if(!download->ota_started) {
        download->content_length = esp_http_client_get_content_length(event->client);
        download->partition = esp_ota_get_next_update_partition(nullptr);
        if(download->partition == nullptr ||
           esp_ota_begin(download->partition, OTA_SIZE_UNKNOWN,
                         &download->ota_handle) != ESP_OK) {
            download->failed = true;
            return ESP_FAIL;
        }
        mbedtls_sha256_init(&download->sha256);
        if(mbedtls_sha256_starts(&download->sha256, 0) != 0) {
            download->failed = true;
            esp_ota_abort(download->ota_handle);
            return ESP_FAIL;
        }
        download->ota_started = true;
    }

    const size_t chunk_size = static_cast<size_t>(event->data_len);
    if(esp_ota_write(download->ota_handle, event->data, chunk_size) != ESP_OK ||
       mbedtls_sha256_update(&download->sha256,
                             static_cast<const unsigned char *>(event->data),
                             chunk_size) != 0) {
        download->failed = true;
        return ESP_FAIL;
    }

    download->bytes_received += event->data_len;
    if(download->content_length > 0) {
        int percent = static_cast<int>(
            (download->bytes_received * 100) / download->content_length);
        if(percent > 100) percent = 100;
        char status[96];
        snprintf(status, sizeof(status), "Update wird geladen: %d%%", percent);
        set_update_status(status, percent);
    }
    return ESP_OK;
}

static bool download_firmware(const char *url, const char *expected_sha256)
{
    if(strncmp(url, FIRMWARE_URL_PREFIX, sizeof(FIRMWARE_URL_PREFIX) - 1) != 0) {
        Serial.println("OTA: rejected firmware URL outside the configured repository");
        set_update_status("Firmware-URL im Manifest ist nicht erlaubt", 0);
        return false;
    }

    DownloadContext download = {};
    set_update_status("Neues Update gefunden - Download startet", 0);
    Serial.printf("OTA: before firmware TLS internal=%u, DMA=%u, largest DMA=%u, PSRAM=%u bytes free\n",
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = download_event_handler;
    config.user_data = &download;
    config.max_redirection_count = 5;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(client == nullptr) {
        Serial.println("OTA: failed to initialize firmware HTTP client");
        set_update_status("Firmware-Download konnte nicht gestartet werden", 0);
        return false;
    }
    esp_http_client_set_header(client, "Accept", "application/octet-stream");

    const esp_err_t result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    bool success = result == ESP_OK && status_code == 200 &&
                   !download.failed && download.ota_started &&
                   download.bytes_received > 0;
    unsigned char digest[32];
    if(success && mbedtls_sha256_finish(&download.sha256, digest) != 0) {
        success = false;
    }
    if(download.ota_started) {
        mbedtls_sha256_free(&download.sha256);
    }

    char digest_hex[65] = {0};
    if(success) {
        for(size_t index = 0; index < sizeof(digest); index++) {
            snprintf(digest_hex + index * 2, 3, "%02x", digest[index]);
        }
        if(strcasecmp(digest_hex, expected_sha256) != 0) {
            Serial.println("OTA: firmware SHA-256 does not match the manifest");
            success = false;
        }
    }

    if(!success) {
        if(download.ota_started) esp_ota_abort(download.ota_handle);
        set_update_status("OTA-Download oder Integritätsprüfung fehlgeschlagen", 0);
        Serial.printf("OTA: firmware download failed (err=%s, HTTP=%d)\n",
                      esp_err_to_name(result), status_code);
        return false;
    }

    if(esp_ota_end(download.ota_handle) != ESP_OK ||
       esp_ota_set_boot_partition(download.partition) != ESP_OK) {
        set_update_status("OTA-Partition konnte nicht aktiviert werden", 0);
        Serial.println("OTA: failed to finalize or select the new app partition");
        return false;
    }

    set_update_status("Update installiert - Neustart...", 100);
    Serial.println("OTA: update verified; restarting into the new firmware");
    delay(500);
    esp_restart();
    return true;
}

static void check_for_update(bool mainboard)
{
    set_update_status("Prüfe auf Firmware-Update...", 0);
    ManifestResponse response = {};
    if(!fetch_manifest(&response, mainboard ? MAINBOARD_MANIFEST_URL : MANIFEST_URL)) {
        set_update_status("Update-Prüfung fehlgeschlagen: Internet prüfen", 0);
        return;
    }

    JsonDocument document;
    const DeserializationError parse_error =
        deserializeJson(document, response.data, response.length);
    if(parse_error) {
        Serial.printf("OTA: invalid manifest JSON: %s\n", parse_error.c_str());
        set_update_status("Update-Manifest ist ungültig", 0);
        return;
    }

    const char *version = document["version"];
    const char *url = document["url"];
    const char *sha256 = document["sha256"];
    if(version == nullptr || url == nullptr || !is_valid_sha256(sha256)) {
        set_update_status("Update-Manifest ist unvollständig", 0);
        return;
    }
    const char *allowed_prefix = mainboard ? MAINBOARD_FIRMWARE_PREFIX : FIRMWARE_URL_PREFIX;
    if(strncmp(url, allowed_prefix, strlen(allowed_prefix)) != 0) {
        Serial.println("OTA: rejected firmware URL outside the configured repository");
        set_update_status("Firmware-URL im Manifest ist nicht erlaubt", 0);
        return;
    }
    if(mainboard) {
        const char *target = document["target"];
        if(!target || strcmp(target, "ESP32-S3-Zero-KM") != 0) {
            set_update_status("Mainboard-Manifest hat ein falsches Ziel", 0);
            return;
        }
    }

    uint32_t remote_version[3];
    uint32_t current_version[3];
    char mainboard_version[16];
    bool version_known = !mainboard ||
                         uart_switch_version(mainboard_version, sizeof(mainboard_version));
    if(!parse_firmware_version(version, remote_version) ||
       (version_known && !parse_firmware_version(mainboard ? mainboard_version : APP_VERSION,
                                        current_version))) {
        set_update_status("Versionsangabe im Update-Manifest ist ungültig", 0);
        return;
    }

    if(version_known && !is_newer_firmware_version(remote_version, current_version)) {
        set_update_status("Software ist auf dem neuesten Stand.", 0);
        return;
    }

    char message[96];
    if(strlcpy(pending_firmware_url, url, sizeof(pending_firmware_url)) >=
           sizeof(pending_firmware_url) ||
       strlcpy(pending_firmware_sha256, sha256,
               sizeof(pending_firmware_sha256)) >=
           sizeof(pending_firmware_sha256)) {
        set_update_status("Update-Information ist zu lang", 0);
        return;
    }

    snprintf(message, sizeof(message),
             mainboard ? "Mainboard %s installieren? Ausgänge werden ausgeschaltet."
                       : "Version %s verfügbar. Möchtest du sie installieren?", version);
    portENTER_CRITICAL(&status_mux);
    update_available = true;
    update_decision_pending = false;
    strlcpy(update_status, message, sizeof(update_status));
    download_progress = 0;
    portEXIT_CRITICAL(&status_mux);
}

struct MainboardDownload {
    uint8_t *image;
    size_t size;
    size_t capacity;
    int64_t content_length;
    bool failed;
};

static esp_err_t mainboard_download_event(esp_http_client_event_t *event)
{
    if(event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 ||
       !event->user_data) return ESP_OK;
    MainboardDownload *download = static_cast<MainboardDownload *>(event->user_data);
    int status = esp_http_client_get_status_code(event->client);
    if(status >= 300 && status < 400) return ESP_OK;
    if(status != 200) { download->failed = true; return ESP_FAIL; }
    if(!download->image) {
        int64_t content_length = esp_http_client_get_content_length(event->client);
        download->content_length = content_length;
        if(content_length > static_cast<int64_t>(MAINBOARD_MAX_FIRMWARE_SIZE)) {
            download->failed = true;
            return ESP_FAIL;
        }
        download->capacity = content_length > 0
                                 ? static_cast<size_t>(content_length)
                                 : MAINBOARD_MAX_FIRMWARE_SIZE;
        download->image = static_cast<uint8_t *>(
            heap_caps_malloc(download->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if(!download->image) { download->failed = true; return ESP_FAIL; }
    }
    size_t chunk = static_cast<size_t>(event->data_len);
    if(chunk > download->capacity - download->size) {
        download->failed = true;
        return ESP_FAIL;
    }
    memcpy(download->image + download->size, event->data, chunk);
    download->size += chunk;
    return ESP_OK;
}

static void mainboard_progress(uint32_t done, uint32_t total)
{
    int percent = static_cast<int>((static_cast<uint64_t>(done) * 100) / total);
    char status[96];
    snprintf(status, sizeof(status), "Mainboard wird übertragen: %d%%", percent);
    set_update_status(status, percent);
}

static bool download_mainboard(const char *url, const char *expected_sha256)
{
    if(strncmp(url, MAINBOARD_FIRMWARE_PREFIX,
               sizeof(MAINBOARD_FIRMWARE_PREFIX) - 1) != 0) {
        set_update_status("Mainboard-Firmware-URL nicht erlaubt", 0);
        Serial.println("Mainboard OTA: rejected firmware URL");
        return false;
    }
    set_update_status("Mainboard-Ausgänge werden ausgeschaltet...", 0);
    if(!uart_update_acquire()) {
        set_update_status("Mainboard nicht erreichbar; Update abgebrochen", 0);
        return false;
    }
    set_update_status("Mainboard-Firmware wird geladen...", 0);
    MainboardDownload download = {};
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = mainboard_download_event;
    config.user_data = &download;
    config.max_redirection_count = 5;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t result = ESP_FAIL;
    int status = 0;
    if(client) {
        esp_http_client_set_header(client, "Accept", "application/octet-stream");
        result = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
    }
    uint8_t digest[32];
    bool ok = client && result == ESP_OK && status == 200 &&
              !download.failed && download.size > 0 &&
              (download.content_length <= 0 ||
               download.size == static_cast<size_t>(download.content_length)) &&
              mbedtls_sha256(download.image, download.size, digest, 0) == 0;
    char digest_hex[65] = {};
    if(ok) {
        for(size_t i = 0; i < sizeof(digest); i++)
            snprintf(digest_hex + i * 2, 3, "%02x", digest[i]);
        ok = strcasecmp(digest_hex, expected_sha256) == 0;
    }
    if(!ok) {
        Serial.printf("Mainboard OTA: download/hash failed (err=%s, HTTP=%d)\n",
                      esp_err_to_name(result), status);
        set_update_status("Mainboard-Download oder SHA-256-Prüfung fehlgeschlagen", 0);
    } else {
        set_update_status("Übertragung zum Mainboard startet...", 0);
        ok = uart_update_transfer(download.image, static_cast<uint32_t>(download.size),
                                  digest, mainboard_progress);
        if(ok) set_update_status("Mainboard-Update übertragen; Neustart läuft", 100);
        else set_update_status("Mainboard-Übertragung fehlgeschlagen; Status prüfen", 0);
    }
    if(download.image) heap_caps_free(download.image);
    uart_update_release();
    if(ok) {
        const uint32_t started = millis();
        while(!uart_switch_online() && millis() - started < 10000) delay(50);
        if(uart_switch_online())
            set_update_status("Übertragen; Mainboard wieder verbunden", 100);
        else
            set_update_status("Übertragen; Neustart konnte nicht bestätigt werden", 100);
    }
    return ok;
}

static void ota_worker(void *)
{
    for(;;) {
        portENTER_CRITICAL(&status_mux);
        const bool decision_pending = update_decision_pending;
        const bool install_requested = update_install_requested;
        const bool mainboard = mainboard_selected;
        char firmware_url[sizeof(pending_firmware_url)];
        char firmware_sha256[sizeof(pending_firmware_sha256)];
        if(decision_pending) {
            update_decision_pending = false;
            update_install_requested = false;
            ota_busy = true;
            strlcpy(firmware_url, pending_firmware_url, sizeof(firmware_url));
            strlcpy(firmware_sha256, pending_firmware_sha256,
                    sizeof(firmware_sha256));
        }
        const bool check_requested = update_check_requested;
        portEXIT_CRITICAL(&status_mux);

        if(decision_pending) {
            if(!install_requested) {
                set_update_status("Update nicht installiert.", 0);
            } else if(WiFi.status() != WL_CONNECTED) {
                set_update_status("WLAN-Verbindung unterbrochen.", 0);
            } else {
                if(mainboard) download_mainboard(firmware_url, firmware_sha256);
                else download_firmware(firmware_url, firmware_sha256);
            }
            portENTER_CRITICAL(&status_mux);
            ota_busy = false;
            update_installing = false;
            portEXIT_CRITICAL(&status_mux);
            continue;
        }

        if(check_requested && WiFi.status() != WL_CONNECTED) {
            portENTER_CRITICAL(&status_mux);
            update_check_requested = false;
            portEXIT_CRITICAL(&status_mux);
            set_update_status("WLAN-Verbindung unterbrochen", 0);
        } else if(check_requested && WiFi.status() == WL_CONNECTED) {
            if(!system_time_is_valid()) {
                if(millis() - update_check_started_ms >= 60000) {
                    portENTER_CRITICAL(&status_mux);
                    update_check_requested = false;
                    portEXIT_CRITICAL(&status_mux);
                    set_update_status("Zeitsynchronisierung fehlgeschlagen: Internet prüfen", 0);
                    continue;
                }
                set_update_status("Warte auf sichere Uhrzeitsynchronisierung...", 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            portENTER_CRITICAL(&status_mux);
            const bool manual_check = update_check_requested;
            update_check_requested = false;
            ota_busy = manual_check;
            portEXIT_CRITICAL(&status_mux);
            if(manual_check) {
                check_for_update(mainboard);
                portENTER_CRITICAL(&status_mux);
                ota_busy = false;
                portEXIT_CRITICAL(&status_mux);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

extern "C" void wifi_ota_init(void)
{
    if(wifi_initialized) return;
    const int allocator_result =
        mbedtls_platform_set_calloc_free(ota_mbedtls_calloc, heap_caps_free);
    if(allocator_result != 0) {
        Serial.printf("OTA: failed to configure TLS memory allocator (%d)\n",
                      allocator_result);
        set_update_status("TLS-Speicher konnte nicht eingerichtet werden", 0);
        return;
    }

    Serial.printf("OTA: TLS heap internal=%u, PSRAM=%u bytes free\n",
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));

    if(!WiFi.mode(WIFI_STA)) {
        Serial.println("WiFi: failed to enable station mode");
        set_wifi_status("WLAN konnte nicht aktiviert werden");
        return;
    }
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    preferences_ready = preferences.begin("wifi", false);
    if(!preferences_ready) {
        Serial.println("WiFi: failed to open the credential store");
        set_wifi_status("Fehler beim Öffnen des WLAN-Speichers");
    }

    if(preferences_ready && preferences.isKey("ssid")) {
        const String ssid = preferences.getString("ssid", "");
        const String password = preferences.getString("password", "");
        if(!ssid.isEmpty() && find_network(ssid.c_str()) < 0 &&
           !store_network(ssid.c_str(), password.c_str())) {
            Serial.println("WiFi: legacy credential migration failed");
            set_wifi_status("Gespeichertes WLAN konnte nicht übernommen werden");
        }
    }
    SavedNetwork network = {};
    for(unsigned i = 0; i < MAX_SAVED_NETWORKS; i++) {
        if(read_network(i, network)) { have_credentials = true; break; }
    }

    if(!have_credentials) {
        strlcpy(wifi_status, "WLAN-Verbindung startet auf Screen4", sizeof(wifi_status));
    } else {
        strlcpy(wifi_status, "Gespeichertes WLAN bereit — Verbindung auf Screen4",
                sizeof(wifi_status));
    }

    const BaseType_t task_result =
        xTaskCreatePinnedToCore(ota_worker, "ota_worker", 16384, nullptr, 1, nullptr, 0);
    if(task_result != pdPASS) {
        Serial.println("OTA: failed to create update-check task");
        set_update_status("OTA-Prüfaufgabe konnte nicht gestartet werden", 0);
    } else ota_worker_ready = true;
    wifi_initialized = true;
}

extern "C" void wifi_ota_autoconnect(void)
{
    if(!wifi_initialized || !device_wifi_enabled() || !have_credentials ||
       manually_disconnected || connection_started || WiFi.status() == WL_CONNECTED) return;
    auto_tried = 0;
    credential_save_failed = false;
    const unsigned last = preferences.getUChar("last", 0);
    if(last < MAX_SAVED_NETWORKS && start_saved_attempt(last)) return;
    for(unsigned i = 0; i < MAX_SAVED_NETWORKS; i++)
        if(start_saved_attempt(i)) return;
    set_wifi_status("Gespeicherte WLAN-Daten nicht lesbar");
    Serial.println("WiFi: no readable saved network");
}

extern "C" bool wifi_ota_set_enabled(bool enabled)
{
    if(enabled == device_wifi_enabled() && (!enabled || wifi_initialized)) return true;
    if(ota_busy) {
        set_wifi_status("WLAN während Update nicht abschaltbar");
        return false;
    }
    if(enabled) {
        wifi_ota_init();
        if(!wifi_initialized || !WiFi.mode(WIFI_STA)) {
            set_wifi_status("WLAN konnte nicht aktiviert werden");
            return false;
        }
        WiFi.setAutoReconnect(true);
        manually_disconnected = false;
        if(!device_wifi_enabled() && !device_set_wifi_enabled(true)) {
            WiFi.mode(WIFI_OFF);
            return false;
        }
        wifi_ota_autoconnect();
    } else {
        if(!device_set_wifi_enabled(false)) {
            set_wifi_status("WLAN-Status konnte nicht gespeichert werden");
            return false;
        }
        WiFi.setAutoReconnect(false);
        if(scan_in_progress) WiFi.scanDelete();
        if(!WiFi.mode(WIFI_OFF)) {
            if(!device_set_wifi_enabled(true))
                Serial.println("WiFi: failed to restore saved state");
            WiFi.setAutoReconnect(true);
            set_wifi_status("WLAN konnte nicht deaktiviert werden");
            return false;
        }
        scan_in_progress = false;
        free(scanned_networks);
        scanned_networks = nullptr;
        scanned_network_count = 0;
        scan_generation++;
        connection_started = false;
        user_connection_pending = false;
        pending_password[0] = '\0';
        auto_network_index = -1;
        time_sync_started = false;
        set_wifi_status("WLAN ausgeschaltet");
    }
    return true;
}

extern "C" void wifi_ota_screen_enter(void)
{
    if(!wifi_initialized || !device_wifi_enabled()) return;
    if(WiFi.status() == WL_CONNECTED || connection_started) return;

    set_wifi_status("Bitte WLAN auswählen und verbinden");
}

extern "C" void wifi_ota_loop(void)
{
    if(!wifi_initialized || !device_wifi_enabled()) return;
    const uint32_t now = millis();
    if(now - last_wifi_update_ms < 250) return;
    last_wifi_update_ms = now;

    const bool connected = WiFi.status() == WL_CONNECTED &&
                           (!connection_started || WiFi.SSID() == pending_ssid);
    if(connected) {
        connection_started = false;
        if(!time_sync_started) {
            configTzTime(LOCAL_TIME_ZONE, "pool.ntp.org", "time.nist.gov", "time.google.com");
            time_sync_started = true;
            time_sync_started_ms = now;
            set_wifi_status("Verbunden — Uhrzeit wird synchronisiert...");
        } else if(!system_time_is_valid() &&
                  now - time_sync_started_ms >= 60000) {
            configTzTime(LOCAL_TIME_ZONE, "pool.ntp.org", "time.nist.gov", "time.google.com");
            time_sync_started_ms = now;
        }

        const bool just_connected_by_user = user_connection_pending;
        if(just_connected_by_user) {
            if(store_network(pending_ssid, pending_password)) {
                have_credentials = true;
            } else {
                Serial.println("WiFi: connected, but credentials could not be saved");
                set_wifi_status("Verbunden, WLAN-Daten konnten nicht gespeichert werden");
                credential_save_failed = true;
            }
            user_connection_pending = false;
            connection_started = false;
            pending_password[0] = '\0';
        }
        if(auto_network_index >= 0) {
            if(preferences.putUChar("last", static_cast<uint8_t>(auto_network_index)) == 0) {
                Serial.println("WiFi: could not save last connected network");
                set_wifi_status("Verbunden, WLAN-Reihenfolge nicht gespeichert");
                credential_save_failed = true;
            }
            auto_network_index = -1;
            pending_password[0] = '\0';
        }

        if((!wifi_was_connected || just_connected_by_user) &&
           !credential_save_failed && system_time_is_valid()) {
            char message[64];
            snprintf(message, sizeof(message), "Verbunden: %s  %s",
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            set_wifi_status(message);
        }
        if(system_time_is_valid() && !time_was_synchronized && !credential_save_failed) {
            char message[64];
            snprintf(message, sizeof(message), "Verbunden: %s  %s",
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            set_wifi_status(message);
            time_was_synchronized = true;
        }
        wifi_was_connected = true;
    } else {
        wifi_was_connected = false;
        if(connection_started && !user_connection_pending &&
           now - connection_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
            connection_started = false;
            pending_password[0] = '\0';
            Serial.println("WiFi: saved credential connection timed out");
            WiFi.disconnect(false, false);
            auto_network_index = -1;
            for(unsigned i = 0; i < MAX_SAVED_NETWORKS; i++) {
                if(!(auto_tried & (1U << i)) && start_saved_attempt(i)) break;
            }
            if(!connection_started)
                set_wifi_status("Keine gespeicherte WLAN-Verbindung möglich");
        }
        if(user_connection_pending &&
           now - connection_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
            user_connection_pending = false;
            connection_started = false;
            pending_password[0] = '\0';
            set_wifi_status("Verbindung fehlgeschlagen — Passwort prüfen");
            Serial.println("WiFi: connection attempt timed out");
        } else if(!user_connection_pending && connection_started && have_credentials &&
                  WiFi.status() == WL_IDLE_STATUS) {
            set_wifi_status("Verbinde mit gespeichertem WLAN...");
        } else if(!have_credentials && !scan_in_progress) {
            set_wifi_status("Nicht verbunden — WLAN auswählen");
        }
    }

    if(scan_in_progress) {
        const int result = WiFi.scanComplete();
        if(result >= 0) {
            scan_in_progress = false;
            const bool cached = cache_scan_results(result);
            WiFi.scanDelete();
            scan_generation++;
            if(!cached) {
                set_wifi_status("WLAN-Suche fehlgeschlagen: Speicherfehler");
            } else if(WiFi.status() != WL_CONNECTED && !user_connection_pending) {
                set_wifi_status("WLAN-Suche abgeschlossen");
            }
        } else if(result == WIFI_SCAN_FAILED) {
            scan_in_progress = false;
            scan_generation++;
            if(WiFi.status() != WL_CONNECTED && !user_connection_pending) {
                set_wifi_status("WLAN-Suche fehlgeschlagen");
            }
        } else if(now - scan_started_ms >= WIFI_SCAN_TIMEOUT_MS) {
            WiFi.scanDelete();
            scan_in_progress = false;
            scan_generation++;
            if(WiFi.status() != WL_CONNECTED && !user_connection_pending) {
                set_wifi_status("WLAN-Suche abgebrochen — erneut versuchen");
            }
            Serial.println("WiFi: scan timed out");
        }
    }
}

extern "C" bool wifi_ota_has_credentials(void)
{
    return have_credentials;
}

extern "C" bool wifi_ota_is_busy(void)
{
    return ota_busy;
}

extern "C" void wifi_ota_start_scan(void)
{
    if(!wifi_initialized || !device_wifi_enabled()) return;
    if(scan_in_progress || ota_busy || connection_started ||
       user_connection_pending) {
        return;
    }
    const int result = WiFi.scanNetworks(true, false);
    if(result == WIFI_SCAN_FAILED) {
        scan_in_progress = false;
        scan_generation++;
        set_wifi_status("WLAN-Suche konnte nicht gestartet werden");
        Serial.println("WiFi: scan could not be started");
        return;
    }
    scan_in_progress = result == WIFI_SCAN_RUNNING;
    scan_started_ms = millis();
    if(scan_in_progress) {
        set_wifi_status("Suche nach WLANs...");
    } else {
        if(!cache_scan_results(result)) {
            WiFi.scanDelete();
            scan_generation++;
            set_wifi_status("WLAN-Suche fehlgeschlagen: Speicherfehler");
            return;
        }
        WiFi.scanDelete();
        scan_generation++;
        set_wifi_status("WLAN-Suche abgeschlossen");
    }
}

extern "C" int wifi_ota_network_count(void)
{
    if(!wifi_initialized || !device_wifi_enabled()) return 0;
    return scanned_network_count;
}

extern "C" bool wifi_ota_get_network(int index, char *ssid, size_t ssid_size,
                                     int *signal_percent, bool *secured)
{
    if(ssid == nullptr || ssid_size == 0 || index < 0 ||
       index >= wifi_ota_network_count()) {
        return false;
    }

    strlcpy(ssid, scanned_networks[index].ssid, ssid_size);
    if(signal_percent != nullptr) *signal_percent = scanned_networks[index].signal_percent;
    if(secured != nullptr) *secured = scanned_networks[index].secured;
    return ssid[0] != '\0';
}

extern "C" bool wifi_ota_connect(const char *ssid, const char *password)
{
    if(!wifi_initialized || !device_wifi_enabled() ||
       ssid == nullptr || *ssid == '\0' || ota_busy) return false;

    if(!WiFi.setAutoReconnect(true)) {
        Serial.println("WiFi: failed to enable automatic reconnection");
        set_wifi_status("WLAN-Verbindung konnte nicht gestartet werden");
        return false;
    }
    manually_disconnected = false;
    credential_save_failed = false;
    auto_network_index = -1;
    if(scan_in_progress) {
        WiFi.scanDelete();
        scan_in_progress = false;
        scan_generation++;
    }
    strlcpy(pending_ssid, ssid, sizeof(pending_ssid));
    strlcpy(pending_password, password == nullptr ? "" : password,
            sizeof(pending_password));
    WiFi.disconnect(false, false);
    WiFi.begin(pending_ssid, pending_password);
    user_connection_pending = true;
    connection_started = true;
    connection_started_ms = millis();
    set_wifi_status("Verbindung wird hergestellt...");
    return true;
}

extern "C" bool wifi_ota_has_saved_network(const char *ssid)
{
    return find_network(ssid) >= 0;
}

extern "C" bool wifi_ota_connect_saved(const char *ssid)
{
    const int index = find_network(ssid);
    SavedNetwork network = {};
    if(index < 0 || !read_network(static_cast<unsigned>(index), network)) return false;
    return wifi_ota_connect(network.ssid, network.password);
}

extern "C" bool wifi_ota_disconnect(void)
{
    if(ota_busy || WiFi.status() != WL_CONNECTED) {
        set_wifi_status("WLAN-Verbindung konnte nicht getrennt werden");
        return false;
    }

    if(!WiFi.setAutoReconnect(false)) {
        Serial.println("WiFi: failed to disable automatic reconnection");
        set_wifi_status("WLAN-Verbindung konnte nicht getrennt werden");
        return false;
    }
    if(!WiFi.disconnect(false, false)) {
        WiFi.setAutoReconnect(true);
        Serial.println("WiFi: disconnect failed");
        set_wifi_status("WLAN-Verbindung konnte nicht getrennt werden");
        return false;
    }

    manually_disconnected = true;
    connection_started = false;
    user_connection_pending = false;
    set_wifi_status("WLAN-Verbindung getrennt");
    return true;
}

static bool request_update_check(bool mainboard)
{
    if(!ota_worker_ready) {
        set_update_status("OTA-Prüfaufgabe ist nicht verfügbar", 0);
        return false;
    }
    if(WiFi.status() != WL_CONNECTED) {
        set_update_status("Bitte zuerst WLAN verbinden", 0);
        return false;
    }
    portENTER_CRITICAL(&status_mux);
    if(ota_busy) {
        portEXIT_CRITICAL(&status_mux);
        set_update_status("Ein Update läuft bereits", download_progress);
        return false;
    }
    if(update_decision_pending || update_check_requested || update_available) {
        portEXIT_CRITICAL(&status_mux);
        set_update_status("Update-Auswahl wird bearbeitet", download_progress);
        return false;
    }
    update_check_requested = true;
    update_check_started_ms = millis();
    mainboard_selected = mainboard;
    update_available = false;
    strlcpy(update_status, "Update-Prüfung wird gestartet...", sizeof(update_status));
    download_progress = 0;
    portEXIT_CRITICAL(&status_mux);
    return true;
}

extern "C" bool wifi_ota_request_update_check(void)
{
    return request_update_check(false);
}

extern "C" bool wifi_ota_request_mainboard_update_check(void)
{
    return request_update_check(true);
}

extern "C" bool wifi_ota_confirm_update(bool install)
{
    portENTER_CRITICAL(&status_mux);
    if(!update_available || update_decision_pending) {
        portEXIT_CRITICAL(&status_mux);
        return false;
    }

    update_available = false;
    update_decision_pending = true;
    update_install_requested = install;
    update_installing = install;
    strlcpy(update_status,
            install ? "Update wird vorbereitet..." : "Update nicht installiert.",
            sizeof(update_status));
    portEXIT_CRITICAL(&status_mux);
    return true;
}

extern "C" void wifi_ota_get_snapshot(wifi_ota_snapshot_t *snapshot)
{
    if(snapshot == nullptr) return;

    snapshot->connected = wifi_initialized && device_wifi_enabled() &&
                          WiFi.status() == WL_CONNECTED;
    snapshot->connecting = connection_started || user_connection_pending;
    snapshot->scanning = scan_in_progress;
    snapshot->credential_save_failed = credential_save_failed;
    snapshot->progress = download_progress;
    const int rssi = snapshot->connected ? WiFi.RSSI() : -100;
    snapshot->signal_percent = (rssi + 100) * 2;
    if(snapshot->signal_percent < 0) snapshot->signal_percent = 0;
    if(snapshot->signal_percent > 100) snapshot->signal_percent = 100;
    snapshot->scan_generation = scan_generation;
    strlcpy(snapshot->ssid, WiFi.SSID().c_str(), sizeof(snapshot->ssid));
    strlcpy(snapshot->ip, WiFi.localIP().toString().c_str(), sizeof(snapshot->ip));

    portENTER_CRITICAL(&status_mux);
    snapshot->updating = ota_busy;
    snapshot->installing = update_installing;
    snapshot->update_available = update_available;
    snapshot->update_checking = update_check_requested || update_decision_pending;
    strlcpy(snapshot->wifi_status, wifi_status, sizeof(snapshot->wifi_status));
    strlcpy(snapshot->update_status, update_status, sizeof(snapshot->update_status));
    portEXIT_CRITICAL(&status_mux);
}
