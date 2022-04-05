#ifndef FSW_VIVID_RTOS_CRASH_H
#define FSW_VIVID_RTOS_CRASH_H

#include <FreeRTOS.h>
#include <task.h>

#include <hal/thread.h>

void restart_task(TaskHandle_t task);

#endif /* FSW_VIVID_RTOS_CRASH_H */
