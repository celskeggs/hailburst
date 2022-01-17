#ifndef FSW_FREERTOS_FREERTOS_CONFIG_H
#define FSW_FREERTOS_FREERTOS_CONFIG_H

#include <string.h> // for memset, needed by port.c

#include <fsw/debug.h> // for assert

// configCPU_CLOCK_HZ and configSYSTICK_CLOCK_HZ are not needed for the GCC/ARM_CA9 port.
#define configTICK_RATE_HZ                      10000
#define configMINIMAL_STACK_SIZE                512
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   2
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configUSE_TASK_FPU_SUPPORT              2

/* Interrupt nesting behaviour configuration. */
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS         0x08000000
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET 0x00010000
// 256 external interrupts + 32 internal interrupts
#define configUNIQUE_INTERRUPT_PRIORITIES               (256)
#define configMAX_API_CALL_INTERRUPT_PRIORITY           (129) // no idea if this is the right value

/* Define to trap errors during development. */
//#define configASSERT( ( x ) ) if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )
#define configASSERT(x) assert(x)

/* Tick interrupts */
extern void vConfigureTickInterrupt(void);
#define configSETUP_TICK_INTERRUPT() vConfigureTickInterrupt()

/* Optional functions - most linkers will remove unused functions anyway. */
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskDelayUntil                 0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* A header file that defines trace macro can be included here. */

// #define TASK_DEBUG

extern void trace_task_switch(const char *task_name);

#endif /* FSW_FREERTOS_FREERTOS_CONFIG_H */
