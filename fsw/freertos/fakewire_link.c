#include <rtos/virtio.h>
#include <hal/debug.h>
#include <bus/link.h>

static bool fakewire_link_attached = false;

void fakewire_link_init_check(const fw_link_options_t *options) {
    assert(options->flags == FW_FLAG_VIRTIO); // only option supported on FreeRTOS

    // ensure that this is only called once, because multiple VIRTIO configurations for the same memory would conflict
    assert(!fakewire_link_attached);
    fakewire_link_attached = true;
}
