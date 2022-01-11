#ifndef FSW_FREERTOS_FSW_INIT_H
#define FSW_FREERTOS_FSW_INIT_H

typedef void (*program_init)(void);

#define PROGRAM_INIT(name) const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = name

#endif /* FSW_FREERTOS_FSW_INIT_H */