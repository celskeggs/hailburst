#ifndef FSW_FREERTOS_FSW_INIT_H
#define FSW_FREERTOS_FSW_INIT_H

typedef struct {
    enum init_stage {
        STAGE_RAW = 0, // no kernel yet; do not attempt to register anything; do not use floating-point operations.
        STAGE_READY,   // kernel initialized; registration functions allowable.
    } init_stage;
    union {
        void (*init_fn_1)(void *param);
        void (*init_fn_0)(void);
    };
    void *init_param;
} program_init;

#define PROGRAM_INIT(stage, name) \
    const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = \
        { .init_fn_0 = name, .init_stage = stage, .init_param = NULL }

#define PROGRAM_INIT_PARAM(stage, name, param) \
    const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = \
        { .init_fn_1 = name, .init_stage = stage, .init_param = param }

#endif /* FSW_FREERTOS_FSW_INIT_H */
