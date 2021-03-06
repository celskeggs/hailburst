#include <rtos/arm.h>
#include <rtos/gic.h>
#include <rtos/scheduler.h>
#include <hal/atomic.h>
#include <hal/init.h>

enum {
    GIC_DIST_ADDR = 0x08000000,
    GIC_CPU_ADDR  = 0x08010000,

    IRQ_PHYS_TIMER = IRQ_PPI_BASE + 14,
};

struct gic_dist_reg {
    volatile uint32_t gicd_ctlr;             // Distributor Control Register
    volatile uint32_t gicd_typer;            // Interrupt Controller Type Register
    volatile uint32_t gicd_iidr;             // Distributor Implementer Identification Register (broken in QEMU)
             uint32_t RESERVED0[29];
    volatile uint32_t gicd_igroupr[32];      // Interrupt Group Registers
    volatile uint32_t gicd_isenabler[32];    // Interrupt Set-Enable Registers
    volatile uint32_t gicd_icenabler[32];    // Interrupt Clear-Enable Registers
    volatile uint32_t gicd_ispendr[32];      // Interrupt Set-Pending Registers
    volatile uint32_t gicd_icpendr[32];      // Interrupt Clear-Pending Registers
    volatile uint32_t gicd_isactiver[32];    // GICv2 Interrupt Set-Active Registers
    volatile uint32_t gicd_icactiver[32];    // Interrupt Clear-Active Registers
    volatile uint8_t  gicd_ipriorityr[1020]; // Interrupt Priority Registers
             uint32_t RESERVED1;
    volatile uint8_t  gicd_itargetsr[1020];  // Interrupt Processor Targets Registers
             uint32_t RESERVED2;
    volatile uint32_t gicd_icfgr[64];        // Interrupt Configuration Registers
             uint32_t RESERVED3[64];
    volatile uint32_t gicd_nsacr[64];        // Non-secure Access Control Registers
    volatile uint32_t gicd_sigr;             // Software Generated Interrupt Register
             uint32_t RESERVED4[3];
    volatile uint8_t  gicd_cpendsgir[16];    // SGI Clear-Pending Registers
    volatile uint8_t  gicd_spendsgir[16];    // SGI Set-Pending Registers
             uint32_t RESERVED5[52];
};
static_assert(sizeof(struct gic_dist_reg) == 0x1000, "invalid sizeof(gic_dist_reg)");

struct gic_cpu_reg {
    volatile uint32_t gicc_ctlr;             // CPU Interface Control Register
    volatile uint32_t gicc_pmr;              // Interrupt Priority Mask Register
    volatile uint32_t gicc_bpr;              // Binary Point Register
    volatile uint32_t gicc_iar;              // Interrupt Acknowledge Register
    volatile uint32_t gicc_eoir;             // End of Interrupt Register
    volatile uint32_t gicc_rpr;              // Running Priority Register
    volatile uint32_t gicc_hppir;            // Highest Priority Pending Interrupt Register
    volatile uint32_t gicc_abpr;             // Aliased Binary Point Register
    volatile uint32_t gicc_aiar;             // Aliased Interrupt Acknowledge Register
    volatile uint32_t gicc_aeoir;            // Aliased End of Interrupt Register
    volatile uint32_t gicc_ahppir;           // Aliased Highest Priority Pending Interrupt Register
             uint32_t RESERVED0[41];
    volatile uint32_t gicc_apr[4];           // Active Priorities Registers
    volatile uint32_t gicc_nsapr[4];         // Non-secure Active Priorities Registers
             uint32_t RESERVED1[3];
    volatile uint32_t gicc_iidr;             // CPU Interface Identification Register
             uint32_t RESERVED2[960];
    volatile uint32_t gicc_dir;              // Deactivate Interrupt Register
             uint32_t RESERVED3[1023];
};
static_assert(sizeof(struct gic_cpu_reg) == 0x2000, "invalid sizeof(gic_cpu_reg)");

static struct gic_dist_reg * const dist = (struct gic_dist_reg *) GIC_DIST_ADDR;
static struct gic_cpu_reg  * const cpu  = (struct gic_cpu_reg *)  GIC_CPU_ADDR;

static uint32_t num_interrupts = 0;

void shutdown_gic(void) {
    dist->gicd_ctlr = 0;
    cpu->gicc_ctlr = 0;
}

void gic_validate_ready(void) {
    uint32_t irq = IRQ_PHYS_TIMER;
    uint32_t off = irq / 32;
    uint32_t mask = 1 << (irq % 32);

    assertf((dist->gicd_isactiver[off] & mask) == 0 && (dist->gicd_ispendr[off] & mask) == 0
                                                    && (dist->gicd_isenabler[off] & mask) == mask,
            "GIC misconfigured for regular execution: ICACTIVER=0x%x, ICPENDR=0x%x, ISENABLER=0x%x, mask=0x%08x",
            dist->gicd_isactiver[off], dist->gicd_ispendr[off], dist->gicd_isenabler[off], mask);
}

static void configure_gic(void) {
    num_interrupts = ((dist->gicd_typer & 0x1F) + 1) * 32;

    // disable forwarding of pending interrupts
    dist->gicd_ctlr = 0;
    cpu->gicc_ctlr = 0;

    // reset all GICD state
    for (uint32_t rn = 0; rn * 32 < num_interrupts; rn++) {
        dist->gicd_igroupr[rn]   = 0x00000000; // all group zero
        dist->gicd_icenabler[rn] = 0xFFFFFFFF; // disable everything (if possible)
        dist->gicd_icpendr[rn]   = 0xFFFFFFFF; // clear all pending bits
        dist->gicd_icactiver[rn] = 0xFFFFFFFF; // clear all active bits
        dist->gicd_icfgr[rn]     = 0x00000000; // set everything to level-sensitive
    }

    for (uint32_t i = 0; i < 16; i++) {
        dist->gicd_cpendsgir[i] = 0xFF; // clear all pending SGIs
    }

    for (uint32_t i = 0; i < num_interrupts; i++) {
        dist->gicd_ipriorityr[i] = 0xFF; // set to lowest priority
        dist->gicd_itargetsr[i]  = 1;    // send to only CPU 0 (only applicable to SMP, which we aren't using)
    }

    // reset all GICC state
    cpu->gicc_pmr  = 255; // unmask all interrupt priorities
    cpu->gicc_bpr  = 0; // enable interrupt preemption (as required by FreeRTOS)
    cpu->gicc_abpr = 0; // enable group 1 interrupt preemption

    // enable forwarding of pending interrupts
    asm volatile("dsb\nisb\n" ::: "memory");  // TODO: is this really needed?
    dist->gicd_ctlr = 1;
    cpu->gicc_ctlr = 1;

    // enable timer interrupt
    assert(IRQ_PHYS_TIMER < num_interrupts);

    debugf(DEBUG, "Enabling tick IRQ.");

    uint32_t irq = IRQ_PHYS_TIMER;
    uint32_t off = irq / 32;
    uint32_t mask = 1 << (irq % 32);

    dist->gicd_icfgr[off]     &= ~mask; // set level-sensitive
    dist->gicd_icactiver[off]  = mask;  // clear active bit
    dist->gicd_icpendr[off]    = mask;  // clear pending bit
    dist->gicd_ipriorityr[irq] = 0xF0;  // set priority allowing FreeRTOS calls
    dist->gicd_isenabler[off]  = mask;  // enable IRQ
}
// we don't need to worry about exactly when this happens, because interrupts will be disabled by the bootrom, and not
// re-enabled until the initialization is complete.
PROGRAM_INIT(STAGE_RAW, configure_gic);

void gic_interrupt_handler(void) {
    // Make sure that we were executing normal user code.
    uint32_t spsr = arm_get_spsr();
    assertf((spsr & ARM_CPSR_MASK_MODE) == ARM_SYS_MODE,
            "SPSR indicated interrupted code was not in SYS_MODE: 0x%08x", spsr);

    // Confirm that this was the expected interrupt, and acknowledge it.
    uint32_t irq = atomic_load_relaxed(cpu->gicc_iar);
    assertf(irq == IRQ_PHYS_TIMER,
            "GIC encountered IRQ %u, which is not the timer interrupt and should be disabled.", irq);

    atomic_store_relaxed(cpu->gicc_eoir, irq);

    // Switch to the next task.
    schedule_next_clip();
}
