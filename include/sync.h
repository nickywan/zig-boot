#ifndef SYNC_H
#define SYNC_H

#include "types.h"

// Spinlock
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

// Atomic
typedef struct {
    volatile int counter;
} atomic_t;

#define ATOMIC_INIT(val) { (val) }

int atomic_read(atomic_t *v);
void atomic_set(atomic_t *v, int val);
void atomic_inc(atomic_t *v);
void atomic_dec(atomic_t *v);
int atomic_inc_return(atomic_t *v);

// CPU operations
static inline void cpu_relax(void) {
    __asm__ volatile("pause" ::: "memory");
}

static inline void cpu_halt(void) {
    __asm__ volatile("hlt");
}

#endif
