#ifndef TEST_ESP_OTA_OPS_H
#define TEST_ESP_OTA_OPS_H
#include <cstdint>
#include <cstddef>
struct esp_partition_t { uint32_t size; };
typedef int esp_ota_handle_t;
#define ESP_OK 0
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
int esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
int esp_ota_write(esp_ota_handle_t, const void *, size_t);
int esp_ota_end(esp_ota_handle_t);
int esp_ota_abort(esp_ota_handle_t);
int esp_ota_set_boot_partition(const esp_partition_t *);
#endif
