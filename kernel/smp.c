#include "../include/smp.h"
#include "../include/acpi.h"
#include "../include/serial.h"
#include "../include/sync.h"
#include "../include/types.h"

// MSR operations
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// APIC MMIO read/write
static volatile uint32_t *apic_base = NULL;

static inline uint32_t apic_read(uint32_t reg) {
    return apic_base[reg >> 2];
}

static inline void apic_write(uint32_t reg, uint32_t value) {
    apic_base[reg >> 2] = value;
}

// SMP state
static int cpu_count = 0;
static uint8_t cpu_ids[MAX_CPUS];
static atomic_t cpus_booted = ATOMIC_INIT(0);
static volatile int current_cpu_id = 0;

// Per-CPU data
static int per_cpu_ids[MAX_CPUS];

// AP entry point (called from trampoline)
extern void ap_entry_point(void);

void smp_init(void) {
    // Get APIC base address from MSR
    uint64_t apic_msr = rdmsr(APIC_BASE_MSR);
    apic_base = (volatile uint32_t*)(apic_msr & 0xFFFFF000);

    serial_printf("[SMP] LAPIC base: 0x%lx\n", (uint64_t)apic_base);

    // Enable LAPIC
    wrmsr(APIC_BASE_MSR, apic_msr | (1 << 11));

    // Software enable LAPIC (SVR register)
    apic_write(0xF0, apic_read(0xF0) | 0x100);

    // Get BSP APIC ID
    uint32_t bsp_id = apic_read(APIC_ID_REG) >> 24;
    serial_printf("[SMP] BSP APIC ID: %d\n", bsp_id);

    // Get CPU count from ACPI
    cpu_count = acpi_get_cpu_count();
    for (int i = 0; i < cpu_count; i++) {
        cpu_ids[i] = acpi_get_apic_id(i);
    }

    // BSP is CPU 0
    per_cpu_ids[0] = 0;
    atomic_inc(&cpus_booted);
}

// Send INIT IPI
static void send_init_ipi(uint8_t apic_id) {
    apic_write(APIC_ICR_HIGH, (uint32_t)apic_id << 24);
    apic_write(APIC_ICR_LOW, APIC_ICR_INIT | APIC_ICR_LEVEL_ASSERT | APIC_ICR_TRIGGER_LEVEL);

    // Wait for delivery
    while (apic_read(APIC_ICR_LOW) & (1 << 12));
}

// Send STARTUP IPI
static void send_startup_ipi(uint8_t apic_id, uint8_t vector) {
    apic_write(APIC_ICR_HIGH, (uint32_t)apic_id << 24);
    apic_write(APIC_ICR_LOW, APIC_ICR_STARTUP | vector);

    // Wait for delivery
    while (apic_read(APIC_ICR_LOW) & (1 << 12));
}

// Delay function (busy wait)
static void delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 100000; i++);
}

void smp_boot_aps(void) {
    // NO SERIAL OUTPUT during SMP boot - it's fragile!

    // Copy trampoline to low memory (0x8000)
    extern char trampoline_start[];
    extern char trampoline_end[];
    uint8_t *trampoline_dest = (uint8_t*)0x8000;
    int trampoline_size = trampoline_end - trampoline_start;

    for (int i = 0; i < trampoline_size; i++) {
        trampoline_dest[i] = trampoline_start[i];
    }

    // Boot each AP
    for (int i = 0; i < cpu_count; i++) {
        uint8_t apic_id = cpu_ids[i];
        uint32_t bsp_id = apic_read(APIC_ID_REG) >> 24;

        if (apic_id == bsp_id) {
            continue;  // Skip BSP
        }

        current_cpu_id = i;

        // INIT-SIPI-SIPI sequence
        send_init_ipi(apic_id);
        delay_ms(10);

        send_startup_ipi(apic_id, 0x08);  // Vector 0x08 = 0x8000
        delay_ms(1);

        send_startup_ipi(apic_id, 0x08);
        delay_ms(10);

        // Wait for AP to boot (with timeout)
        int timeout = 1000;
        int initial_count = atomic_read(&cpus_booted);
        while (atomic_read(&cpus_booted) == initial_count && timeout-- > 0) {
            delay_ms(1);
        }
    }

    // NOW we can print - all APs are booted
    serial_printf("[SMP] Boot complete: %d/%d CPUs online\n",
                  atomic_read(&cpus_booted), cpu_count);
}

int smp_get_cpu_count(void) {
    return cpu_count;
}

int smp_processor_id(void) {
    uint32_t apic_id = apic_read(APIC_ID_REG) >> 24;

    for (int i = 0; i < cpu_count; i++) {
        if (cpu_ids[i] == apic_id) {
            return i;
        }
    }
    return 0;
}

// Execute function on all CPUs
static smp_call_func_t global_func = NULL;
static void *global_info = NULL;
static atomic_t cpus_ready = ATOMIC_INIT(0);
static atomic_t cpus_finished = ATOMIC_INIT(0);

void on_each_cpu(smp_call_func_t func, void *info) {
    global_func = func;
    global_info = info;
    atomic_set(&cpus_ready, cpu_count);
    atomic_set(&cpus_finished, 0);

    // Execute on all CPUs (including BSP)
    for (int i = 0; i < cpu_count; i++) {
        if (atomic_read(&cpus_ready) > 0) {
            func(info);
            atomic_inc(&cpus_finished);
            atomic_dec(&cpus_ready);
        }
    }

    // Wait for all CPUs to finish
    while (atomic_read(&cpus_finished) < cpu_count) {
        cpu_relax();
    }
}

// AP boot notification
void ap_boot_complete(void) {
    int cpu_id = smp_processor_id();
    per_cpu_ids[cpu_id] = cpu_id;

    // NO serial output during boot - just increment counter
    atomic_inc(&cpus_booted);

    // AP idle loop
    while (1) {
        if (global_func && atomic_read(&cpus_ready) > 0) {
            global_func(global_info);
            atomic_inc(&cpus_finished);
            atomic_dec(&cpus_ready);
            global_func = NULL;
        }
        cpu_halt();
    }
}
