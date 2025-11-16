#ifndef SMP_H
#define SMP_H

#include "types.h"

#define MAX_CPUS 16

// APIC registers
#define APIC_BASE_MSR 0x1B
#define APIC_ID_REG 0x20
#define APIC_ICR_LOW 0x300
#define APIC_ICR_HIGH 0x310

// ICR delivery modes
#define APIC_ICR_INIT 0x00000500
#define APIC_ICR_STARTUP 0x00000600
#define APIC_ICR_LEVEL_ASSERT 0x00004000
#define APIC_ICR_TRIGGER_LEVEL 0x00008000

void smp_init(void);
void smp_boot_aps(void);
int smp_get_cpu_count(void);
int smp_processor_id(void);

// Per-CPU callback
typedef void (*smp_call_func_t)(void *info);
void on_each_cpu(smp_call_func_t func, void *info);

#endif
