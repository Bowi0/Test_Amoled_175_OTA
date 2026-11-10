#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    bool connecting;
    bool scanning;
    bool updating;
    bool update_checking;
    bool installing;
    bool credential_save_failed;
    bool update_available;
    int progress;
    int signal_percent;
    uint32_t scan_generation;
    char ssid[33];
    char ip[16];
    char wifi_status[64];
    char update_status[96];
} wifi_ota_snapshot_t;

void wifi_ota_init(void);
bool wifi_ota_set_enabled(bool enabled);
void wifi_ota_autoconnect(void);
void wifi_ota_screen_enter(void);
void wifi_ota_loop(void);
bool wifi_ota_has_credentials(void);
bool wifi_ota_is_busy(void);
bool wifi_ota_time_valid(void);
void wifi_ota_start_scan(void);
int wifi_ota_network_count(void);
bool wifi_ota_get_network(int index, char *ssid, size_t ssid_size,
                          int *signal_percent, bool *secured);
bool wifi_ota_connect(const char *ssid, const char *password);
bool wifi_ota_has_saved_network(const char *ssid);
bool wifi_ota_connect_saved(const char *ssid);
bool wifi_ota_disconnect(void);
bool wifi_ota_request_update_check(void);
bool wifi_ota_request_mainboard_update_check(void);
bool wifi_ota_confirm_update(bool install);
void wifi_ota_get_snapshot(wifi_ota_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
