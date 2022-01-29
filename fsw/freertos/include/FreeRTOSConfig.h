#ifndef FSW_FREERTOS_FREERTOS_CONFIG_H
#define FSW_FREERTOS_FREERTOS_CONFIG_H

#include <string.h> // for memset, needed by port.c

#include <hal/debug.h> // for assert

// #define TASK_DEBUG

#define VIVID_REPLICATE_TASK_CODE                       1

/* Interrupt nesting behaviour configuration. */
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS         0x08000000
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET 0x00010000

/* A header file that defines trace macro can be included here. */

#endif /* FSW_FREERTOS_FREERTOS_CONFIG_H */
