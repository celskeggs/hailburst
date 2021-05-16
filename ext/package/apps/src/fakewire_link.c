#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "fakewire_link.h"

// #define DEBUG

#define PORT_IO "/dev/ttyAMA1"

void fakewire_link_attach(fw_link_t *fwp, const char *path, int flags) {
    memset(fwp, 0, sizeof(fw_link_t));

    bit_buf_init(&fwp->readahead, FW_READAHEAD_LEN);
    fwp->parity_ok = true;
    fwp->write_ok = true;

    fwp->writeahead_bits = 0;
    fwp->writeahead = 0;
    fwp->last_remainder = 0; // (either initialization should be fine)

    if (flags != FW_FLAG_SERIAL) {
        assert(flags == FW_FLAG_FIFO_CONS || flags == FW_FLAG_FIFO_PROD);
        // alternate mode for host testing via pipe


        char path_buf[strlen(path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", path);
        int fd_c2p = open(path_buf, (flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", path);
        int fd_p2c = open(path_buf, (flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            exit(1);
        }
        fwp->fd_in = (flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fwp->fd_out = (flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
    } else {
        fwp->fd_out = fwp->fd_in = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fwp->fd_in < 0) {
            perror("open");
            exit(1);
        }
        fcntl(fwp->fd_in, F_SETFL, 0);

        struct termios options;

        if (tcgetattr(fwp->fd_in, &options) < 0) {
            perror("tcgetattr");
            exit(1);
        }

        cfsetispeed(&options, B9600);
        cfsetospeed(&options, B9600);

        // don't attach
        options.c_cflag |= CLOCAL | CREAD;

        // 8-bit data
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;

        // raw input
        options.c_iflag &= ~(IXON | IXOFF | ICRNL | IGNCR | INLCR | ISTRIP);
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

        // raw output
        options.c_oflag &= ~OPOST;

        if (tcsetattr(fwp->fd_in, TCSANOW, &options) < 0) {
            perror("tcsetattr");
            exit(1);
        }
    }
    assert(fwp->fd_in != 0 && fwp->fd_out != 0);
}

void fakewire_link_detach(fw_link_t *fwp) {
    assert(fwp->fd_in != 0 && fwp->fd_out != 0);
    printf("Detaching link...\n");
    if (fwp->fd_in >= 0 && fwp->fd_in != fwp->fd_out) {
        if (close(fwp->fd_in) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_in = -1;
    }
    if (fwp->fd_out >= 0) {
        if (close(fwp->fd_out) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_out = -1;
    }
    bit_buf_destroy(&fwp->readahead);
}

static int count_ones(uint32_t value) {
    int count = 0;
    while (value) {
        count += (value & 1);
        value >>= 1;
    }
    return count;
}

static fw_char_t fakewire_link_parse_readbuf(fw_link_t *fwp) {
    if (!fwp->parity_ok) {
        return FW_PARITYFAIL;
    }
    size_t avail_bits = bit_buf_extractable_bits(&fwp->readahead);
    if (avail_bits < 6) {
        return -1;
    }
    uint32_t head = bit_buf_peek_bits(&fwp->readahead, 2);
    // ignore this parity bit... it was previously processed
    if (!(head & 2)) {
        if (avail_bits < 12) {
            // need more bits before parity can be validated
            return -1;
        }
        // data character
        fw_char_t dc = (bit_buf_extract_bits(&fwp->readahead, 10) >> 2);
        assert(dc == FW_DATA(dc));
        head = bit_buf_peek_bits(&fwp->readahead, 2);
#ifdef DEBUG
        printf("data character: %x with head %x\n", dc, head);
#endif
        if (((count_ones(dc) + count_ones(head)) & 1) != 1) {
            fprintf(stderr, "fakewire_link_parse_readbuf: hit parity failure on data character %x with head %x\n", dc, head);
            // parity fail!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        return dc;
    } else {
        // control character
        fw_char_t control = bit_buf_extract_bits(&fwp->readahead, 4) >> 2;
        assert(control >= 0 && control <= 3);
        head = bit_buf_peek_bits(&fwp->readahead, 2);
#ifdef DEBUG
        printf("control character: %x with head %x\n", control, head);
#endif
        if (((count_ones(control) + count_ones(head)) & 1) != 1) {
            fprintf(stderr, "fakewire_link_parse_readbuf: hit parity failure on control character %x with head %x\n", control, head);
            // parity fail!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        return FW_CTRL_FCT | control;
    }
}

fw_char_t fakewire_link_read(fw_link_t *fwp) {
    uint8_t readbuf[FW_READAHEAD_LEN];
    fw_char_t ch;
    while ((ch = fakewire_link_parse_readbuf(fwp)) < 0) {
        size_t count = bit_buf_insertable_bytes(&fwp->readahead);
        // if we can't parse yet, must have space to add more data!
        if (count < 1 && count > FW_READAHEAD_LEN) {
            fprintf(stderr, "fakewire_link_read: count outside of expected range: %u not in [1, %u]\n", count, FW_READAHEAD_LEN);
            abort();
        }
        int fd = fwp->fd_in;
        if (fd == -1) {
            fprintf(stderr, "fakewire_link_read: connection found to be closed (no fd)\n");
            // connection already closed!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        ssize_t actual = read(fwp->fd_in, readbuf, count);
        if (actual < 0) {
            fprintf(stderr, "fakewire_link_read: attempt to read failed\n");
            // read failure... end connection!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        } else if (actual == 0) {
            fprintf(stderr, "fakewire_link_read: encountered end of file\n");
            // EOF... end of connection!
            fwp->parity_ok = false;
            return FW_PARITYFAIL;
        }
        assert(actual >= 1 && actual <= count);
#ifdef DEBUG
        for (size_t i = 0; i < actual; i++) {
            uint8_t val = readbuf[i];
            printf("read byte: %d%d%d%d%d%d%d%d\n",
                    val&1, (val>>1)&1, (val>>2)&1, (val>>3)&1, (val>>4)&1, (val>>5)&1, (val>>6)&1, (val>>7)&1);
        }
#endif
        bit_buf_insert_bytes(&fwp->readahead, readbuf, actual);
    }
    return ch;
}

static int fakewire_link_write_bits(fw_link_t *fwp, uint32_t data, int nbits) {
    assert(0 <= fwp->writeahead_bits && fwp->writeahead_bits < 8);
    assert(nbits >= 1 && nbits <= 32);
    assert(fwp->writeahead_bits + nbits <= 32);
    data &= (1 << nbits) - 1;
    fwp->writeahead |= (data << fwp->writeahead_bits);
    fwp->writeahead_bits += nbits;
    while (fwp->writeahead_bits >= 8) {
        uint8_t c = fwp->writeahead & 0xFF;
#ifdef DEBUG
        printf("Writing byte: %d: %d%d%d%d%d%d%d%d\n", c, c&1, (c>>1)&1, (c>>2)&1, (c>>3)&1, (c>>4)&1, (c>>5)&1, (c>>6)&1, (c>>7)&1);
#endif
        int fd = fwp->fd_out;
        // closing...
        if (fd == -1) {
            return -1;
        }
        if (write(fd, &c, 1) < 1) {
            return -1;
        }
        fwp->writeahead >>= 8;
        fwp->writeahead_bits -= 8;
    }
    return 0;
}

void fakewire_link_write(fw_link_t *fwp, fw_char_t c) {
    if (!fwp->write_ok) {
        return;
    }

#ifdef DEBUG
    switch (c) {
    case FW_PARITYFAIL:
        printf("Writing character: ParityFail\n");
        break;
    case FW_CTRL_FCT:
        printf("Writing character: FCT\n");
        break;
    case FW_CTRL_EOP:
        printf("Writing character: EOP\n");
        break;
    case FW_CTRL_EEP:
        printf("Writing character: EEP\n");
        break;
    case FW_CTRL_ESC:
        printf("Writing character: ESC\n");
        break;
    default:
        printf("Writing character: 0x%02x -> %c\n", c, c);
        break;
    }
#endif

    int ctrl_bit = FW_IS_CTRL(c) ? 1 : 0;

    // [last:odd] [P] [C=0] -> P must be 0 to be odd!
    // [last:odd] [P] [C=1] -> P must be 1 to be odd!
    // [last:even] [P] [C=0] -> P must be 1 to be odd!
    // [last:even] [P] [C=1] -> P must be 0 to be odd!
    int parity_bit = fwp->last_remainder ^ ctrl_bit ^ 1;
    assert((parity_bit >> 1) == 0);

#ifdef DEBUG
    printf("Generated bits (PC): %d%d\n", parity_bit, ctrl_bit);
#endif

    uint32_t data_bits;
    int nbits;
    if (FW_IS_CTRL(c)) {
        assert(c >= FW_CTRL_FCT && c <= FW_CTRL_ESC);
        data_bits = c & 3;
        nbits = 2;
#ifdef DEBUG
        printf("Generated bits (CT): %d%d\n", c & 1, (c >> 1) & 1);
#endif
    } else {
        data_bits = FW_DATA(c);
        assert(c == data_bits);
        nbits = 8;
#ifdef DEBUG
        printf("Generated bits (DA): %d%d%d%d%d%d%d%d\n", c&1, (c>>1)&1, (c>>2)&1, (c>>3)&1, (c>>4)&1, (c>>5)&1, (c>>6)&1, (c>>7)&1);
#endif
    }
    if (fakewire_link_write_bits(fwp, (data_bits << 2) | (ctrl_bit << 1) | parity_bit, nbits + 2) < 0) {
        fwp->write_ok = false;
        return;
    }

    fwp->last_remainder = count_ones(data_bits) & 1;
}

bool fakewire_link_write_ok(fw_link_t *fwp) {
    return fwp->write_ok;
}
