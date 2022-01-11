#ifndef FSW_FREERTOS_FSW_INIT_H
#define FSW_FREERTOS_FSW_INIT_H

typedef struct {
    enum init_stage {
        STAGE_RAW = 0, // no kernel yet; do not attempt to register anything; do not use floating-point operations.
        STAGE_READY,   // kernel initialized; registration functions allowable.
    } init_stage;
    void (*init_fn)(void);
} program_init;

#define PROGRAM_INIT(stage, name) \
    const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = \
        { .init_fn = name, .init_stage = stage };

#endif /* FSW_FREERTOS_FSW_INIT_H */
