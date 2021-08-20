#include <stdio.h>

volatile unsigned int scan_buffer[64 * 1024];

void scrub_memory(void)
{
    for (unsigned int i = 0; i < sizeof(scan_buffer) / sizeof(*scan_buffer); i++) {
        if (scan_buffer[i] != 0) {
            printf("memory error: addr=0x%08x, value=0x%08x\n");
            scan_buffer[i] = 0;
        }
    }
}

int main(int argc, char *argv[0])
{
    int pass = 0;
    puts("hello, world!");
    while (1) {
        printf("scrubbing memory (pass #%d)\n", pass++);
        scrub_memory();
    }
    return 0;
}
