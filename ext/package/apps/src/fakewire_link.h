#ifndef APP_FAKEWIRE_LINK_H
#define APP_FAKEWIRE_LINK_H

#include <stdint.h>
#include <stdbool.h>

#include "bitbuf.h"

// FakeWire character type

typedef int16_t fw_char_t;
#define FW_BIT_CTRL (0x100)
#define FW_MASK_DATA (0xFF)

#define FW_IS_CTRL(fwchar) (((fwchar) & FW_BIT_CTRL) != 0)
#define FW_DATA(fwchar) ((fwchar) & FW_MASK_DATA)

#define FW_PARITYFAIL (0x180)
// Flow Control Token
#define FW_CTRL_FCT (0x1F0)
// Normal End of Packet
#define FW_CTRL_EOP (0x1F1)
// Error End of Packet
#define FW_CTRL_EEP (0x1F2)
// Escape
#define FW_CTRL_ESC (0x1F3)

// FakeWire port structure

#define FW_READAHEAD_LEN 100

typedef struct fw_link_st {
    int fd_in;
    int fd_out;

    bool parity_ok;
    bit_buf_t readahead;

    bool write_ok;
    int writeahead_bits;
    uint32_t writeahead;
    uint8_t last_remainder; // 1 if odd # of one bits, 0 if even # of one bits
} fw_link_t;

#define FW_FLAG_SERIAL (0)
#define FW_FLAG_FIFO_PROD (1)
#define FW_FLAG_FIFO_CONS (2)

void fakewire_link_attach(fw_link_t *fwp, const char *path, int flags);
void fakewire_link_detach(fw_link_t *fwp);

fw_char_t fakewire_link_read(fw_link_t *fwp);
void fakewire_link_write(fw_link_t *fwp, fw_char_t c);
bool fakewire_link_write_ok(fw_link_t *fwp);

#endif /* APP_FAKEWIRE_LINK_H */