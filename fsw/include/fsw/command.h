#ifndef FSW_CMD_H
#define FSW_CMD_H

#include <stddef.h>
#include <stdint.h>

#include <fsw/spacecraft.h>

void cmd_mainloop(spacecraft_t *sc);

// may only be used once
#define COMMAND_REGISTER(c_ident, c_spacecraft)                                                              \
    TASK_REGISTER(c_ident ## _task, "cmd_loop", cmd_mainloop, &c_spacecraft, RESTARTABLE);                   \
    static void c_ident ## _init(void) {                                                                     \
        comm_dec_set_task(&c_spacecraft.comm_decoder, &c_ident ## _task);                                    \
    }                                                                                                        \
    PROGRAM_INIT(STAGE_CRAFT, c_ident ## _init);

#endif /* FSW_CMD_H */
