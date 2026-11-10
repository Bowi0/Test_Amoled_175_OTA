#ifndef UART_SWITCH_H
#define UART_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void uart_switch_init(void);
void uart_switch_loop(void);
bool uart_switch_online(void);
uint8_t uart_switch_state(void);
bool uart_switch_pending(void);
bool uart_switch_version(char *version, size_t size);
bool uart_switch_take_pressure(float *pressure_bar);
bool uart_switch_toggle(unsigned channel);
bool uart_update_acquire(void);
bool uart_update_transfer(const uint8_t *image, uint32_t size,
                          const uint8_t digest[32],
                          void (*progress)(uint32_t, uint32_t));
void uart_update_release(void);
#ifdef __cplusplus
}
#endif
#endif
