#ifndef FSW_IO_H
#define FSW_IO_H

#include <stdint.h>

struct io_rx_ent {
    uint64_t receive_timestamp;
    uint32_t actual_length;
    uint8_t  data[];
};

struct io_tx_ent {
    uint32_t actual_length;
    uint8_t  data[];
};

#endif /* FSW_IO_H */
