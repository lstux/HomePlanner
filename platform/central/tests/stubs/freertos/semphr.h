// Host stand-in, only for platform/central/tests.
#pragma once
typedef void* SemaphoreHandle_t;
#define portMAX_DELAY 0
#define pdTRUE 1
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static int m;return &m;}
inline int xSemaphoreTake(SemaphoreHandle_t,int){return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
inline void vSemaphoreDelete(SemaphoreHandle_t){}
