#include "../include/sync.h"

// Spinlock implementation
void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        while (lock->lock) cpu_relax();
    }
}

void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

// Atomic operations
int atomic_read(atomic_t *v) {
    return __sync_fetch_and_add(&v->counter, 0);
}

void atomic_set(atomic_t *v, int val) {
    v->counter = val;
}

void atomic_inc(atomic_t *v) {
    __sync_add_and_fetch(&v->counter, 1);
}

void atomic_dec(atomic_t *v) {
    __sync_sub_and_fetch(&v->counter, 1);
}

int atomic_inc_return(atomic_t *v) {
    return __sync_add_and_fetch(&v->counter, 1);
}
