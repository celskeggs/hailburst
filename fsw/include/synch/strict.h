#ifndef FSW_SYNCH_STRICT_H
#define FSW_SYNCH_STRICT_H

#include <hal/debug.h>
#include <synch/config.h>

// Note: on Linux, only strict mode is available.
#if ( CONFIG_SYNCH_MODE_STRICT == 0 ) && defined(__VIVID__)
#define malfunctionf(fmt, ...) restartf("Malfunction: " fmt, ## __VA_ARGS__)
#define miscomparef(fmt, ...) debugf(WARNING, "Miscompare: " fmt, ## __VA_ARGS__)
#else
#define malfunctionf(fmt, ...) abortf("Malfunction: " fmt, ## __VA_ARGS__)
#define miscomparef(fmt, ...) abortf("Miscompare: " fmt, ## __VA_ARGS__)
#endif

#endif /* FSW_SYNCH_STRICT_H */
