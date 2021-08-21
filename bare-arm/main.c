#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include "arm.h"
#include "gic.h"
#include "timer.h"

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

void timer_loop(void *param) {
    (void) param;
    int iter = 0;
    while (1) {
        printf("timing: %d milliseconds have elapsed, time is %llu\n", iter, timer_now_ns());
        vTaskDelay(50 / portTICK_PERIOD_MS);
        iter += 50;
    }
}

void scrub_loop(void *param) {
    (void) param;
    int pass = 0;
    while (1) {
        printf("scrubbing memory (pass #%d) at vtime=%llu ns\n", pass++, timer_now_ns());
        scrub_memory();
    }
}

int main(void) {
    BaseType_t status;
    status = xTaskCreate(timer_loop, "timer_loop", 100, NULL, 4, NULL);
    if (status != pdPASS) {
        puts("Error: could not create timer_loop task");
        return 1;
    }

    status = xTaskCreate(scrub_loop, "scrub_loop", 100, NULL, 1, NULL);
    if (status != pdPASS) {
        puts("Error: could not create scrub_loop task");
        return 1;
    }

    vTaskSuspend(NULL);
    return 0;
}
