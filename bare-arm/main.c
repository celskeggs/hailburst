#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include "arm.h"
#include "gic.h"
#include "io.h"
#include "virtio.h"
#include <timer.h>

volatile unsigned int scan_buffer[64 * 1024];

void scrub_memory(void)
{
    for (unsigned int i = 0; i < sizeof(scan_buffer) / sizeof(*scan_buffer); i++) {
        if (scan_buffer[i] != 0) {
            printk("memory error: addr=0x%08x, value=0x%08x\n");
            scan_buffer[i] = 0;
        }
    }
}

void timer_loop(void *param) {
    (void) param;
    int iter = 0;
    while (1) {
        printk("timing: %d milliseconds have elapsed, time is %llu\n", iter, timer_now_ns());
        vTaskDelay(50 / portTICK_PERIOD_MS);
        iter += 50;
    }
}

void scrub_loop(void *param) {
    (void) param;
    int pass = 0;
    while (1) {
        printk("scrubbing memory (pass #%d) at vtime=%llu ns\n", pass++, timer_now_ns());
        scrub_memory();
    }
}

static struct virtio_console console;

int main(void) {
    if (!virtio_init(&console, VIRTIO_MMIO_ADDRESS, VIRTIO_MMIO_IRQ)) {
        printk("Failed to initialize virtio device.\n");
        return 1;
    }
    printk("Initialization on main thread complete. Suspending main thread to let others run.\n");
    vTaskSuspend(NULL);
    return 0;

/*
    BaseType_t status;

    status = xTaskCreate(timer_loop, "timer_loop", 1000, NULL, 4, NULL);
    if (status != pdPASS) {
        printk("Error: could not create timer_loop task: error %d\n", status);
        return 1;
    }

    status = xTaskCreate(scrub_loop, "scrub_loop", 1000, NULL, 1, NULL);
    if (status != pdPASS) {
        printk("Error: could not create scrub_loop task: error %d\n", status);
        return 1;
    }

    return 0;
*/
}
