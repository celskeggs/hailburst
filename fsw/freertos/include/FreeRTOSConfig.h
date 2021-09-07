#ifndef FSW_FREERTOS_FREERTOS_CONFIG_H
#define FSW_FREERTOS_FREERTOS_CONFIG_H

#include <assert.h>
#include <string.h> // for memset, needed by port.c

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
// configCPU_CLOCK_HZ and configSYSTICK_CLOCK_HZ are not needed for the GCC/ARM_CA9 port.
#define configTICK_RATE_HZ                      250
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                512
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_ALTERNATIVE_API               0 /* Deprecated! */
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t
#define configUSE_TASK_FPU_SUPPORT              2

enum {
    PRIORITY_DRIVERS = 4,
    PRIORITY_SERVERS = 3,
    PRIORITY_WORKERS = 2,
    PRIORITY_INIT    = 1,
};

/* Memory allocation related definitions. */
#define configSUPPORT_STATIC_ALLOCATION             0
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configTOTAL_HEAP_SIZE                       327680
#define configAPPLICATION_ALLOCATED_HEAP            0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP   0

/* Hook function related definitions. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* Run time and task stats gathering related definitions. */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Co-routine related definitions. */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

/* Software timer related definitions. */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

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

/* FreeRTOS MPU specific definitions. */
#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS 0
// #define configTOTAL_MPU_REGIONS                                8 /* Default value. */
// #define configTEX_S_C_B_FLASH                                  0x07UL /* Default value. */
// #define configTEX_S_C_B_SRAM                                   0x07UL /* Default value. */
#define configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY            1

/* Tick interrupts */
extern void vConfigureTickInterrupt(void);
#define configSETUP_TICK_INTERRUPT() vConfigureTickInterrupt()

/* Optional functions - most linkers will remove unused functions anyway. */
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  0
#define INCLUDE_vTaskDelayUntil                 0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          0
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xEventGroupSetBitFromISR        0
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              0

/* A header file that defines trace macro can be included here. */
#include <stdio.h>  // for printf, needed by trace handler
#include <rtos/timer.h>  // for timer_now_ns, needed by trace handler
#define traceTASK_SWITCHED_IN() do { uint64_t now = timer_now_ns(); printf("[%u.%09u] FreeRTOS scheduling %15s at priority %u\n", (uint32_t) (now / TIMER_NS_PER_SEC), (uint32_t) (now % TIMER_NS_PER_SEC), pxCurrentTCB->pcTaskName, pxCurrentTCB->uxPriority); } while(0)

#endif /* FSW_FREERTOS_FREERTOS_CONFIG_H */