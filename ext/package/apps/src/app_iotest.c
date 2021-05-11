#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "fakewire.h"

static fw_port_t fwport;
static bool fwport_init;

void init_iotest(void) {
    assert(!fwport_init);

    fakewire_attach(&fwport, "/dev/ttyAMA1", FW_FLAG_SERIAL);

    fwport_init = true;
}

static void write_message(const char *msg) {
    fakewire_write(&fwport, FW_CTRL_ESC);
    fakewire_write(&fwport, FW_CTRL_FCT);
    for (const char *ch = msg; *ch; ch++) {
        fakewire_write(&fwport, (uint8_t) *ch);
    }
    fakewire_write(&fwport, FW_CTRL_EOP);
    fakewire_write(&fwport, FW_CTRL_ESC);
    fakewire_write(&fwport, FW_CTRL_FCT);
}

void task_iotest_transmitter(void) {
    assert(fwport_init);

    char msgbuf[64];

    for (int index = 0;; index++) {
        snprintf(msgbuf, sizeof(msgbuf), "this is txmit msg #%d\n", index);
        printf("tx: sending msg %d (%d bytes)...\n", index, strlen(msgbuf));
        write_message(msgbuf);
        printf("tx: sent msg %d!\n", index);
        // only once per second
        sleep(1);
    }
}

static bool read_message(char *msgout, size_t len) {
    while (true) {
        assert(len > 0);
        fw_char_t ch = fakewire_read(&fwport);
        if (!FW_IS_CTRL(ch)) {
            if (len == 1) {
                *msgout = '\0';
                fprintf(stderr, "rx: msg buffer filled unexpectedly!\n");
                return true;
            }
            assert(ch == (uint8_t) ch && len > 1);
            *msgout = ch;
            msgout++;
            len--;
        } else if (ch == FW_PARITYFAIL) {
            fprintf(stderr, "rx: parity failure\n");
            return false;
        } else if (ch == FW_CTRL_ESC) {
            ch = fakewire_read(&fwport);
            if (ch == FW_CTRL_FCT) {
                // null! ignore.
                continue;
            } else if (ch == FW_PARITYFAIL) {
                fprintf(stderr, "rx: parity failure\n");
                return false;
            } else {
                fprintf(stderr, "rx: unexpected ctrl character %x after ESC\n", ch);
                continue;
            }
        } else if (ch == FW_CTRL_EOP) {
            // normal completion of packet
            *msgout = '\0';
            return true;
        } else {
            fprintf(stderr, "rx: unexpected ctrl character %x\n", ch);
        }
    }
}

void task_iotest_receiver(void) {
    assert(fwport_init);

    char msgbuf[128];

    for (int index = 0;; index++) {
        printf("rx: reading message %d...\n", index);
        if (!read_message(msgbuf, sizeof(msgbuf))) {
            fprintf(stderr, "rx: halting receive loop\n");
            break;
        }
        printf("rx: read message %d of %d bytes\n", index, strlen(msgbuf));
        printf("rx: MSG: \"");
        for (int i = 0; msgbuf[i] != '\0'; i++) {
            char c = msgbuf[i];
            if (c >= 32 && c <= 126) {
                if (c == '"' || c == '\\') {
                    putchar('\\');
                }
                putchar(c);
            } else {
                printf("\\x%02x", c & 0xFF);
            }
        }
        printf("\"\n");
    }
}
