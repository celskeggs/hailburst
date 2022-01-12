#ifndef FSW_INIT_H
#define FSW_INIT_H

typedef struct {
    // these stages are defined to make sense for FreeRTOS, because there are more constraints there.
    enum init_stage {
        STAGE_RAW = 0, // no kernel yet; do not attempt to register anything; do not use floating-point operations.
        STAGE_READY,   // kernel initialized; registration functions allowable.
        STAGE_CRAFT,   // spacecraft initialization has completed.
    } init_stage;
    void (*init_fn)(void *param);
    void *init_param;
} program_init;

#define _PP_CHECK_TYPE(expr, type) \
    __builtin_choose_expr( \
        __builtin_types_compatible_p(typeof(expr), type), \
        expr, /* return expr if type matches */ \
        (void) 0 /* to cause a compile-time error */ \
    )

// ********** READ ME IF YOU HAVE A COMPILER ERROR **********
// IF YOU SEE A "invalid use of void expression" ERROR, THAT INDICATES THAT THE TYPE OF THE REGISTERED FUNCTION WAS
// INCORRECT! If PROGRAM_INIT is used, then it means it expected a parameter when none would be provided; if
// PROGRAM_INIT_PARAM is used, then it indicates that the pointer passed and the pointed provided as a parameter are
// not the same type!

#define PROGRAM_INIT(stage, name) \
    const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = \
        { \
            /* make sure that the function is the correct form before casting it */ \
            /* note that casting it will be fine, because it will just ignore the unexpected argument in r0 */ \
            .init_fn = (void (*)(void*)) _PP_CHECK_TYPE(&name, void (*)(void)), \
            .init_stage = stage, \
            .init_param = NULL, \
        }

#define PROGRAM_INIT_PARAM(stage, name, param) \
    const __attribute__((section(".initpoints"))) program_init _initpoint_ ## name = \
        { \
            /* make sure that param pointer type matches the function argument, since we'll be throwing them away! */ \
            .init_fn = (void (*)(void*)) _PP_CHECK_TYPE(&name, void (*)(typeof(*param)*)), \
            .init_stage = stage, \
            .init_param = param, \
        }

void initialize_systems(void);

#endif /* FSW_INIT_H */
