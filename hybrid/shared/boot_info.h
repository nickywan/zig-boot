#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_CPUS 16

// Boot information structure passed from C bootstrap to Zig kernel
// IMPORTANT: Keep in sync with Zig definition in kernel/boot_info.zig
typedef struct {
    // CPU Information
    uint32_t cpu_count;          // Number of CPUs detected
    uint32_t bsp_apic_id;        // Bootstrap processor APIC ID
    bool use_x2apic;             // True if x2APIC mode is active

    // Memory Layout
    uintptr_t kernel_phys_start; // Physical address where kernel is loaded
    uintptr_t kernel_phys_end;   // End of kernel in physical memory
    uintptr_t free_mem_start;    // Start of free physical memory
    uint64_t free_mem_size;      // Size of free memory in bytes

    // Memory Management Structures (already initialized by C)
    uintptr_t pmm_bitmap;        // Physical memory manager bitmap
    uint32_t pmm_bitmap_size;    // Bitmap size in bytes
    uintptr_t pml4_physical;     // CR3 value (page table root)

    // ACPI Tables
    uintptr_t rsdp_address;      // Root System Description Pointer
    uintptr_t madt_address;      // Multiple APIC Description Table

    // Interrupt Handling (IDT already loaded by C)
    uintptr_t idt_base;          // IDT base address
    uint16_t idt_limit;          // IDT limit
    bool idt_loaded;             // True if IDT is loaded on BSP

    // Per-CPU Information
    struct {
        uint8_t apic_id;         // APIC ID for this CPU
        uintptr_t stack_top;     // Top of stack for this CPU
        bool online;             // True if CPU is running
    } cpus[MAX_CPUS];

    // APIC Base Address
    uintptr_t apic_base;         // APIC MMIO base (0xFEE00000 for xAPIC)

    // Debug/Serial
    bool serial_initialized;     // True if COM1 is ready

} BootInfo;

// C bootstrap calls this to hand control to Zig kernel
// This function is implemented in Zig (kernel/main.zig)
extern void zig_kernel_main(const BootInfo* boot_info) __attribute__((noreturn));

// Zig kernel can call back to C for low-level services
// These are implemented in C bootstrap (boot/services.c)
extern void c_write_serial(const char* str);
extern void c_write_serial_hex(uint64_t value);
extern void c_send_eoi(void);  // Send End-Of-Interrupt to APIC

#endif // BOOT_INFO_H
