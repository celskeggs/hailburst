#include <flight/radio.h>

bool radio_validate_common_config(uint32_t config_data[3]) {
    if (config_data[0] != RADIO_MAGIC) {
        debugf(CRITICAL, "Invalid magic number 0x%08x when 0x%08x was expected.", config_data[0], RADIO_MAGIC);
        return false;
    }
    if (config_data[1] != RADIO_MEM_BASE_ADDR) {
        debugf(CRITICAL, "Invalid base address 0x%08x when 0x%08x was expected.", config_data[1], RADIO_MEM_BASE_ADDR);
        return false;
    }
    if (config_data[2] != RADIO_MEM_SIZE) {
        debugf(CRITICAL, "Invalid memory size 0x%08x when 0x%08x was expected.", config_data[2], RADIO_MEM_SIZE);
        return false;
    }
    return true;
}
