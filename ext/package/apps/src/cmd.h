#ifndef APP_CMD_H
#define APP_CMD_H

#include <stddef.h>
#include <stdint.h>

void cmd_execute(uint32_t cid, uint64_t timestamp_ns, uint8_t *args, size_t args_len);

#endif /* APP_CMD_H */
