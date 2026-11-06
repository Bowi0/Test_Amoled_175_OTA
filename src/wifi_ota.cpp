#include "wifi_ota.h"

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
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr time_t MINIMUM_VALID_UNIX_TIME = 1704067200;
static constexpr size_t MANIFEST_MAX_SIZE = 2048;

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
static bool update_available;
static bool update_decision_pending;
static bool update_install_requested;
static bool saved_connection_pending;
static bool scan_results_ready_for_display;
static char pending_firmware_url[256];
static char pending_firmware_sha256[65];
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;

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

static bool fetch_manifest(ManifestResponse *response)
{
    char url[sizeof(MANIFEST_URL) + 32];
    snprintf(url, sizeof(url), "%s?t=%lu", MANIFEST_URL,
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

static bool parse_version(const char *version, uint32_t parts[3])
{
    if(version == nullptr || *version == '\0') return false;
    if(*version == 'v' || *version == 'V') version++;

    for(size_t index = 0; index < 3; index++) {
        if(*version < '0' || *version > '9') return false;
        char *end = nullptr;
        const unsigned long value = strtoul(version, &end, 10);
        if(end == version || value > UINT32_MAX) return false;
        parts[index] = static_cast<uint32_t>(value);
        version = end;
        if(index < 2) {
            if(*version != '.') return false;
            version++;
        }
    }
    return *version == '\0';
}

static bool is_newer_version(const uint32_t available[3],
                             const uint32_t current[3])
{
    for(size_t index = 0; index < 3; index++) {
        if(available[index] != current[index]) {
            return available[index] > current[index];
        }
    }
    return false;
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

    ota_busy = true;
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
        ota_busy = false;
        return false;
    }

    if(esp_ota_end(download.ota_handle) != ESP_OK ||
       esp_ota_set_boot_partition(download.partition) != ESP_OK) {
        set_update_status("OTA-Partition konnte nicht aktiviert werden", 0);
        Serial.println("OTA: failed to finalize or select the new app partition");
        ota_busy = false;
        return false;
    }

    set_update_status("Update installiert - Neustart...", 100);
    Serial.println("OTA: update verified; restarting into the new firmware");
    delay(500);
    esp_restart();
    return true;
}

static void check_for_update()
{
    set_update_status("Prüfe auf Firmware-Update...", 0);
    ManifestResponse response = {};
    if(!fetch_manifest(&response)) {
        set_update_status("Update-Prüfung fehlgeschlagen; neuer Versuch folgt", 0);
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

    uint32_t remote_version[3];
    uint32_t current_version[3];
    if(!parse_version(version, remote_version) ||
       !parse_version(APP_VERSION, current_version)) {
        set_update_status("Versionsangabe im Update-Manifest ist ungültig", 0);
        return;
    }

    if(!is_newer_version(remote_version, current_version)) {
        set_update_status("Du bist auf dem neuesten Stand.", 0);
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
             "Version %s verfügbar. Möchtest du sie installieren?", version);
    portENTER_CRITICAL(&status_mux);
    update_available = true;
    update_decision_pending = false;
    strlcpy(update_status, message, sizeof(update_status));
    download_progress = 0;
    portEXIT_CRITICAL(&status_mux);
}

static void ota_worker(void *)
{
    for(;;) {
        portENTER_CRITICAL(&status_mux);
        const bool decision_pending = update_decision_pending;
        const bool install_requested = update_install_requested;
        char firmware_url[sizeof(pending_firmware_url)];
        char firmware_sha256[sizeof(pending_firmware_sha256)];
        if(decision_pending) {
            update_decision_pending = false;
            update_install_requested = false;
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
                download_firmware(firmware_url, firmware_sha256);
            }
            continue;
        }

        if(check_requested && WiFi.status() == WL_CONNECTED) {
            if(!system_time_is_valid()) {
                set_update_status("Warte auf sichere Uhrzeitsynchronisierung...", 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            portENTER_CRITICAL(&status_mux);
            const bool manual_check = update_check_requested;
            update_check_requested = false;
            portEXIT_CRITICAL(&status_mux);
            if(manual_check) {
                check_for_update();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

extern "C" void wifi_ota_init(void)
{
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

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    preferences_ready = preferences.begin("wifi", false);
    if(!preferences_ready) {
        Serial.println("WiFi: failed to open the credential store");
        set_wifi_status("Fehler beim Öffnen des WLAN-Speichers");
    }

    have_credentials = preferences_ready && preferences.isKey("ssid");
    if(have_credentials) {
        const String ssid = preferences.getString("ssid", "");
        const String password = preferences.getString("password", "");
        if(ssid.isEmpty()) {
            have_credentials = false;
        }
        ssid.toCharArray(pending_ssid, sizeof(pending_ssid));
        password.toCharArray(pending_password, sizeof(pending_password));
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
    }
}

extern "C" void wifi_ota_screen_enter(void)
{
    if(WiFi.status() == WL_CONNECTED || connection_started) return;

    if(have_credentials) {
        saved_connection_pending = true;
        set_wifi_status("Suche nach verfügbaren WLANs...");
    } else {
        set_wifi_status("Bitte WLAN auswählen und verbinden");
    }
    wifi_ota_start_scan();
}

extern "C" void wifi_ota_networks_displayed(void)
{
    if(!saved_connection_pending || !scan_results_ready_for_display ||
       WiFi.status() == WL_CONNECTED || connection_started) {
        return;
    }

    saved_connection_pending = false;
    scan_results_ready_for_display = false;
    char message[64];
    snprintf(message, sizeof(message), "Verbinde mit %s...", pending_ssid);
    set_wifi_status(message);
    connection_started_ms = millis();
    connection_started = true;
    WiFi.begin(pending_ssid, pending_password);
}

extern "C" void wifi_ota_loop(void)
{
    const uint32_t now = millis();
    if(now - last_wifi_update_ms < 250) return;
    last_wifi_update_ms = now;

    const bool connected = WiFi.status() == WL_CONNECTED;
    if(connected) {
        if(!time_sync_started) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
            time_sync_started = true;
            time_sync_started_ms = now;
            set_wifi_status("Verbunden — Uhrzeit wird synchronisiert...");
        } else if(!system_time_is_valid() &&
                  now - time_sync_started_ms >= 60000) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
            time_sync_started_ms = now;
        }

        const bool just_connected_by_user = user_connection_pending;
        bool credential_save_failed = false;
        if(just_connected_by_user) {
            const size_t ssid_saved = preferences_ready
                                          ? preferences.putString("ssid", pending_ssid)
                                          : 0;
            const size_t password_saved = preferences_ready
                                              ? preferences.putString("password", pending_password)
                                              : 0;
            if(ssid_saved > 0 && password_saved > 0) {
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

        if((!wifi_was_connected || just_connected_by_user) &&
           !credential_save_failed && system_time_is_valid()) {
            char message[64];
            snprintf(message, sizeof(message), "Verbunden: %s  %s",
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            set_wifi_status(message);
        }
        if(system_time_is_valid() && !time_was_synchronized) {
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
            set_wifi_status("Verbindung fehlgeschlagen — gespeicherte Daten prüfen");
            Serial.println("WiFi: saved credential connection timed out");
        }
        if(user_connection_pending &&
           now - connection_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
            user_connection_pending = false;
            connection_started = false;
            pending_password[0] = '\0';
            set_wifi_status("Verbindung fehlgeschlagen — Passwort prüfen");
            Serial.println("WiFi: connection attempt timed out");
        } else if(!user_connection_pending && have_credentials &&
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
            scan_generation++;
            scan_results_ready_for_display = true;
            if(WiFi.status() != WL_CONNECTED && !user_connection_pending) {
                set_wifi_status("WLAN-Suche abgeschlossen");
            }
        } else if(result == WIFI_SCAN_FAILED) {
            scan_in_progress = false;
            scan_generation++;
            scan_results_ready_for_display = true;
            if(WiFi.status() != WL_CONNECTED && !user_connection_pending) {
                set_wifi_status("WLAN-Suche fehlgeschlagen");
            }
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
    if(scan_in_progress || ota_busy) return;
    WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true, false);
    if(result == WIFI_SCAN_FAILED) {
        set_wifi_status("WLAN-Suche konnte nicht gestartet werden");
        return;
    }
    scan_in_progress = true;
    scan_results_ready_for_display = false;
    set_wifi_status("Suche nach WLANs...");
}

extern "C" int wifi_ota_network_count(void)
{
    const int result = WiFi.scanComplete();
    return result >= 0 ? result : 0;
}

extern "C" bool wifi_ota_get_network(int index, char *ssid, size_t ssid_size,
                                     int *signal_percent, bool *secured)
{
    if(ssid == nullptr || ssid_size == 0 || index < 0 ||
       index >= wifi_ota_network_count()) {
        return false;
    }

    const String network_name = WiFi.SSID(index);
    network_name.toCharArray(ssid, ssid_size);
    if(signal_percent != nullptr) {
        *signal_percent = (WiFi.RSSI(index) + 100) * 2;
        if(*signal_percent < 0) *signal_percent = 0;
        if(*signal_percent > 100) *signal_percent = 100;
    }
    if(secured != nullptr) {
        *secured = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }
    return ssid[0] != '\0';
}

extern "C" void wifi_ota_connect(const char *ssid, const char *password)
{
    if(ssid == nullptr || *ssid == '\0' || ota_busy) return;

    strlcpy(pending_ssid, ssid, sizeof(pending_ssid));
    strlcpy(pending_password, password == nullptr ? "" : password,
            sizeof(pending_password));
    WiFi.disconnect(false, false);
    WiFi.begin(pending_ssid, pending_password);
    user_connection_pending = true;
    connection_started = true;
    connection_started_ms = millis();
    set_wifi_status("Verbindung wird hergestellt...");
}

extern "C" bool wifi_ota_request_update_check(void)
{
    if(WiFi.status() != WL_CONNECTED) {
        set_update_status("Bitte zuerst WLAN verbinden", 0);
        return false;
    }
    if(ota_busy) {
        set_update_status("Ein Update läuft bereits", download_progress);
        return false;
    }

    portENTER_CRITICAL(&status_mux);
    if(update_decision_pending) {
        portEXIT_CRITICAL(&status_mux);
        set_update_status("Update-Auswahl wird bearbeitet", download_progress);
        return false;
    }
    update_check_requested = true;
    update_available = false;
    strlcpy(update_status, "Update-Prüfung wird gestartet...", sizeof(update_status));
    download_progress = 0;
    portEXIT_CRITICAL(&status_mux);
    return true;
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
    strlcpy(update_status,
            install ? "Update wird vorbereitet..." : "Update nicht installiert.",
            sizeof(update_status));
    portEXIT_CRITICAL(&status_mux);
    return true;
}

extern "C" void wifi_ota_get_snapshot(wifi_ota_snapshot_t *snapshot)
{
    if(snapshot == nullptr) return;

    snapshot->connected = WiFi.status() == WL_CONNECTED;
    snapshot->scanning = scan_in_progress;
    snapshot->updating = ota_busy;
    snapshot->progress = download_progress;
    const int rssi = snapshot->connected ? WiFi.RSSI() : -100;
    snapshot->signal_percent = (rssi + 100) * 2;
    if(snapshot->signal_percent < 0) snapshot->signal_percent = 0;
    if(snapshot->signal_percent > 100) snapshot->signal_percent = 100;
    snapshot->scan_generation = scan_generation;
    strlcpy(snapshot->ssid, WiFi.SSID().c_str(), sizeof(snapshot->ssid));
    strlcpy(snapshot->ip, WiFi.localIP().toString().c_str(), sizeof(snapshot->ip));

    portENTER_CRITICAL(&status_mux);
    snapshot->update_available = update_available;
    strlcpy(snapshot->wifi_status, wifi_status, sizeof(snapshot->wifi_status));
    strlcpy(snapshot->update_status, update_status, sizeof(snapshot->update_status));
    portEXIT_CRITICAL(&status_mux);
}
