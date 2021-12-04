#ifndef FSW_LOGLEVEL_H
#define FSW_LOGLEVEL_H

typedef enum {
    CRITICAL = 1, // regarding major system events, or any system faults
    INFO     = 2, // top-level information; should be O(1) per command or autonomous action
    DEBUG    = 3, // intermediate information about subsystems; may be O(time) per command
    TRACE    = 4, // low-level information; can be arbitrary amounts of spew.
} loglevel_t;

#endif /* FSW_LOGLEVEL_H */
