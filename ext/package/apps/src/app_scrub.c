#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "fakewire_exc.h"
#include "thread.h"

// 21 MB!
#define BUFSIZE (21 * 1024 * 1024)
#define FILL (0xCA72F19E)
volatile int *buffer = NULL;

static void alloc_memory(void) {
	buffer = (int*) malloc(BUFSIZE);
	for (int i = 0; i < BUFSIZE / sizeof(int); i++) {
		buffer[i] = FILL;
	}
	if (!buffer) {
		fputs("Failed to allocate\n", stderr);
		exit(1);
	}
	fprintf(stderr, "Succeeded in allocation: buffer at %p\n", buffer);
}

static uint32_t scrub_memory(void) {
    uint32_t count = 0;
	for (int i = 0; i < BUFSIZE / sizeof(int); i++) {
		if (buffer[i] != FILL) {
			printf("Scrubbed error in memory at address %p: %x\n", &buffer[i], buffer[i] ^ FILL);
			buffer[i] = FILL;
			count++;
		}
	}
	return count;
}

// roughly, "SCRUB ALL"
#define MAGIC_HEADER 0x2C90BA11

static volatile bool run_ok = true;
static volatile uint32_t marker = 0;

static void send_packet(fw_exchange_t *exc, uint32_t iteration, uint32_t total) {
    uint32_t packet[4] = {
        htonl(MAGIC_HEADER),
        htonl(iteration),
        htonl(total),
        htonl(marker),
    };
    if (fakewire_exc_write(exc, (uint8_t*) packet, sizeof(packet)) < 0) {
        fprintf(stderr, "writer task hit error; stopping.\n");
        run_ok = false;
    }
}

// discards all data; only needed because exchanges won't initialize until they're ready to receive data
static void *reader_task(void *opaque) {
    fw_exchange_t *exc = (fw_exchange_t *) opaque;
    uint32_t output;
    for (;;) {
        ssize_t count = fakewire_exc_read(exc, (uint8_t*) &output, sizeof(output));
        if (count < 0) {
            fprintf(stderr, "reader task hit error; stopping.\n");
            run_ok = false;
            return NULL;
        }
        if (count != 4) {
            fprintf(stderr, "reader task received invalid packet size: %d\n", count);
            run_ok = false;
            return NULL;
        }
        marker = ntohl(output);
    }
}

void task_scrub_memory(void) {
    fw_exchange_t fwport;
    fakewire_exc_init(&fwport, "scrub");
    fakewire_exc_attach(&fwport, "/dev/ttyAMA1", FW_FLAG_SERIAL);

    alloc_memory();

    pthread_t reader;
    thread_create(&reader, reader_task, &fwport);

	uint32_t iteration = 0, total = 0;
	printf("Beginning scrub...\n");
	while (run_ok) {
	    iteration += 1;
		total += scrub_memory();
		printf("Scrubbed %u errors across %u iterations.\n", total, iteration);
		send_packet(&fwport, iteration, total);
		printf("Sent packet.\n");
		sleep(1);
	}

    fakewire_exc_detach(&fwport);
    thread_join(reader);
    fakewire_exc_destroy(&fwport);
}
