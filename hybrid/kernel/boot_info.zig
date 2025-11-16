// Boot information structure received from C bootstrap
// IMPORTANT: Keep in sync with C definition in shared/boot_info.h

const MAX_CPUS = 16;

pub const CpuInfo = extern struct {
    apic_id: u8,
    stack_top: usize,
    online: bool,
};

pub const BootInfo = extern struct {
    // CPU Information
    cpu_count: u32,
    bsp_apic_id: u32,
    use_x2apic: bool,

    // Memory Layout
    kernel_phys_start: usize,
    kernel_phys_end: usize,
    free_mem_start: usize,
    free_mem_size: u64,

    // Memory Management Structures
    pmm_bitmap: usize,
    pmm_bitmap_size: u32,
    pml4_physical: usize,

    // ACPI Tables
    rsdp_address: usize,
    madt_address: usize,

    // Interrupt Handling
    idt_base: usize,
    idt_limit: u16,
    idt_loaded: bool,

    // Per-CPU Information
    cpus: [MAX_CPUS]CpuInfo,

    // APIC Base
    apic_base: usize,

    // Debug/Serial
    serial_initialized: bool,
};

// C services that Zig can call back to
pub extern fn c_write_serial(str: [*:0]const u8) void;
pub extern fn c_write_serial_hex(value: u64) void;
pub extern fn c_send_eoi() void;
