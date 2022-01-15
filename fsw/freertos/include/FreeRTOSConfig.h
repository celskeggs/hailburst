#ifndef FSW_FREERTOS_FREERTOS_CONFIG_H
#define FSW_FREERTOS_FREERTOS_CONFIG_H

#include <string.h> // for memset, needed by port.c

#include <fsw/debug.h> // for assert

#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
// configCPU_CLOCK_HZ and configSYSTICK_CLOCK_HZ are not needed for the GCC/ARM_CA9 port.
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    6
#define configMINIMAL_STACK_SIZE                512
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   2
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configUSE_TASK_FPU_SUPPORT              2

enum {
    PRIORITY_REPAIR  = 5, // only used for critical repair tasks!
    PRIORITY_DRIVERS = 4,
    PRIORITY_SERVERS = 3,
    PRIORITY_WORKERS = 2,
    PRIORITY_INIT    = 1,
    PRIORITY_IDLE    = 0,
};

/* Hook function related definitions. */
#define configCHECK_FOR_STACK_OVERFLOW          2

/* Interrupt nesting behaviour configuration. */
// #define configKERNEL_INTERRUPT_PRIORITY         [dependent of processor]
// #define configMAX_SYSCALL_INTERRUPT_PRIORITY    [dependent on processor and application]
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
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* A header file that defines trace macro can be included here. */

// #define TASK_DEBUG

extern void trace_task_switch(const char *task_name, unsigned int priority);

#ifdef TASK_DEBUG
#define traceTASK_SWITCHED_IN() trace_task_switch(pxCurrentTCB->pcTaskName, pxCurrentTCB->uxPriority)
#endif

#endif /* FSW_FREERTOS_FREERTOS_CONFIG_H */
