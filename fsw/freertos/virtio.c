#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
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

void virtio_monitor_loop(struct virtio_device *device) {
    assert(device != NULL);

    device->monitor_started = true;

#ifdef DEBUG_VIRTQ
    debugf(TRACE, "VIRTIO[Q=*]: Entering monitor loop.");
#endif
    for (;;) {
        // update I/O
        for (uint32_t i = 0; i < device->num_queues; i++) {
            virtio_monitor(device, i, &device->queues[i]);
        }

        // wait for event, which might either be from the chart or from the IRQ callback
        task_doze();

        // process event
#ifdef DEBUG_VIRTQ
        debugf(TRACE, "VIRTIO[Q=*]: Processing received monitor IRQ in task.");
#endif
    }
}

static void virtio_device_irq_callback(void *opaque_device) {
    struct virtio_device *device = (struct virtio_device *) opaque_device;
    assert(device != NULL && device->initialized == true && device->monitor_task != NULL
             && device->monitor_started == true);

    uint32_t status = device->mmio->interrupt_status;
    if (status & VIRTIO_IRQ_BIT_USED_BUFFER) {
        task_rouse_from_isr(device->monitor_task);
    }
    device->mmio->interrupt_ack = status;
    portYIELD_FROM_ISR(pdFALSE); // TODO: can this be eliminated?
}

void virtio_device_setup_queue_internal(struct virtio_device *device, uint32_t queue_index) {
    struct virtio_device_queue *queue = &device->queues[queue_index];

    assert(queue->chart != NULL);
    assert(queue->queue_num > 0);
    assert(queue->desc != NULL);
    assert(queue->avail != NULL);
    assert(queue->used != NULL);

    device->mmio->queue_sel = queue_index;
    if (device->mmio->queue_ready != 0) {
        abortf("VIRTIO device apparently already had virtqueue %d initialized; failing.", queue_index);
    }
    // inconsistency if we hit this: we already checked this condition during discovery!
    assert(device->mmio->queue_num_max != 0);

    if (queue->queue_num > device->mmio->queue_num_max) {
        abortf("VIRTIO device supports up to %u entries in a queue buffer, but sticky-chart contained %u.",
               device->mmio->queue_num_max, queue->queue_num);
    }

    device->mmio->queue_num = queue->queue_num;

    queue->last_used_idx = 0;

    device->mmio->queue_desc   = (uint64_t) (uintptr_t) queue->desc;
    device->mmio->queue_driver = (uint64_t) (uintptr_t) queue->avail;
    device->mmio->queue_device = (uint64_t) (uintptr_t) queue->used;

    atomic_store(device->mmio->queue_ready, 1);

    if (queue->direction == QUEUE_INPUT) {
        // make sure chart is in expected blank state
        assert(chart_request_avail(queue->chart) == chart_note_count(queue->chart));
        assert(chart_request_peek(queue->chart, 0) == chart_get_note(queue->chart, 0));
        assert(chart_reply_avail(queue->chart) == 0);

        chart_attach_client(queue->chart, PP_ERASE_TYPE(task_rouse, device->monitor_task), (void*) device->monitor_task);
    } else if (queue->direction == QUEUE_OUTPUT) {
        chart_attach_server(queue->chart, PP_ERASE_TYPE(task_rouse, device->monitor_task), (void*) device->monitor_task);
    } else {
        abortf("Invalid queue direction: %u", queue->direction);
    }

    // configure descriptors to refer to chart memory directly
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        uint8_t *data_ptr;
        if (queue->direction == QUEUE_INPUT) {
            struct io_rx_ent *entry = chart_get_note(queue->chart, i);
            data_ptr = &entry->data[0];
            assert(chart_note_size(queue->chart) > sizeof(*entry));
        } else if (queue->direction == QUEUE_OUTPUT) {
            struct io_tx_ent *entry = chart_get_note(queue->chart, i);
            data_ptr = &entry->data[0];
            assert(chart_note_size(queue->chart) > sizeof(*entry));
        } else {
            abortf("Invalid queue direction: %u", queue->direction);
        }
        queue->desc[i] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = (uint64_t) (uintptr_t) data_ptr,
            .len   = queue->direction == QUEUE_INPUT ? io_rx_size(queue->chart) : 0,
            .flags = queue->direction == QUEUE_INPUT ? VIRTQ_DESC_F_WRITE : 0,
            .next  = 0xFFFF /* invalid index */,
        };
        // populate all of the avail ring entries to their corresponding descriptors.
        // we won't need to change these again.
        queue->avail->ring[i] = i;
    }
    if (queue->direction == QUEUE_INPUT) {
        assert(queue->avail->idx == 0);
        atomic_store(queue->avail->idx, queue->queue_num);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(device->mmio->queue_notify, queue_index);
        }
    }

    debugf(DEBUG, "VIRTIO queue %d now configured", queue_index);
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

// this function runs during STAGE_RAW, so it had better not use any kernel registration facilities
void virtio_device_init_internal(struct virtio_device *device) {
    assert(device != NULL && device->initialized == false && device->num_queues == 0 && device->max_queues > 0);
    struct virtio_mmio_registers *mmio = device->mmio;

    debugf(DEBUG, "VIRTIO device: addr=%x, irq=%u.", (uintptr_t) device->mmio, device->irq);

    if (le32toh(mmio->magic_value) != VIRTIO_MAGIC_VALUE) {
        abortf("VIRTIO device had the wrong magic number: 0x%08x instead of 0x%08x; failing.",
               le32toh(mmio->magic_value), VIRTIO_MAGIC_VALUE);
    }

    if (le32toh(mmio->version) == VIRTIO_LEGACY_VERSION) {
        abortf("VIRTIO device configured as legacy-only; cannot initialize; failing. "
               "Set -global virtio-mmio.force-legacy=false to fix this.");
    } else if (le32toh(mmio->version) != VIRTIO_VERSION) {
        abortf("VIRTIO device version not recognized: found %u instead of %u; failing.",
               le32toh(mmio->version), VIRTIO_VERSION);
    }

    // make sure this is a serial port
    if (le32toh(mmio->device_id) != device->expected_device_id) {
        abortf("VIRTIO device ID=%u instead of ID=%u; failing.", le32toh(mmio->device_id), device->expected_device_id);
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
    device->feature_select_cb(&features);

    // write selected bits back
    mmio->driver_features_sel = htole32(0);
    mmio->driver_features = htole32((uint32_t) features);
    mmio->driver_features_sel = htole32(1);
    mmio->driver_features = htole32((uint32_t) (features >> 32));

    // validate features
    mmio->status |= htole32(VIRTIO_DEVSTAT_FEATURES_OK);
    if (!(le32toh(mmio->status) & VIRTIO_DEVSTAT_FEATURES_OK)) {
        abortf("VIRTIO device did not set FEATURES_OK: read back status=%08x; failing.", mmio->status);
    }

    // discover number of queues
    for (uint32_t queue_i = 0; ; queue_i++) {
        device->mmio->queue_sel = queue_i;
        if (device->mmio->queue_ready != 0) {
            abortf("VIRTIO device already had virtqueue %d initialized; failing.", queue_i);
        }
        if (device->mmio->queue_num_max == 0 || queue_i >= device->max_queues) {
            device->num_queues = queue_i;
            break;
        }
    }

    assert(device->num_queues <= device->max_queues);

    if (device->num_queues == 0) {
        abortf("VIRTIO device discovered to have 0 queues; failing.");
    }

    debugf(DEBUG, "VIRTIO device discovered to have %u queues.", device->num_queues);

    // mark everything as uninitialized
    for (uint32_t vq = 0; vq < device->num_queues; vq++) {
        device->queues[vq].chart = NULL;
    }

    // enable driver
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER_OK);

    assert(device->initialized == false);
    device->initialized = true;
}

void virtio_device_start_internal(struct virtio_device *device) {
    assert(device != NULL && device->initialized == true && device->monitor_task != NULL);
    // it's okay to run this here, even before the task is necessarily created, because interrupts will not be enabled
    // until the scheduler starts running
    enable_irq(device->irq, virtio_device_irq_callback, device);
}

void *virtio_device_config_space(struct virtio_device *device) {
    assert(device != NULL && device->initialized && device->mmio != NULL);
    return device->mmio + 1;
}
