#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_init(void);
void ble_toggle(void);
bool ble_set_enabled(bool enabled);
bool ble_is_active(void);
void ble_send_pressure(float pressure);

#ifdef __cplusplus
}
#endif

#endif // BLE_MANAGER_H
