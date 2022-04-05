#ifndef FSW_VIVID_RTOS_GIC_H
#define FSW_VIVID_RTOS_GIC_H

enum {
    IRQ_SGI_BASE = 0,  // software-generated interrupts
    IRQ_PPI_BASE = 16, // private peripheral interrupt (replicated in entrypoint.s)
    IRQ_SPI_BASE = 32, // shared peripheral interrupt
};

typedef void (*gic_callback_t)(void *);

void shutdown_gic(void);
void enable_irq(uint32_t irq, gic_callback_t callback, void *param);

#endif /* FSW_VIVID_RTOS_GIC_H */
