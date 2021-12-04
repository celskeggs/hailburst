#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <rtos/virtqueue.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/io.h>

// #define DEBUG_VIRTQ

enum {
    VIRTIO_MAGIC_VALUE    = 0x74726976,
    VIRTIO_LEGACY_VERSION = 1,
    VIRTIO_VERSION        = 2,

    VIRTIO_IRQ_BIT_USED_BUFFER = 0x1,
    VIRTIO_IRQ_BIT_CONF_CHANGE = 0x2,
};

enum {
    VIRTIO_DEVSTAT_ACKNOWLEDGE        = 1,
    VIRTIO_DEVSTAT_DRIVER             = 2,
    VIRTIO_DEVSTAT_DRIVER_OK          = 4,
    VIRTIO_DEVSTAT_FEATURES_OK        = 8,
    VIRTIO_DEVSTAT_DEVICE_NEEDS_RESET = 64,
    VIRTIO_DEVSTAT_FAILED             = 128,
};

// all of these are little-endian
struct virtio_mmio_registers {
    const volatile uint32_t magic_value;         // Magic value (R)
    const volatile uint32_t version;             // Device version number (R)
    const volatile uint32_t device_id;           // Virtio Subsystem Device ID (R)
    const volatile uint32_t vendor_id;           // Virtio Subsystem Vendor ID (R)
    const volatile uint32_t device_features;     // Flags representing features the device supports (R)
          volatile uint32_t device_features_sel; // Device (host) features word selection (W)
                   uint32_t RESERVED_0[2];
          volatile uint32_t driver_features;     // Flags representing device features understood and activated by the driver (W)
          volatile uint32_t driver_features_sel; // Activated (guest) features word selection (W)
                   uint32_t RESERVED_1[2];
          volatile uint32_t queue_sel;           // Virtual queue index (W)
    const volatile uint32_t queue_num_max;       // Maximum virtual queue size (R)
          volatile uint32_t queue_num;           // Virtual queue size (W)
                   uint32_t RESERVED_2[2];
          volatile uint32_t queue_ready;         // Virtual queue ready bit (RW)
                   uint32_t RESERVED_3[2];
          volatile uint32_t queue_notify;        // Queue notifier (W)
                   uint32_t RESERVED_4[3];
    const volatile uint32_t interrupt_status;    // Interrupt status (R)
          volatile uint32_t interrupt_ack;       // Interrupt acknowledge (W)
                   uint32_t RESERVED_5[2];
          volatile uint32_t status;              // Device status (RW)
                   uint32_t RESERVED_6[3];
          volatile uint64_t queue_desc;          // Virtual queue's Descriptor Area 64 bit long physical address (W)
                   uint32_t RESERVED_7[2];
          volatile uint64_t queue_driver;        // Virtual queue's Driver Area 64 bit long physical address (W)
                   uint32_t RESERVED_8[2];
          volatile uint64_t queue_device;        // Virtual queue's Device Area 64 bit long physical address (W)
                   uint32_t RESERVED_9[21];
    const volatile uint32_t config_generation;   // Configuration atomicity value (R)
};
static_assert(sizeof(struct virtio_mmio_registers) == 0x100, "wrong sizeof(struct virtio_mmio_registers)");

static void *zalloc_aligned(size_t size, size_t align) {
    assert(size > 0 && align > 0);
    uint8_t *out = malloc(size + align - 1);
    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, size + align - 1);
    size_t misalignment = (size_t) out % align;
    if (misalignment != 0) {
        out += align - misalignment;
    }
    assert((uintptr_t) out % align == 0);
    return out;
}

// TODO: go back through and add all the missing conversions from LE32 to CPU

// monitors two things:
static void virtio_monitor(struct virtio_device *device, uint32_t queue_index, struct virtio_device_queue *queue) {
    assert(queue != NULL);

    // only monitor if initialized
    if (queue->chart == NULL) {
        return;
    }

    assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);

    bool is_output = (queue->direction == QUEUE_OUTPUT);
    assert(is_output || queue->direction == QUEUE_INPUT);

    // FIRST: process chart updates

    // how many descriptors have we already sent to the device?
    uint32_t outstanding = (queue->avail->idx - queue->last_used_idx + 2 * queue->queue_num) % (2 * queue->queue_num);
    // how many reads or writes do we have that could be made outstanding?
    chart_index_t avail = is_output ? chart_reply_avail(queue->chart) : chart_request_avail(queue->chart);
    // we could have any number of these reads or writes outstanding, but not more than this many
    assertf(outstanding <= avail, "queue=%u, outstanding=%u, available=%u, avail->idx=%u, last_used=%u",
            queue_index, outstanding, avail, queue->avail->idx, queue->last_used_idx);

#ifdef DEBUG_VIRTQ
    debugf(TRACE, "VIRTIO[Q=%u]: Found %u outstanding descriptors and %u available notes.", queue_index, outstanding, avail);
    debugf(TRACE, "VIRTIO[Q=%u]: queue->avail->idx = %u, queue->last_used_idx = %u.", queue_index, queue->avail->idx, queue->last_used_idx);
#endif

    // start new reads or writes if we are able to do so now
    for (chart_index_t i = outstanding; i < avail; i++) {
        void *request = is_output ? chart_reply_peek(queue->chart, i) : chart_request_peek(queue->chart, i);
        // find the index of our matching ring buffer entry
        uint32_t index = (queue->last_used_idx + i) % queue->queue_num;
        // make sure the two match, so that we know we're using the right descriptor
        assert(request == chart_get_note(queue->chart, index));
        // these should still match from the previous cycle, so we don't need to update anything.
        assert(queue->avail->ring[index] == index);

        // if this is a write, make sure we transfer the actual request length, instead of using the max size
        if (is_output) {
            struct io_tx_ent *tx_request = request;
            // validate that length fits within constraints
            assert(tx_request->actual_length <= io_tx_size(queue->chart));
            // update descriptor to have the right length
            queue->desc[index].len = tx_request->actual_length;
        }

#ifdef DEBUG_VIRTQ
        debugf(TRACE, "VIRTIO[Q=%u]: Dispatching transaction for index=%u.", queue_index, index);
#endif
    }

    // notify device of any new descriptors
    uint16_t new_avail = (uint16_t) (queue->last_used_idx + avail);
    if (new_avail != queue->avail->idx) {
        atomic_store(queue->avail->idx, new_avail);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(device->mmio->queue_notify, queue_index);
        }
    }

    // SECOND: process 'used' ring buffer from device

    // TODO: do I need to validate the rest of the data structures somewhere (i.e. that the descriptors haven't changed?)

    chart_index_t advance_count = 0;

    while (queue->last_used_idx != htole16(queue->used->idx)) {
        uint32_t ring_index = queue->last_used_idx % queue->queue_num;
        struct virtq_used_elem *elem = &queue->used->ring[ring_index];
#ifdef DEBUG_VIRTQ
        debugf(TRACE, "VIRTIO[Q=%u]: Received transaction for index=%u (len=%u, last_used_idx=%u, vq->used->idx=%u).",
               queue_index, ring_index, elem->len, queue->last_used_idx, htole16(queue->used->idx));
#endif
        assert(elem->id == ring_index);

        if (is_output) {
            // no work to do; just validate the state is correct
            assert(elem->len == 0);
            assert(chart_reply_peek(queue->chart, advance_count) == chart_get_note(queue->chart, ring_index));
        } else {
            // if this trips, it might be because the device tried to write more data than there was actually room
            assertf(elem->len > 0 && elem->len <= io_rx_size(queue->chart),
                "elem->len=%u, rx_size=%u, desc len=%u",
                elem->len, io_rx_size(queue->chart), queue->desc[ring_index].len);

            struct io_rx_ent *request = chart_request_peek(queue->chart, advance_count);
            assert(request == chart_get_note(queue->chart, ring_index));
            // populate the actual receive timestamp and actual receive length
            request->receive_timestamp = clock_timestamp();
            request->actual_length = elem->len;
        }

        queue->last_used_idx++;
        advance_count++;
    }

    // transfer control back to the other end of the chart
    if (advance_count > 0) {
        if (is_output) {
            chart_reply_send(queue->chart, advance_count);
        } else {
            chart_request_send(queue->chart, advance_count);
        }
    }
}

static void *virtio_monitor_loop(void *opaque_device) {
    assert(opaque_device != NULL);
    struct virtio_device *device = (struct virtio_device *) opaque_device;

#ifdef DEBUG_VIRTQ
    debugf(TRACE, "VIRTIO[Q=*]: Entering monitor loop.");
#endif
    for (;;) {
        // update I/O
        for (uint32_t i = 0; i < device->num_queues; i++) {
            virtio_monitor(device, i, &device->queues[i]);
        }

        // wait for event, which might either be from the chart or from the IRQ callback
        BaseType_t value;
        value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        assert(value != 0);

        // process event
#ifdef DEBUG_VIRTQ
        debugf(TRACE, "VIRTIO[Q=*]: Processing received monitor IRQ in task.");
#endif
    }
}

static void virtio_device_irq_callback(void *opaque_device) {
    assert(opaque_device != NULL);
    struct virtio_device *device = (struct virtio_device *) opaque_device;
    assert(device->initialized == true && device->monitor_task != NULL && device->monitor_task->handle != NULL);

    BaseType_t was_woken = pdFALSE;
    uint32_t status = device->mmio->interrupt_status;
    if (status & VIRTIO_IRQ_BIT_USED_BUFFER) {
        // TODO: find a way to do this that doesn't involve accessing private fields of thread_t
        vTaskNotifyGiveFromISR(device->monitor_task->handle, &was_woken);
    }
    device->mmio->interrupt_ack = status;
    portYIELD_FROM_ISR(was_woken);
}

static void virtio_device_chart_wakeup(void *opaque) {
    struct virtio_device *device = (struct virtio_device *) opaque;
    assert(device != NULL && device->monitor_task != NULL && device->monitor_task->handle != NULL);

    // TODO: find a way to do this that doesn't involve accessing private fields of thread_t
    BaseType_t result = xTaskNotifyGive(device->monitor_task->handle);
    assert(result == pdPASS);
}

bool virtio_device_setup_queue(struct virtio_device *device, uint32_t queue_index, virtio_queue_dir_t direction,
                               chart_t *chart) {
    assert(device != NULL && chart != NULL);
    assert(direction == QUEUE_INPUT || direction == QUEUE_OUTPUT);
    assert(device->initialized == true && device->monitor_task == NULL);
    assert(device->queues != NULL);
    assert(queue_index < device->num_queues);

    struct virtio_device_queue *queue = &device->queues[queue_index];
    assert(queue->chart == NULL);

    device->mmio->queue_sel = queue_index;
    if (device->mmio->queue_ready != 0) {
        debugf(CRITICAL, "VIRTIO device apparently already had virtqueue %d initialized; failing.", queue_index);
        return false;
    }
    // inconsistency if we hit this: we already checked this condition during discovery!
    assert(device->mmio->queue_num_max != 0);

    queue->direction = direction;
    queue->queue_num = chart_note_count(chart);
    assert(queue->queue_num > 0);

    if (queue->queue_num > device->mmio->queue_num_max) {
        debugf(CRITICAL, "VIRTIO device supports up to %u entries in a queue buffer, but sticky-chart contained %u.",
               device->mmio->queue_num_max, queue->queue_num);
        return false;
    }

    device->mmio->queue_num = queue->queue_num;

    queue->last_used_idx = 0;

    queue->desc = zalloc_aligned(sizeof(struct virtq_desc) * queue->queue_num, 16);
    assert(queue->desc != NULL);
    queue->avail = zalloc_aligned(sizeof(struct virtq_avail) + sizeof(uint16_t) * queue->queue_num, 2);
    assert(queue->avail != NULL);
    queue->used = zalloc_aligned(sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * queue->queue_num, 4);
    assert(queue->used != NULL);

    device->mmio->queue_desc   = (uint64_t) (uintptr_t) queue->desc;
    device->mmio->queue_driver = (uint64_t) (uintptr_t) queue->avail;
    device->mmio->queue_device = (uint64_t) (uintptr_t) queue->used;

    atomic_store(device->mmio->queue_ready, 1);

    if (direction == QUEUE_INPUT) {
        // make sure chart is in expected blank state
        assert(chart_request_avail(chart) == chart_note_count(chart));
        assert(chart_request_peek(chart, 0) == chart_get_note(chart, 0));
        assert(chart_reply_avail(chart) == 0);

        chart_attach_client(chart, virtio_device_chart_wakeup, device);
    } else if (direction == QUEUE_OUTPUT) {
        chart_attach_server(chart, virtio_device_chart_wakeup, device);
    } else {
        assert(false);
    }

    // configure descriptors to refer to chart memory directly
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        uint8_t *data_ptr;
        if (direction == QUEUE_INPUT) {
            struct io_rx_ent *entry = chart_get_note(chart, i);
            data_ptr = &entry->data[0];
            assert(chart_note_size(chart) > sizeof(*entry));
        } else if (direction == QUEUE_OUTPUT) {
            struct io_tx_ent *entry = chart_get_note(chart, i);
            data_ptr = &entry->data[0];
            assert(chart_note_size(chart) > sizeof(*entry));
        } else {
            assert(false);
        }
        queue->desc[i] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = (uint64_t) (uintptr_t) data_ptr,
            .len   = direction == QUEUE_INPUT ? io_rx_size(chart) : 0,
            .flags = direction == QUEUE_INPUT ? VIRTQ_DESC_F_WRITE : 0,
            .next  = 0xFFFF /* invalid index */,
        };
        // populate all of the avail ring entries to their corresponding descriptors.
        // we won't need to change these again.
        queue->avail->ring[i] = i;
    }
    if (direction == QUEUE_INPUT) {
        assert(queue->avail->idx == 0);
        atomic_store(queue->avail->idx, queue->queue_num);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(device->mmio->queue_notify, queue_index);
        }
    }

    // set chart ONLY if we succeed, because it's what marks the queue as being valid
    queue->chart = chart;

    debugf(DEBUG, "VIRTIO queue %d now configured", queue_index);

    return true;
}

void virtio_device_force_notify_queue(struct virtio_device *device, uint32_t queue_index) {
    // validate device initialized
    assert(device != NULL && device->initialized == true && device->queues != NULL);
    // validate queue index
    assert(queue_index < device->num_queues);
    // make sure this queue has actually been set up.
    assert(device->queues[queue_index].chart != NULL);

    // spuriously notify the queue.
    atomic_store_relaxed(device->mmio->queue_notify, queue_index);
}

static void virtio_device_teardown_queue(struct virtio_device *device, uint32_t queue_index) {
    assert(device != NULL);
    assert(device->initialized == true && device->monitor_task == NULL);
    assert(device->queues != NULL);
    assert(queue_index < device->num_queues);

    struct virtio_device_queue *queue = &device->queues[queue_index];
    if (queue->chart != NULL) {
        queue->chart = NULL;
        assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);
        free(queue->desc);
        free(queue->avail);
        free(queue->used);
    } else {
        assert(queue->desc == NULL && queue->avail == NULL && queue->used == NULL);
    }
    memset(queue, 0, sizeof(*queue));
}

// true on success, false on failure
bool virtio_device_init(struct virtio_device *device, uintptr_t mem_addr, uint32_t irq, uint32_t device_id,
                        virtio_feature_select_cb feature_select) {
    assert(device != NULL && device->initialized == false);
    struct virtio_mmio_registers *mmio = (struct virtio_mmio_registers *) mem_addr;

    device->mmio = mmio;
    device->config_space = mmio + 1;
    device->irq = irq;

    debugf(DEBUG, "VIRTIO device: addr=%x, irq=%u.", mem_addr, irq);

    if (le32toh(mmio->magic_value) != VIRTIO_MAGIC_VALUE) {
        debugf(CRITICAL, "VIRTIO device had the wrong magic number: 0x%08x instead of 0x%08x; failing.",
               le32toh(mmio->magic_value), VIRTIO_MAGIC_VALUE);
        return false;
    }

    if (le32toh(mmio->version) == VIRTIO_LEGACY_VERSION) {
        debugf(CRITICAL, "VIRTIO device configured as legacy-only; cannot initialize; failing. "
               "Set -global virtio-mmio.force-legacy=false to fix this.");
        return false;
    } else if (le32toh(mmio->version) != VIRTIO_VERSION) {
        debugf(CRITICAL, "VIRTIO device version not recognized: found %u instead of %u; failing.",
               le32toh(mmio->version), VIRTIO_VERSION);
        return false;
    }

    // make sure this is a serial port
    if (le32toh(mmio->device_id) != device_id) {
        debugf(CRITICAL, "VIRTIO device ID=%u instead of ID=%u; failing.", le32toh(mmio->device_id), device_id);
        return false;
    }

    // reset the device
    mmio->status = htole32(0);

    // acknowledge the device
    mmio->status |= htole32(VIRTIO_DEVSTAT_ACKNOWLEDGE);
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER);

    // read the feature bits
    mmio->device_features_sel = htole32(0);
    uint64_t features = htole32(mmio->device_features);
    mmio->device_features_sel = htole32(1);
    features |= ((uint64_t) htole32(mmio->device_features)) << 32;

    // select feature bits
    if (!feature_select(&features)) {
        mmio->status |= htole32(VIRTIO_DEVSTAT_FAILED);
        return false;
    }

    // write selected bits back
    mmio->driver_features_sel = htole32(0);
    mmio->driver_features = htole32((uint32_t) features);
    mmio->driver_features_sel = htole32(1);
    mmio->driver_features = htole32((uint32_t) (features >> 32));

    // validate features
    mmio->status |= htole32(VIRTIO_DEVSTAT_FEATURES_OK);
    if (!(le32toh(mmio->status) & VIRTIO_DEVSTAT_FEATURES_OK)) {
        debugf(CRITICAL, "VIRTIO device did not set FEATURES_OK: read back status=%08x; failing.", mmio->status);
        mmio->status |= htole32(VIRTIO_DEVSTAT_FAILED);
        return false;
    }

    // discover number of queues
    for (uint32_t queue_i = 0; ; queue_i++) {
        device->mmio->queue_sel = queue_i;
        if (device->mmio->queue_ready != 0) {
            debugf(CRITICAL, "VIRTIO device already had virtqueue %d initialized; failing.", queue_i);
            mmio->status |= htole32(VIRTIO_DEVSTAT_FAILED);
            return false;
        }
        if (device->mmio->queue_num_max == 0) {
            device->num_queues = queue_i;
            break;
        }
    }

    if (device->num_queues == 0) {
        debugf(CRITICAL, "VIRTIO device discovered to have 0 queues; failing.");
        mmio->status |= htole32(VIRTIO_DEVSTAT_FAILED);
        return false;
    }

    debugf(DEBUG, "VIRTIO device discovered to have %u queues.", device->num_queues);

    device->queues = malloc(sizeof(struct virtio_device_queue) * device->num_queues);
    assert(device->queues != NULL);

    // mark everything as uninitialized
    for (uint32_t vq = 0; vq < device->num_queues; vq++) {
        device->queues[vq].chart = NULL;
    }

    // enable driver
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER_OK);

    assert(device->initialized == false);
    device->initialized = true;

    return true;
}

void virtio_device_start(struct virtio_device *device) {
    assert(device != NULL && device->initialized);
    assert(device->monitor_task == NULL);
    thread_create(&device->monitor_task, "virtio-monitor", PRIORITY_DRIVERS, virtio_monitor_loop, device,
                  RESTARTABLE);
    assert(device->monitor_task != NULL);
    enable_irq(device->irq, virtio_device_irq_callback, device);
}

void *virtio_device_config_space(struct virtio_device *device) {
    assert(device != NULL && device->initialized && device->config_space != NULL);
    return device->config_space;
}

void virtio_device_fail(struct virtio_device *device) {
    // currently, this can only be called before virtio_device_start is called
    assert(device != NULL && device->initialized);
    assert(device->monitor_task == NULL);
    device->mmio->status |= htole32(VIRTIO_DEVSTAT_FAILED);
    // wait until after we indicate that we've failed before we free any memory, just in case some of it was referenced
    // by buffers provided to the device.
    assert(device->queues != NULL);
    for (uint32_t i = 0; i < device->num_queues; i++) {
        virtio_device_teardown_queue(device, i);
    }
    free(device->queues);
    // clear everything
    memset(device, 0, sizeof(*device));
}
