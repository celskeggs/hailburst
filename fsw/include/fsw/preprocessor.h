#ifndef FSW_PREPROCESSOR_H
#define FSW_PREPROCESSOR_H

#define PP_CHECK_TYPE(expr, type) \
    __builtin_choose_expr( \
        __builtin_types_compatible_p(typeof(expr), type), \
        expr, /* return expr if type matches */ \
        (void) 0 /* to cause a compile-time error */ \
    )

#define PP_ERASE_TYPE(callback, param) \
    (void (*)(void*)) PP_CHECK_TYPE(&callback, void (*)(typeof(*param)*))

#endif /* FSW_PREPROCESSOR_H */
