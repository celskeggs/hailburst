#include <assert.h>
#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <rtos/virtqueue.h>
#include <hal/thread.h>

// #define DEBUG_INIT
// #define DEBUG_VIRTQ

enum {
    VIRTIO_MAGIC_VALUE    = 0x74726976,
    VIRTIO_LEGACY_VERSION = 1,
    VIRTIO_VERSION        = 2,

    VIRTIO_CONSOLE_ID     = 3,

    VIRTIO_CONSOLE_VQ_RECEIVE  = 0,
    VIRTIO_CONSOLE_VQ_TRANSMIT = 1,

    VIRTIO_CONSOLE_VQ_CTRL_BASE = 2,

    VIRTIO_IRQ_BIT_USED_BUFFER = 0x1,
    VIRTIO_IRQ_BIT_CONF_CHANGE = 0x2,
};

enum {
    VIRTIO_CONSOLE_DEVICE_READY  = 0,
    VIRTIO_CONSOLE_DEVICE_ADD    = 1,
    VIRTIO_CONSOLE_DEVICE_REMOVE = 2,
    VIRTIO_CONSOLE_PORT_READY    = 3,
    VIRTIO_CONSOLE_CONSOLE_PORT  = 4,
    VIRTIO_CONSOLE_RESIZE        = 5,
    VIRTIO_CONSOLE_PORT_OPEN     = 6,
    VIRTIO_CONSOLE_PORT_NAME     = 7,
};

enum {
    VIRTIO_DEVSTAT_ACKNOWLEDGE        = 1,
    VIRTIO_DEVSTAT_DRIVER             = 2,
    VIRTIO_DEVSTAT_DRIVER_OK          = 4,
    VIRTIO_DEVSTAT_FEATURES_OK        = 8,
    VIRTIO_DEVSTAT_DEVICE_NEEDS_RESET = 64,
    VIRTIO_DEVSTAT_FAILED             = 128,
};

enum {
    VIRTIO_CONSOLE_F_SIZE        = 1ull << 0,
    VIRTIO_CONSOLE_F_MULTIPORT   = 1ull << 1, // **
    VIRTIO_CONSOLE_F_EMERG_WRITE = 1ull << 2, // **

    VIRTIO_F_RING_INDIRECT_DESC = 1ull << 28, // **
    VIRTIO_F_RING_EVENT_IDX     = 1ull << 29, // **
    VIRTIO_F_VERSION_1          = 1ull << 32, // **
    VIRTIO_F_ACCESS_PLATFORM    = 1ull << 33,
    VIRTIO_F_RING_PACKED        = 1ull << 34,
    VIRTIO_F_IN_ORDER           = 1ull << 35,
    VIRTIO_F_ORDER_PLATFORM     = 1ull << 36,
    VIRTIO_F_SR_IOV             = 1ull << 37,
    VIRTIO_F_NOTIFICATION_DATA  = 1ull << 38,
};

#define LE32_TO_CPU(x) (le32toh(x))
#define CPU_TO_LE32(x) (htole32(x))

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
          volatile uint64_t queue_desc;          // Virtual queue’s Descriptor Area 64 bit long physical address (W)
                   uint32_t RESERVED_7[2];
          volatile uint64_t queue_driver;        // Virtual queue’s Driver Area 64 bit long physical address (W)
                   uint32_t RESERVED_8[2];
          volatile uint64_t queue_device;        // Virtual queue’s Device Area 64 bit long physical address (W)
                   uint32_t RESERVED_9[21];
    const volatile uint32_t config_generation;   // Configuration atomicity value (R)
};
_Static_assert(sizeof(struct virtio_mmio_registers) == 0x100, "wrong sizeof(struct virtio_mmio_registers)");

struct virtio_console_config {
    uint16_t cols;
    uint16_t rows;
    uint32_t max_nr_ports;
    uint32_t emerg_wr;
};
_Static_assert(sizeof(struct virtio_console_config) == 12, "wrong sizeof(struct virtio_console_config)");

struct virtio_console_control {
    uint32_t id;    /* Port number */
    uint16_t event; /* The kind of control event */
    uint16_t value; /* Extra information for the event */
};
_Static_assert(sizeof(struct virtio_console_control) == 8, "wrong sizeof(struct virtio_console_control)");

enum {
    // max handled length of received console names
    VIRTIO_CONSOLE_CTRL_RECV_MARGIN = 32,
};

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

static void virtqueue_init(struct virtq *vq, size_t num) {
    assert(vq != NULL);
    assert(num > 0);

    vq->num = num;
    vq->desc_meta = zalloc_aligned(sizeof(struct virtq_desc_meta) * num, 1);
    assert(vq->desc_meta != NULL);
    vq->desc = zalloc_aligned(sizeof(struct virtq_desc) * num, 16);
    assert(vq->desc != NULL);
    vq->avail = zalloc_aligned(sizeof(struct virtq_avail) + sizeof(uint16_t) * num, 2);
    assert(vq->avail != NULL);
    vq->used = zalloc_aligned(sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * num, 4);
    assert(vq->used != NULL);
}

static struct virtq *virtio_get_vq(struct virtio_console *con, size_t vqn) {
    struct virtq *vq;
    if (vqn >= con->num_queues) {
        return NULL;
    }
    vq = &con->virtqueues[vqn];
    mutex_lock(&vq->mutex);
    if (vq->num != 0) {
        // already initialized
        mutex_unlock(&vq->mutex);
        return vq;
    }

    con->mmio->queue_sel = vqn;
    if (con->mmio->queue_ready != 0) {
        printf("VIRTIO device already had virtqueue %d initialized; failing.\n", vqn);
        mutex_unlock(&vq->mutex);
        return NULL;
    }
    if (con->mmio->queue_num_max == 0) {
        printf("VIRTIO device queue %d was unexpectedly nonexistent; failing.\n", vqn);
        mutex_unlock(&vq->mutex);
        return NULL;
    }
    size_t num = con->mmio->queue_num_max;
    if (num > 4) {
        num = 4;
    }
    vq->con = con;
    vq->vq_index = vqn;
    virtqueue_init(vq, num);
    con->mmio->queue_num = vq->num;
    con->mmio->queue_desc = (uint64_t) (uintptr_t) vq->desc;
    con->mmio->queue_driver = (uint64_t) (uintptr_t) vq->avail;
    con->mmio->queue_device = (uint64_t) (uintptr_t) vq->used;
    asm volatile("dsb");
    con->mmio->queue_ready = 1;

#ifdef DEBUG_INIT
    printf("VIRTIO queue %d now configured\n", vqn);
#endif

    mutex_unlock(&vq->mutex);
    return vq;
}

// TODO: go back through and add all the missing conversions from LE32 to CPU
bool virtio_transact(struct virtq *vq, struct vector_entry *ents, size_t ent_count, transact_cb cb, void *param) {
    assert(vq != NULL);
    assert(ents != NULL);
    assert(ent_count > 0);
    assert(cb != NULL);

    // printf("beginning transaction on vq=%u...\n", vq->vq_index);

    mutex_lock(&vq->mutex);
    assert(vq->num > 0);

    // first: find a free descriptor table entry
    size_t descriptors[ent_count];
    size_t filled = 0;
    for (size_t i = 0; i < vq->num; i++) {
        if (!vq->desc_meta[i].in_use) {
            descriptors[filled++] = i;
            if (filled >= ent_count) {
                break;
            }
        }
    }
    if (filled < ent_count) {
        printf("VIRTIO: no more descriptors to use in ring buffer for vq=%u\n", vq->vq_index);
        mutex_unlock(&vq->mutex);
        return false;
    }
    assert(filled == ent_count);
    for (size_t i = 0; i < ent_count; i++) {
        size_t desc = descriptors[i];
        assert(desc < vq->num);
        assert(!vq->desc_meta[desc].in_use);
        vq->desc_meta[desc].in_use = true;
        vq->desc_meta[desc].callback = NULL;
        vq->desc_meta[desc].callback_param = NULL;

        vq->desc[desc].addr = (uint64_t) (uintptr_t) ents[i].data_buffer;
        vq->desc[desc].len = (uint32_t) ents[i].length;
        assert((size_t) vq->desc[desc].len == ents[i].length);
        vq->desc[desc].flags = (i < ent_count - 1 ? VIRTQ_DESC_F_NEXT : 0) | (ents[i].is_receive ? VIRTQ_DESC_F_WRITE : 0);
        vq->desc[desc].next = (i < ent_count - 1 ? descriptors[i+1] : 0xFFFF);
    }
    vq->desc_meta[descriptors[0]].callback = cb;
    vq->desc_meta[descriptors[0]].callback_param = param;
    vq->avail->ring[vq->avail->idx % vq->num] = descriptors[0];
    asm volatile("dsb");
    vq->avail->idx++;
    asm volatile("dsb");
    if (!vq->avail->flags) {
        vq->con->mmio->queue_notify = vq->vq_index;
    }

    mutex_unlock(&vq->mutex);
#ifdef DEBUG_VIRTQ
    printf("dispatched transaction on vq=%u.\n", vq->vq_index);
#endif
    return true;
}

struct virtio_transact_sync_status {
    TaskHandle_t wake;
    ssize_t total_length;
};

static void virtio_transact_sync_done(struct virtio_console *con, void *opaque, size_t chain_bytes) {
    (void) con;

    struct virtio_transact_sync_status *status = (struct virtio_transact_sync_status *) opaque;
    assert(status->total_length == -1);
    status->total_length = chain_bytes;
    assert(status->total_length >= 0);
    xTaskNotifyGive(status->wake);
}

ssize_t virtio_transact_sync(struct virtq *vq, struct vector_entry *ents, size_t ent_count) {
    struct virtio_transact_sync_status status = {
        .wake         = xTaskGetCurrentTaskHandle(),
        .total_length = -1,
    };
    if (!virtio_transact(vq, ents, ent_count, virtio_transact_sync_done, &status)) {
        return -1;
    }

    // wait until transaction completes
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    assert(status.total_length >= 0);
    return status.total_length;
}

static void virtio_monitor(struct virtq *vq) {
    assert(vq != NULL);

    mutex_lock(&vq->mutex);
    // only monitor if initialized
    if (vq->num == 0) {
        mutex_unlock(&vq->mutex);
        return;
    }
    mutex_unlock(&vq->mutex);

    // this section doesn't need to be locked (except descriptor manipulation, inside) because it only accesses
    // vq->last_used_idx (only accessed by this function anyway) and vq->used (only accessed here and by the device)
#ifdef DEBUG_VIRTQ
    printf("processing monitor for virtqueue %u\n", vq->vq_index);
#endif
    assert(vq->desc != NULL && vq->used != NULL);
    while (vq->last_used_idx != CPU_TO_LE32(vq->used->idx)) {
        struct virtq_used_elem *elem = &vq->used->ring[vq->last_used_idx%vq->num];
#ifdef DEBUG_VIRTQ
        printf("received used entry %u for virtqueue %u: id=%u, len=%u\n", vq->last_used_idx, vq->vq_index, elem->id, elem->len);
#endif
        size_t chain_bytes = elem->len;
        size_t desc = elem->id;

        transact_cb callback = NULL;
        void *callback_param = NULL;

        mutex_lock(&vq->mutex);
        for (;;) {
            assert(desc < vq->num);
            assert(vq->desc_meta[desc].in_use == true);
            vq->desc_meta[desc].in_use = false;
            if (callback == NULL) {
                callback = vq->desc_meta[desc].callback;
                callback_param = vq->desc_meta[desc].callback_param;
                assert(callback != NULL);
                // zero out state for safety
                vq->desc_meta[desc].callback = NULL;
                vq->desc_meta[desc].callback_param = NULL;
            } else {
                assert(vq->desc_meta[desc].callback == NULL);
                assert(vq->desc_meta[desc].callback_param == NULL);
            }
            // zero out state for safety
            vq->desc[desc].addr = 0xFFFFFFFF;
            vq->desc[desc].len = 0;
            // continue if additional elements in chain
            if (vq->desc[desc].flags & VIRTQ_DESC_F_NEXT) {
                desc = vq->desc[desc].next;
            } else {
                break;
            }
        }
        mutex_unlock(&vq->mutex);

        vq->last_used_idx++;
        assert(callback != NULL);

#ifdef DEBUG_VIRTQ
        printf("calling monitor callback.\n");
#endif
        callback(vq->con, callback_param, chain_bytes);
    }
}

static void virtio_monitor_loop(void *opaque_con) {
    assert(opaque_con != NULL);
    struct virtio_console *con = (struct virtio_console *) opaque_con;

    for (;;) {
        // wait for event
#ifdef DEBUG_VIRTQ
        printf("waiting for monitor event...\n");
#endif
        BaseType_t value;
        value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        assert(value != 0);

        // process event
#ifdef DEBUG_VIRTQ
        printf("processing monitor event...\n");
#endif
        for (size_t i = 0; i < con->num_queues; i++) {
            virtio_monitor(&con->virtqueues[i]);
        }
    }
}

static void virtio_irq_callback(void *opaque_con) {
    assert(opaque_con != NULL);
    struct virtio_console *con = (struct virtio_console *) opaque_con;

    BaseType_t was_woken = pdFALSE;
    uint32_t status = con->mmio->interrupt_status;
    if (status & VIRTIO_IRQ_BIT_USED_BUFFER) {
        vTaskNotifyGiveFromISR(con->monitor_task, &was_woken);
    }
    con->mmio->interrupt_ack = status;
    portYIELD_FROM_ISR(was_woken);
}

static void receive_ctrl_cb(struct virtio_console *con, void *opaque, size_t chain_bytes) {
    assert(opaque != NULL);
    struct virtio_console_control *ctrl_recv = (struct virtio_console_control *) opaque;

#ifdef DEBUG_VIRTQ
    printf("received CTRL message on queue: id=%u, event=%u, value=%u (chain_bytes=%u)\n", ctrl_recv->id, ctrl_recv->event, ctrl_recv->value, chain_bytes);
#endif

    if (ctrl_recv->event == VIRTIO_CONSOLE_DEVICE_ADD) {
        assert(chain_bytes == sizeof(struct virtio_console_control));

        struct virtio_console_port *port = malloc(sizeof(struct virtio_console_port));
        assert(port != NULL);
        size_t base_queue = (ctrl_recv->id == 0 ? 0 : 2 + ctrl_recv->id * 2);
        port->console   = con;
        port->port_num  = ctrl_recv->id;
        port->receiveq  = virtio_get_vq(con, base_queue + 0);
        port->transmitq = virtio_get_vq(con, base_queue + 1);

        con->callback(con->callback_param, port);
    } else if (ctrl_recv->event == VIRTIO_CONSOLE_PORT_NAME) {
        assert(chain_bytes > sizeof(struct virtio_console_control));
        size_t n = chain_bytes - sizeof(struct virtio_console_control);
        if (n > VIRTIO_CONSOLE_CTRL_RECV_MARGIN) {
            n = VIRTIO_CONSOLE_CTRL_RECV_MARGIN;
        }

        char name[VIRTIO_CONSOLE_CTRL_RECV_MARGIN + 1];
        memcpy(name, ctrl_recv + 1, n);
        name[n] = '\0';

        printf("VIRTIO device name: '%s' (%u)\n", name, n);
    } else if (ctrl_recv->event == VIRTIO_CONSOLE_PORT_OPEN) {
        assert(chain_bytes == sizeof(struct virtio_console_control));
        assert(ctrl_recv->value == 1);
    } else {
        printf("UNHANDLED event: ctrl event %u\n", ctrl_recv->event);
    }

    memset(ctrl_recv, 0, sizeof(struct virtio_console_control));

    struct vector_entry ents_recv = {
        .data_buffer = ctrl_recv,
        .length = sizeof(struct virtio_console_control),
        .is_receive = true,
    };

    struct virtq *ctrl_rx_q = virtio_get_vq(con, VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_RECEIVE);
    bool ok = virtio_transact(ctrl_rx_q, &ents_recv, 1, receive_ctrl_cb, ctrl_recv);
    assert(ok);
}

static void transmit_port_ready_cb(struct virtio_console *con, void *opaque, size_t chain_bytes) {
    assert(opaque != NULL);
    free(opaque);
    (void) con;

#ifdef DEBUG_VIRTQ
    printf("completed transmit of PORT_READY message on CONSOLE device: chain_bytes=%u\n", chain_bytes);
#endif
    assert(chain_bytes == 0);
}

void virtio_serial_ready(struct virtio_console_port *port) {
    assert(port != NULL);

    struct virtq *ctrl_tx_q = virtio_get_vq(port->console, VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_TRANSMIT);

    for (int event = 0; event < 2; event++) {
        struct virtio_console_control *ctrl = malloc(sizeof(struct virtio_console_control));
        assert(ctrl != NULL);
        ctrl->id = port->port_num;
        ctrl->event = (event == 0) ? VIRTIO_CONSOLE_PORT_READY : VIRTIO_CONSOLE_PORT_OPEN;
        ctrl->value = 1;

        struct vector_entry ents = {
            .data_buffer = ctrl,
            .length = sizeof(struct virtio_console_control),
            .is_receive = false,
        };
        bool ok = virtio_transact(ctrl_tx_q, &ents, 1, transmit_port_ready_cb, ctrl);
        assert(ok);
    }
}

static void transmit_ready_cb(struct virtio_console *con, void *opaque, size_t chain_bytes) {
    assert(opaque != NULL);
    free(opaque);
    (void) con;

#ifdef DEBUG_VIRTQ
    printf("completed transmit of ready message on CONSOLE device: chain_bytes=%u\n", chain_bytes);
#endif
    assert(chain_bytes == 0);
}

void virtio_init_console(virtio_port_cb callback, void *param, uintptr_t mem_addr, uint32_t irq) {
    struct virtio_mmio_registers *mmio = (struct virtio_mmio_registers *) mem_addr;
    struct virtio_console *con = malloc(sizeof(struct virtio_console));
    assert(con != NULL);

    con->mmio = mmio;
    con->config = (struct virtio_console_config *) (mmio + 1);
    con->irq = irq;
    con->callback = callback;
    con->callback_param = param;

#ifdef DEBUG_INIT
    printf("VIRTIO device: addr=%x, irq=%u\n", mem_addr, irq);
#endif

    if (LE32_TO_CPU(mmio->magic_value) != VIRTIO_MAGIC_VALUE) {
        printf("VIRTIO device had the wrong magic number: 0x%08x instead of 0x%08x; not initializing.\n",
               LE32_TO_CPU(mmio->magic_value), VIRTIO_MAGIC_VALUE);
        return;
    }

    if (LE32_TO_CPU(mmio->version) == VIRTIO_LEGACY_VERSION) {
        printf("VIRTIO device configured as legacy-only; cannot initialize.\n"
               "Set -global virtio-mmio.force-legacy=false to fix this.\n");
        return;
    } else if (LE32_TO_CPU(mmio->version) != VIRTIO_VERSION) {
        printf("VIRTIO device version not recognized: found %u instead of %u\n",
               LE32_TO_CPU(mmio->version), VIRTIO_VERSION);
        return;
    }

    // make sure this is a serial port
    if (LE32_TO_CPU(mmio->device_id) != VIRTIO_CONSOLE_ID) {
#ifdef DEBUG_INIT
        printf("VIRTIO device ID=%u instead of CONSOLE (%u)\n",
               LE32_TO_CPU(mmio->device_id), VIRTIO_CONSOLE_ID);
#endif
        return;
    }

    // reset the device
    mmio->status = CPU_TO_LE32(0);

    // acknowledge the device
    mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_ACKNOWLEDGE);
    mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_DRIVER);

    // read the feature bits
    mmio->device_features_sel = CPU_TO_LE32(0);
    uint64_t features = CPU_TO_LE32(mmio->device_features);
    mmio->device_features_sel = CPU_TO_LE32(1);
    features |= ((uint64_t) CPU_TO_LE32(mmio->device_features)) << 32;

    // select feature bits
    if (!(features & VIRTIO_F_VERSION_1)) {
        printf("VIRTIO device featureset (0x%016x) does not include VIRTIO_F_VERSION_1 (0x%016x).\n"
               "Legacy devices are not supported.\n", features, VIRTIO_F_VERSION_1);
        mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_FAILED);
        return;
    }
    if (!(features & VIRTIO_CONSOLE_F_MULTIPORT)) {
        printf("VIRTIO device featureset (0x%016x) does not include VIRTIO_CONSOLE_F_MULTIPORT (0x%016x).\n"
               "This configuration is not yet supported.\n", features, VIRTIO_CONSOLE_F_MULTIPORT);
        mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_FAILED);
        return;
    }

    // write selected bits back
    uint64_t selected_features = VIRTIO_F_VERSION_1 | VIRTIO_CONSOLE_F_MULTIPORT;
    mmio->driver_features_sel = CPU_TO_LE32(0);
    mmio->driver_features = CPU_TO_LE32((uint32_t) selected_features);
    mmio->driver_features_sel = CPU_TO_LE32(1);
    mmio->driver_features = CPU_TO_LE32((uint32_t) (selected_features >> 32));

    // validate features
    mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_FEATURES_OK);
    if (!(LE32_TO_CPU(mmio->status) & VIRTIO_DEVSTAT_FEATURES_OK)) {
        printf("VIRTIO device did not set FEATURES_OK: read back status=%08x.\n", mmio->status);
        mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_FAILED);
        return;
    }

#ifdef DEBUG_INIT
    printf("Maximum number of ports supported by VIRTIO device: %d\n", con->config->max_nr_ports);
#endif

    uint32_t virtqueues = (con->config->max_nr_ports + 1) * 2;
    con->num_queues = virtqueues;
    con->virtqueues = malloc(sizeof(struct virtq) * virtqueues);
    assert(con->virtqueues != NULL);

    for (uint32_t vq = 0; vq < virtqueues; vq++) {
        con->virtqueues[vq].num = 0;
        mutex_init(&con->virtqueues[vq].mutex);
    }

    assert(con->monitor_task == NULL);
    BaseType_t status;
    status = xTaskCreate(virtio_monitor_loop, "virtio-monitor", 1000, con, PRIORITY_DRIVERS, &con->monitor_task);
    if (status != pdPASS) {
        printf("could not initialize virtio-monitor task; failed.\n");
        mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_FAILED);
        return;
    }
    assert(con->monitor_task != NULL);

    enable_irq(irq, virtio_irq_callback, con);

    // enable driver
    mmio->status |= CPU_TO_LE32(VIRTIO_DEVSTAT_DRIVER_OK);

    // set up control queues
    struct virtq *ctrl_rx_q = virtio_get_vq(con, VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_RECEIVE);
    struct virtq *ctrl_tx_q = virtio_get_vq(con, VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_TRANSMIT);

    // initialize receive request for control queue first, so that replies don't get dropped
    for (int i = 0; i < 4; i++) {
        struct virtio_console_control *ctrl_recv = zalloc_aligned(sizeof(struct virtio_console_control) + VIRTIO_CONSOLE_CTRL_RECV_MARGIN, 1);
        assert(ctrl_recv != NULL);
        struct vector_entry ents_recv = {
            .data_buffer = ctrl_recv,
            .length = sizeof(struct virtio_console_control) + VIRTIO_CONSOLE_CTRL_RECV_MARGIN,
            .is_receive = true,
        };

        bool ok = virtio_transact(ctrl_rx_q, &ents_recv, 1, receive_ctrl_cb, ctrl_recv);
        assert(ok);
    }

    // now request initialization
    struct virtio_console_control *ctrl = malloc(sizeof(struct virtio_console_control));
    assert(ctrl != NULL);
    ctrl->id = 0xFFFFFFFF;
    ctrl->event = VIRTIO_CONSOLE_DEVICE_READY;
    ctrl->value = 1;

    struct vector_entry ents = {
        .data_buffer = ctrl,
        .length = sizeof(struct virtio_console_control),
        .is_receive = false,
    };
    bool ok = virtio_transact(ctrl_tx_q, &ents, 1, transmit_ready_cb, ctrl);
    assert(ok);
}

static bool virtio_initialized = false;

void virtio_init(virtio_port_cb callback, void *param) {
    assert(!virtio_initialized);
    virtio_initialized = true;

    for (size_t n = 0; n < VIRTIO_MMIO_REGION_NUM; n++) {
        virtio_init_console(callback, param,
                            VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * n,
                            VIRTIO_MMIO_IRQS_BASE + n);
    }
}
