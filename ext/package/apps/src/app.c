#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// 40 MB!
#define BUFSIZE (20 * 1024 * 1024)
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

int main(int argc, char *argv[]) {
	int now = 0;
	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);
	buffer = (int*) malloc(BUFSIZE);
	for (int i = 0; i < BUFSIZE / sizeof(int); i++) {
		buffer[i] = FILL;
	}
	if (!buffer) {
		fputs("Failed to allocate\n", stderr);
	}
	fprintf(stderr, "Succeeded in allocation: buffer at %p\n", buffer);
	for (;;) {
		printf("Scrub iteration %d...\n", ++now);
		scrub_memory();
		sleep(1);
	}
}
