#include <stdarg.h>

#include <rtos/arm.h>
#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/debug.h>

enum {
    // address constants for writing to the serial port
    SERIAL_BASE          = 0x09000000,
    SERIAL_FLAG_REGISTER = 0x18,
    SERIAL_BUFFER_FULL   = (1 << 5),

    // three bytes unlikely to show up frequently in common data to serialize
    DEBUG_ESCAPE_BYTE   = 0xA7,
    DEBUG_SEGMENT_START = 0xA9,
    DEBUG_SEGMENT_END   = 0xAF,
};

static void emit(uint8_t c) {
    /* Wait until the serial buffer is empty */
    while (atomic_load_relaxed(*(uint32_t*)(SERIAL_BASE + SERIAL_FLAG_REGISTER)) & SERIAL_BUFFER_FULL) {}
    /* Put our character, c, into the serial buffer */
    atomic_store_relaxed(*(uint32_t*)SERIAL_BASE, c);
}

static void debug_write_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == DEBUG_ESCAPE_BYTE || data[i] == DEBUG_SEGMENT_START || data[i] == DEBUG_SEGMENT_END) {
            emit(DEBUG_ESCAPE_BYTE);
            emit(data[i] ^ 0x80); // flip highest bit to change into regularly-allowed data
        } else {
            emit(data[i]);
        }
    }
}

void debugf_internal(const void **data_sequences, const size_t *data_sizes, size_t data_num) {
    uint32_t cpsr = arm_get_cpsr();
    // if interrupts are not already disabled, then disable them
    // (this is necessary to ensure that output is coherent)
    if (!(cpsr & ARM_CPSR_MASK_INTERRUPTS)) {
        asm volatile("CPSID i" ::: "memory");
    }
    // emit output
    emit(DEBUG_SEGMENT_START);
    for (size_t i = 0; i < data_num; i++) {
        debug_write_bytes(data_sequences[i], data_sizes[i]);
    }
    emit(DEBUG_SEGMENT_END);
    // if we disabled interrupts, then re-enable them
    if (!(cpsr & ARM_CPSR_MASK_INTERRUPTS)) {
        asm volatile("CPSIE i" ::: "memory");
    }
}
