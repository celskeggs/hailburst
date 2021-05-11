#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "app.h"

// 21 MB!
#define BUFSIZE (21 * 1024 * 1024)
#define FILL (0xCA72F19E)
volatile int *buffer = NULL;

void scrub_memory(void) {
	for (int i = 0; i < BUFSIZE / sizeof(int); i++) {
		if (buffer[i] != FILL) {
			printf("Scrubbed error in memory at address %p: %x\n", &buffer[i], buffer[i] ^ FILL);
			buffer[i] = FILL;
		}
	}
}

void task_scrub_memory(void) {
	int now = 0;
	buffer = (int*) malloc(BUFSIZE);
	for (int i = 0; i < BUFSIZE / sizeof(int); i++) {
		buffer[i] = FILL;
	}
	if (!buffer) {
		fputs("Failed to allocate\n", stderr);
		exit(1);
	}
	fprintf(stderr, "Succeeded in allocation: buffer at %p\n", buffer);
	for (;;) {
		printf("Scrub iteration %d...\n", ++now);
		scrub_memory();
		sleep(1);
	}
}
