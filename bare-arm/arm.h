#ifndef BARE_ARM_ARM_H
#define BARE_ARM_ARM_H

#include <stdint.h>

enum {
    ARM_TIMER_ENABLE  = 0x00000001,
    ARM_TIMER_IMASK   = 0x00000002,
    ARM_TIMER_ISTATUS = 0x00000004,
};

/** Physical Timer Control Register (CNTP_CTL) **/
static inline void arm_set_cntp_ctl(uint32_t v) {
    asm("MCR p15, 0, %0, c14, c2, 1" : : "r" (v));
}

static inline uint32_t arm_get_cntp_ctl(void) {
    uint32_t v;
    asm("MRC p15, 0, %0, c14, c2, 1" : "=r" (v));
    return v;
}

/** Physical Timer CompareValue Register (CNTP_CVAL) **/
static inline void arm_set_cntp_cval(uint64_t v) {
    uint32_t v_low, v_high;
    v_low = (uint32_t) v;
    v_high = (uint32_t) (v >> 32);
    asm("MCRR p15, 2, %0, %1, c14" : : "r" (v_low), "r" (v_high));
}

static inline uint64_t arm_get_cntp_cval(void) {
    uint32_t v_low, v_high;
    asm("MRRC p15, 2, %0, %1, c14" : "=r" (v_low), "=r" (v_high));
    return ((uint64_t) v_high << 32) | v_low;
}

/** Counter Frequency Register (CNTFRQ) **/
static inline uint32_t arm_get_cntfrq(void) {
    uint32_t v;
    asm("MRC p15, 0, %0, c14, c0, 0" : "=r" (v));
    return v;
}

/** Physical Counter Register (CNTPCT) **/
static inline uint64_t arm_get_cntpct(void) {
    uint32_t v_low, v_high;
    asm("MRRC p15, 0, %0, %1, c14" : "=r" (v_low), "=r" (v_high));
    return ((uint64_t) v_high << 32) | v_low;
}

#endif /* BARE_ARM_ARM_H */
