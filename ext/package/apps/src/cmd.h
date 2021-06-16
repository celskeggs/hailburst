#ifndef APP_CMD_H
#define APP_CMD_H

#include <stddef.h>
#include <stdint.h>

#include "spacecraft.h"

typedef enum {
    CMD_STATUS_OK = 0,            // command succeeded
    CMD_STATUS_FAIL = 1,          // command failed
    CMD_STATUS_UNRECOGNIZED = 2,  // command not valid
} cmd_status_t;

cmd_status_t cmd_execute(spacecraft_t *sc, uint32_t cid, uint8_t *args, size_t args_len);

void cmd_mainloop(spacecraft_t *sc);

#endif /* APP_CMD_H */
