#ifndef FSW_FREERTOS_RTOS_CRASH_H
#define FSW_FREERTOS_RTOS_CRASH_H

#include <FreeRTOS.h>
#include <task.h>

#include <hal/thread.h>

void restart_task(TaskHandle_t task);

#endif /* FSW_FREERTOS_RTOS_CRASH_H */
