#ifndef FSW_CMD_H
#define FSW_CMD_H

#include <stddef.h>
#include <stdint.h>

#include <fsw/spacecraft.h>

void cmd_mainloop(void *opaque);

// may only be used once
#define COMMAND_REGISTER(c_ident, c_spacecraft) \
    TASK_REGISTER(c_ident ## _task, "cmd_loop", PRIORITY_WORKERS, cmd_mainloop, &c_spacecraft, RESTARTABLE)

#endif /* FSW_CMD_H */
