#ifndef APP_FAKEWIRE_H
#define APP_FAKEWIRE_H

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
#define FW_CTRL_FCT (0x1F0)
#define FW_CTRL_EOP (0x1F1)
#define FW_CTRL_EEP (0x1F2)
#define FW_CTRL_ESC (0x1F3)

// FakeWire port structure

#define FW_READAHEAD_LEN 100

typedef struct fw_port_st {
    int fd_in;
    int fd_out;

    bool parity_ok;
    bit_buf_t readahead;

    int writeahead_bits;
    uint32_t writeahead;
    uint8_t last_remainder; // 1 if odd # of one bits, 0 if even # of one bits
} fw_port_t;

#define FW_FLAG_SERIAL (0)
#define FW_FLAG_FIFO_PROD (1)
#define FW_FLAG_FIFO_CONS (2)

void fakewire_attach(fw_port_t *fwp, const char *path, int flags);
void fakewire_detach(fw_port_t *fwp);

fw_char_t fakewire_read(fw_port_t *fwp);
void fakewire_write(fw_port_t *fwp, fw_char_t c);

#endif /* APP_FAKEWIRE_H */