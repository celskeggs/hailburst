#ifndef HAL_ATOMIC_H
#define HAL_ATOMIC_H

// we can use the builtins available in GCC to make this the same across both FreeRTOS and Linux

#define atomic_load(x) (__atomic_load_n(&(x), __ATOMIC_ACQ_REL))
#define atomic_store(x, v) (__atomic_store_n(&(x), (v), __ATOMIC_ACQ_REL))

#define atomic_load_relaxed(x) (__atomic_load_n(&(x), __ATOMIC_RELAXED))
#define atomic_store_relaxed(x, v) (__atomic_store_n(&(x), (v), __ATOMIC_RELAXED))

#endif /* HAL_ATOMIC_H */
