#ifndef FSW_BUS_TEST_FIFO_H
#define FSW_BUS_TEST_FIFO_H

#include <hal/init.h>

void test_fifo_make(const char *prefix);

#define FIFO_REGISTER(prefix)                                                                                         \
    anonymous_symbol(symbol) {                                                                                        \
        PROGRAM_INIT_PARAM(STAGE_RAW, test_fifo_make, symbol, (const char *) prefix)                                  \
    }

#endif /* FSW_BUS_TEST_TEST_H */
