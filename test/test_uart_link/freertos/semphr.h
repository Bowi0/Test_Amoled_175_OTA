#ifndef TEST_SEMPHR_H
#define TEST_SEMPHR_H
typedef bool *SemaphoreHandle_t;
#define pdTRUE 1
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static bool locked = false; return &locked; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, int)
{
    if(*mutex) return 0;
    *mutex = true;
    return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { *mutex = false; }
#endif
