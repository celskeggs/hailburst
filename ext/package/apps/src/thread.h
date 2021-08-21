#ifndef APP_THREAD_H
#define APP_THREAD_H

#ifdef __FREERTOS__
#include <thread_freertos.h>
#else
#include "thread_posix.h"
#endif

#endif /* APP_THREAD_H */
