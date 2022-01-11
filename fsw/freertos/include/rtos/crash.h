#ifndef FSW_FREERTOS_RTOS_CRASH_H
#define FSW_FREERTOS_RTOS_CRASH_H

#include <FreeRTOS.h>
#include <task.h>

#include <hal/thread.h>

extern thread_t iter_first_thread;

void restart_task(TaskHandle_t task);
void task_clear_crash(void);

void thread_restart_other_task(thread_t state);

#endif /* FSW_FREERTOS_RTOS_CRASH_H */
