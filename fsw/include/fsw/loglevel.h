#ifndef FSW_LOGLEVEL_H
#define FSW_LOGLEVEL_H

typedef enum {
    CRITICAL = 1, // reporting of critical/unrecoverable errors, including reporting on initialization
    WARNING  = 2, // system faults that are likely recoverable
    INFO     = 3, // top-level information; should be O(1) per command or autonomous action
    DEBUG    = 4, // intermediate information about subsystems; may be O(time) per command
    TRACE    = 5, // low-level information; can be arbitrary amounts of spew.
} loglevel_t;

#endif /* FSW_LOGLEVEL_H */
