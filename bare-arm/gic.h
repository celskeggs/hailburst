#ifndef BARE_ARM_GIC_H
#define BARE_ARM_GIC_H

typedef void (*gic_callback_t)(void);

void configure_gic(void);
void shutdown_gic(void);
void enable_irq(uint32_t irq, gic_callback_t callback);

#endif /* BARE_ARM_GIC_H */