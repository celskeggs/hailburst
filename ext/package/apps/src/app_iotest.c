#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "fakewire_exc.h"

static fw_exchange_t fwport;
static bool fwport_init;

void init_iotest(void) {
    assert(!fwport_init);

    fakewire_exc_init(&fwport, "iotest");
    fakewire_exc_attach(&fwport, "/dev/ttyAMA1", FW_FLAG_SERIAL);

    fwport_init = true;
}

void task_iotest_transmitter(void) {
    assert(fwport_init);

    char msgbuf[64];

    for (int index = 0;; index++) {
        snprintf(msgbuf, sizeof(msgbuf), "this is txmit msg #%d\n", index);
        printf("tx: sending msg %d (%zd bytes)...\n", index, strlen(msgbuf));
        if (fakewire_exc_write(&fwport, (uint8_t*) msgbuf, strlen(msgbuf)) < 0) {
            printf("tx: failed to write\n");
            break;
        }
        printf("tx: sent msg %d!\n", index);
        // only once per second
        sleep(1);
    }
}

void task_iotest_receiver(void) {
    assert(fwport_init);

    uint8_t msgbuf[256];

    for (int index = 0;; index++) {
        printf("rx: reading message %d...\n", index);
        ssize_t len = fakewire_exc_read(&fwport, msgbuf, sizeof(msgbuf));
        if (len < 0) {
            fprintf(stderr, "rx: errored; halting receive loop\n");
            break;
        }
        printf("rx: read message %d of %zd bytes\n", index, len);
        printf("rx: MSG: \"");
        for (int i = 0; i < len; i++) {
            uint8_t c = msgbuf[i];
            if (c >= 32 && c <= 126) {
                if (c == '"' || c == '\\') {
                    putchar('\\');
                }
                putchar(c);
            } else {
                printf("\\x%02x", c);
            }
        }
        printf("\"\n");
    }
}
