#ifndef FSW_FREERTOS_RTOS_CRASH_H
#define FSW_FREERTOS_RTOS_CRASH_H

#include <FreeRTOS.h>
#include <task.h>

typedef struct {
    void (*hook_callback)(void *param, TaskHandle_t task);
    void *hook_param;
} task_restart_hook_t;

typedef void (*task_restart_hook)(TaskHandle_t task);

void task_idle_init(void);
void task_restart_init(void);
void task_set_restart_handler(TaskHandle_t task, task_restart_hook_t *hook);
void restart_task(TaskHandle_t task);
void task_clear_crash(void);

#endif /* FSW_FREERTOS_RTOS_CRASH_H */
