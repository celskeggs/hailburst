#include <endian.h>

#include <rtos/virtio.h>

enum {
    VIRTIO_MAGIC_VALUE    = 0x74726976,
    VIRTIO_LEGACY_VERSION = 1,
    VIRTIO_VERSION        = 2,
};

enum {
    VIRTIO_DEVSTAT_ACKNOWLEDGE        = 1,
    VIRTIO_DEVSTAT_DRIVER             = 2,
    VIRTIO_DEVSTAT_DRIVER_OK          = 4,
    VIRTIO_DEVSTAT_FEATURES_OK        = 8,
    VIRTIO_DEVSTAT_DEVICE_NEEDS_RESET = 64,
    VIRTIO_DEVSTAT_FAILED             = 128,
};

// TODO: go back through and add all the missing conversions from LE32 to CPU

// this function runs during STAGE_RAW, so it had better not use any kernel registration facilities
void virtio_device_init_internal(virtio_device_t *device) {
    assert(device != NULL);
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

    // enable driver
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER_OK);
}

void *virtio_device_config_space(virtio_device_t *device) {
    assert(device != NULL && device->mmio != NULL);
    return device->mmio + 1;
}

void virtio_device_setup_queue_internal(struct virtio_mmio_registers *mmio, uint32_t queue_index, size_t queue_num,
                                        struct virtq_desc *desc, struct virtq_avail *avail, struct virtq_used *used) {
    assert(mmio != NULL && queue_num > 0 && desc != NULL && avail != NULL && used != NULL);

    mmio->queue_sel = queue_index;
    if (mmio->queue_ready != 0) {
        abortf("VIRTIO device apparently already had virtqueue %d initialized; failing.", queue_index);
    }
    if (mmio->queue_num_max == 0) {
        abortf("VIRTIO device does not have queue %u that it was expected to have.", queue_index);
    }

    if (queue_num > mmio->queue_num_max) {
        abortf("VIRTIO device supports up to %u entries in a queue buffer, but max flow is %u.",
               mmio->queue_num_max, queue_num);
    }

    mmio->queue_num = queue_num;

    mmio->queue_desc   = (uint64_t) (uintptr_t) desc;
    mmio->queue_driver = (uint64_t) (uintptr_t) avail;
    mmio->queue_device = (uint64_t) (uintptr_t) used;

    atomic_store(mmio->queue_ready, 1);

    debugf(DEBUG, "VIRTIO queue %d now configured", queue_index);
}
