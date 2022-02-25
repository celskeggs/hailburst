#ifndef FSW_SYNCH_CONFIG_H
#define FSW_SYNCH_CONFIG_H

#include <hal/debug.h>

// CONFIG_SYNCH_MODE_STRICT can be set to one of two values:
//   [0] Timing and data errors will be considered recoverable. (Use for operation in the presence of radiation.)
//   [1] Timing and data errors will be considered crash-worthy. (Use for testing in the absence of radiation.)
#define CONFIG_SYNCH_MODE_STRICT 1

#if ( CONFIG_SYNCH_MODE_STRICT == 0 )
#define miscomparef(fmt, ...) restartf("Miscompare: " fmt, ## __VA_ARGS__)
#else
#define miscomparef(fmt, ...) abortf("Miscompare: " fmt, ## __VA_ARGS__)
#endif

#endif /* FSW_SYNCH_CONFIG_H */
