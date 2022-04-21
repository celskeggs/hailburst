#ifndef FSW_VIVID_RTOS_ARM_H
#define FSW_VIVID_RTOS_ARM_H

#include <stdint.h>

enum {
    ARM_TIMER_ENABLE  = 0x00000001,
    ARM_TIMER_IMASK   = 0x00000002,
    ARM_TIMER_ISTATUS = 0x00000004,
};

enum {
    ARM_FPEXC_EN = 0x40000000, // enable bit

    ARM_CPSR_MASK_INTERRUPTS = 0x80,
    ARM_CPSR_MASK_MODE       = 0x1F,

    ARM_USER_MODE = 0x10,
    ARM_IRQ_MODE  = 0x12,
    ARM_SYS_MODE  = 0x1F,

    ARM_CPACR_CP10_FULL_ACCESS = 0x300000, // enable CP10 with full access
    ARM_CPACR_CP11_FULL_ACCESS = 0xC00000, // enable CP11 with full access
};

/** Current Program Status Register (CPSR) **/
static inline uint32_t arm_get_cpsr(void) {
    uint32_t v;
    asm volatile("MRS %0, CPSR" : "=r" (v));
    return v;
}

/** Saved Program Status Register (SPSR) **/
static inline uint32_t arm_get_spsr(void) {
    uint32_t v;
    asm volatile("MRS %0, SPSR" : "=r" (v));
    return v;
}

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

/** Coprocessor Access Control Register (CPACR) **/
static inline void arm_set_cpacr(uint32_t v) {
    asm("MCR p15, 0, %0, c1, c0, 2" : : "r" (v));
}

static inline uint32_t arm_get_cpacr(void) {
    uint32_t v;
    asm("MRC p15, 0, %0, c1, c0, 2" : "=r" (v));
    return v;
}

/** Floating-Point Exception Control register (FPEXC) **/
static inline void arm_set_fpexc(uint32_t v) {
    asm("VMSR FPEXC, %0" : : "r" (v));
}

static inline uint32_t arm_get_fpexc(void) {
    uint32_t v;
    asm("VMRS %0, FPEXC" : "=r" (v));
    return v;
}

#endif /* FSW_VIVID_RTOS_ARM_H */
