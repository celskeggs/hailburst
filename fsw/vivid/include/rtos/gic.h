#ifndef FSW_VIVID_RTOS_GIC_H
#define FSW_VIVID_RTOS_GIC_H

enum {
    IRQ_SGI_BASE = 0,  // software-generated interrupts
    IRQ_PPI_BASE = 16, // private peripheral interrupt (replicated in entrypoint.s)
    IRQ_SPI_BASE = 32, // shared peripheral interrupt
};

void shutdown_gic(void);
void gic_validate_ready(void);

#endif /* FSW_VIVID_RTOS_GIC_H */
