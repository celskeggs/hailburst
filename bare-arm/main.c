#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>

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

void scrub_loop(void *param) {
    (void) param;
    int pass = 0;
    while (1) {
        printf("scrubbing memory (pass #%d)\n", pass++);
        scrub_memory();
    }
}

int main(int argc, char *argv[0])
{
    printf("Initialized: argc=%d, argv[0]=%s\n", argc, argv[0]);

    BaseType_t status;
    status = xTaskCreate(scrub_loop, "scrub_loop", 100, NULL, 1, NULL);
    if (status != pdPASS) {
        puts("Error: could not create scrub_loop task");
        return 1;
    }

    puts("Starting scheduler...");

    vTaskStartScheduler();

    puts("Error: ran out of RAM before scheduler was initialized");
    return 1;
}
