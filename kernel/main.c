#include "../include/types.h"
#include "../include/serial.h"
#include "../include/sync.h"
#include "../include/acpi.h"
#include "../include/smp.h"

// Shared computation state
static uint64_t shared_result = 0;
static spinlock_t result_lock = SPINLOCK_INIT;
static atomic_t cores_done = ATOMIC_INIT(0);

// Multiboot2 info
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} multiboot2_info_t;

// Computation task executed on each CPU
static void computation_task(void *info) {
    int cpu_id = smp_processor_id();
    uint64_t local_result = 0;

    // Compute sum from 1 to 1,000,000
    for (uint64_t i = 1; i <= 1000000; i++) {
        local_result += i;
    }

    // Add to shared result (protected by spinlock)
    spin_lock(&result_lock);
    shared_result += local_result;
    spin_unlock(&result_lock);

    // Signal completion
    atomic_inc(&cores_done);

    serial_printf("[Core %d] Computation done (local result: %lu)\n", cpu_id, local_result);
}

// Kernel entry point
void kernel_main(void *multiboot_info) {
    // Initialize serial port
    serial_init();

    serial_puts("\n=== Boot Linux Minimal - 64-bit SMP Kernel ===\n\n");

    // Parse multiboot info to find ACPI RSDP
    void *rsdp = NULL;  // TODO: extract from multiboot tags if needed

    // Initialize ACPI and detect CPUs
    serial_puts("[Boot] Detecting CPUs via ACPI...\n");
    acpi_init(rsdp);

    int cpu_count = acpi_get_cpu_count();
    if (cpu_count == 0) {
        serial_puts("[Boot] ERROR: No CPUs detected!\n");
        while (1) cpu_halt();
    }

    serial_printf("[Boot] Using ACPI for SMP detection\n");
    serial_printf("[Boot] Detected %d possible CPUs\n", cpu_count);

    // Initialize SMP (Local APIC)
    serial_puts("[Boot] Initializing SMP...\n");
    smp_init();

    // Get CR3 for trampoline
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // Set up trampoline variables
    extern uint32_t trampoline_cr3;
    extern uint64_t trampoline_stack;
    extern uint64_t trampoline_entry;

    trampoline_cr3 = (uint32_t)cr3;
    trampoline_stack = 0x7000;  // Stack for APs
    extern void ap_boot_complete(void);
    trampoline_entry = (uint64_t)ap_boot_complete;

    // Boot APs (NO serial output inside!)
    serial_puts("[Boot] Starting Application Processors...\n");
    smp_boot_aps();

    // Display results
    int booted_cpus = smp_get_cpu_count();
    serial_printf("[Boot] Boot complete: %d CPUs online\n", booted_cpus);

    // Print each CPU info
    for (int i = 0; i < cpu_count; i++) {
        uint8_t apic_id = acpi_get_apic_id(i);
        serial_printf("[Core %d] APIC ID: %d\n", i, apic_id);
    }

    // Launch computation on all CPUs
    serial_puts("\n[Computation] Starting parallel computation...\n");
    on_each_cpu(computation_task, NULL);

    // Wait for all CPUs to finish
    while (atomic_read(&cores_done) < cpu_count) {
        cpu_relax();
    }

    // Display final result
    serial_puts("\n=== Results ===\n");
    serial_printf("Total result: %lu\n", shared_result);
    serial_printf("Expected: %lu (per core) * %d (cores) = %lu\n",
                  500000500000ULL, cpu_count, 500000500000ULL * cpu_count);

    if (shared_result == 500000500000ULL * cpu_count) {
        serial_puts("[SUCCESS] All APs booted and functional!\n");
    } else {
        serial_puts("[ERROR] Result mismatch!\n");
    }

    serial_puts("\n=== System Halted ===\n");

    // Halt forever
    while (1) cpu_halt();
}
