#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include "arm.h"
#include "gic.h"
#include "io.h"
#include "virtio.h"
#include <timer.h>
#include <thread_freertos.h>

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

static void *mainloop_run(void *opaque) {
    struct virtio_console_port *port = (struct virtio_console_port *) opaque;

    virtio_serial_ready(port);

    char data_buffer[] = "Hello, World!\n";
    struct vector_entry txv = {
        .data_buffer = data_buffer,
        .length      = sizeof(data_buffer) - 1,
        .is_receive  = false,
    };
    printk("Transacting...\n");
    ssize_t actual = virtio_transact_sync(port->transmitq, &txv, 1);
    printk("Transacted: %zd!\n", actual);
    assert(actual == 0);

    char rdata[1024];
    struct vector_entry rxv = {
        .data_buffer = rdata,
        .length      = sizeof(rdata),
        .is_receive  = true,
    };
    printk("Transacting read...\n");
    actual = virtio_transact_sync(port->receiveq, &rxv, 1);
    printk("Transacted: %zd: %s!\n", actual, rdata);

    return NULL;
}

static void console_initialize(void *opaque, struct virtio_console_port *con) {
    (void) opaque;

    printk("Initialized console... starting thread.\n");
    thread_t ptr;
    thread_create(&ptr, mainloop_run, con);
    // discard ptr
}

int main(void) {
    printk("Initializing all virtio ports...\n");
    virtio_init(console_initialize, NULL);

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
