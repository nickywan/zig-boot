// APIC (Advanced Programmable Interrupt Controller)
// Supports both xAPIC (MMIO) and x2APIC (MSR) modes
const serial = @import("serial.zig");

// APIC MSR and basic constants
const APIC_BASE_MSR: u32 = 0x1B;
const APIC_BASE_ENABLE: u64 = 1 << 11;   // xAPIC enable bit
const X2APIC_ENABLE: u64 = 1 << 10;      // x2APIC enable bit

// xAPIC MMIO register offsets
const APIC_ID_REG: u32 = 0x20;
const APIC_SVR_REG: u32 = 0xF0;
const APIC_ENABLE: u32 = 1 << 8;
const SPURIOUS_VECTOR: u32 = 0xFF;
const APIC_EOI_REG: u32 = 0xB0;

// Timer registers (xAPIC offsets)
const APIC_TIMER_LVT: u32 = 0x320;
const APIC_TIMER_ICR: u32 = 0x380;  // Initial Count Register
const APIC_TIMER_CCR: u32 = 0x390;  // Current Count Register
const APIC_TIMER_DCR: u32 = 0x3E0;  // Divide Configuration Register

// x2APIC MSR addresses (base = 0x800)
const X2APIC_MSR_BASE: u32 = 0x800;
const X2APIC_APICID: u32 = 0x802;      // APIC ID (read-only)
const X2APIC_VERSION: u32 = 0x803;     // APIC Version
const X2APIC_TPR: u32 = 0x808;         // Task Priority Register
const X2APIC_PPR: u32 = 0x80A;         // Processor Priority Register
const X2APIC_EOI: u32 = 0x80B;         // EOI
const X2APIC_LDR: u32 = 0x80D;         // Logical Destination Register
const X2APIC_SVR: u32 = 0x80F;         // Spurious Vector Register
const X2APIC_ESR: u32 = 0x828;         // Error Status Register
const X2APIC_ICR: u32 = 0x830;         // Interrupt Command Register (64-bit!)
const X2APIC_LVT_TIMER: u32 = 0x832;   // LVT Timer
const X2APIC_TIMER_ICR: u32 = 0x838;   // Timer Initial Count
const X2APIC_TIMER_CCR: u32 = 0x839;   // Timer Current Count
const X2APIC_TIMER_DCR: u32 = 0x83E;   // Timer Divide Configuration
const X2APIC_LVT_LINT0: u32 = 0x835;   // LVT LINT0
const X2APIC_LVT_LINT1: u32 = 0x836;   // LVT LINT1
const X2APIC_LVT_ERROR: u32 = 0x837;   // LVT Error

// Timer modes
const APIC_TIMER_PERIODIC: u32 = 0x20000;  // Periodic mode (bit 17)
const TIMER_VECTOR: u32 = 32;              // IRQ 0 (timer) mapped to vector 32

// APIC mode tracking
var apic_base: usize = 0xFEE00000;  // Default xAPIC MMIO base
pub var use_x2apic: bool = false;    // true if x2APIC mode, false if xAPIC mode

// Global timer tick counter
pub var timer_ticks: u32 = 0;

// MSR operations
inline fn rdmsr(msr: u32) u64 {
    var low: u32 = undefined;
    var high: u32 = undefined;
    asm volatile ("rdmsr"
        : [low] "={eax}" (low),
          [high] "={edx}" (high),
        : [msr] "{ecx}" (msr),
    );
    return (@as(u64, high) << 32) | low;
}

inline fn wrmsr(msr: u32, value: u64) void {
    const low = @as(u32, @truncate(value));
    const high = @as(u32, @truncate(value >> 32));
    asm volatile ("wrmsr"
        :
        : [msr] "{ecx}" (msr),
          [low] "{eax}" (low),
          [high] "{edx}" (high),
    );
}

// CPUID instruction
inline fn cpuid(leaf: u32) struct { eax: u32, ebx: u32, ecx: u32, edx: u32 } {
    var eax: u32 = undefined;
    var ebx: u32 = undefined;
    var ecx: u32 = 0;
    var edx: u32 = undefined;

    asm volatile ("cpuid"
        : [eax] "={eax}" (eax),
          [ebx] "={ebx}" (ebx),
          [ecx] "={ecx}" (ecx),
          [edx] "={edx}" (edx),
        : [leaf] "{eax}" (leaf),
          [ecx_in] "{ecx}" (ecx),
    );

    return .{ .eax = eax, .ebx = ebx, .ecx = ecx, .edx = edx };
}

// APIC read/write abstraction (supports both xAPIC MMIO and x2APIC MSR)
fn apic_read(reg: u32) u32 {
    if (use_x2apic) {
        // x2APIC: use MSR
        // Convert MMIO offset to MSR address
        const msr = X2APIC_MSR_BASE + (reg >> 4);
        return @as(u32, @truncate(rdmsr(msr)));
    } else {
        // xAPIC: use MMIO
        const addr = apic_base + reg;
        return @as(*volatile u32, @ptrFromInt(addr)).*;
    }
}

fn apic_write(reg: u32, value: u32) void {
    if (use_x2apic) {
        // x2APIC: use MSR
        const msr = X2APIC_MSR_BASE + (reg >> 4);
        wrmsr(msr, value);
    } else {
        // xAPIC: use MMIO
        const addr = apic_base + reg;
        @as(*volatile u32, @ptrFromInt(addr)).* = value;
    }
}

// Disable legacy 8259 PIC (CRITICAL before using APIC!)
fn disable_pic() void {
    serial.write_string("[PIC] Disabling legacy 8259 PIC...\n");
    // Mask all interrupts on both PICs
    asm volatile ("outb %[val], %[port]" : : [val] "{al}" (@as(u8, 0xFF)), [port] "{dx}" (@as(u16, 0x21)));  // Master PIC
    asm volatile ("outb %[val], %[port]" : : [val] "{al}" (@as(u8, 0xFF)), [port] "{dx}" (@as(u16, 0xA1)));  // Slave PIC
    serial.write_string("[PIC] Legacy PIC disabled\n");
}

pub fn init() !void {
    serial.write_string("\n[APIC] Initializing Local APIC...\n");

    // CRITICAL: Disable legacy PIC first!
    disable_pic();

    // Check for x2APIC support via CPUID.01H:ECX[21]
    const result = cpuid(1);
    const x2apic_available = (result.ecx >> 21) & 1;

    if (x2apic_available != 0) {
        serial.write_string("[APIC] x2APIC supported - enabling x2APIC mode\n");

        // Read current APIC base MSR
        var apic_msr = rdmsr(APIC_BASE_MSR);

        // Enable both xAPIC (bit 11) and x2APIC (bit 10)
        // Must enable xAPIC first, then x2APIC
        if ((apic_msr & APIC_BASE_ENABLE) == 0) {
            apic_msr |= APIC_BASE_ENABLE;
            wrmsr(APIC_BASE_MSR, apic_msr);
        }

        // Now enable x2APIC mode
        apic_msr |= X2APIC_ENABLE;
        wrmsr(APIC_BASE_MSR, apic_msr);

        use_x2apic = true;

        // In x2APIC mode, enable APIC via SVR MSR with spurious vector
        wrmsr(X2APIC_SVR, APIC_ENABLE | SPURIOUS_VECTOR);

        // Set Task Priority Register to 0 to accept all interrupts
        wrmsr(X2APIC_TPR, 0);

        // Read APIC ID from x2APIC MSR
        const apic_id = @as(u32, @truncate(rdmsr(X2APIC_APICID)));
        serial.write_string("[APIC] x2APIC mode enabled (MSR-based)\n");
        serial.write_string("[APIC] BSP APIC ID: ");
        serial.write_dec_u32(apic_id);
        serial.write_string("\n");
    } else {
        serial.write_string("[APIC] x2APIC not available - using xAPIC mode\n");

        const apic_msr = rdmsr(APIC_BASE_MSR);
        const apic_phys_addr = apic_msr & 0xFFFFF000;

        serial.write_string("[APIC] Physical address: ");
        serial.write_hex_u64(apic_phys_addr);
        serial.write_string(" (default)\n");

        apic_base = apic_phys_addr;

        if ((apic_msr & APIC_BASE_ENABLE) == 0) {
            serial.write_string("[APIC] Enabling APIC in MSR...\n");
            wrmsr(APIC_BASE_MSR, apic_msr | APIC_BASE_ENABLE);
        }

        serial.write_string("[APIC] Enabling APIC (SVR register)...\n");
        apic_write(APIC_SVR_REG, APIC_ENABLE | SPURIOUS_VECTOR);

        // Set Task Priority Register to 0 to accept all interrupts
        apic_write(0x80, 0);  // TPR register at offset 0x80

        const apic_id = apic_read(APIC_ID_REG) >> 24;
        serial.write_string("[APIC] BSP APIC ID: ");
        serial.write_dec_u32(apic_id);
        serial.write_string("\n");

        use_x2apic = false;
    }

    serial.write_string("[APIC] Local APIC initialized successfully!\n");
}

pub fn start_timer() void {
    serial.write_string("[TIMER] Configuring APIC timer...\n");

    // Set timer divide configuration to 16 (divide by 16)
    if (use_x2apic) {
        wrmsr(X2APIC_TIMER_DCR, 0x3);
    } else {
        apic_write(APIC_TIMER_DCR, 0x3);
    }

    // Set timer to periodic mode with vector 32
    // IMPORTANT: Clear the mask bit (bit 16) to enable interrupts
    var lvt_timer = APIC_TIMER_PERIODIC | TIMER_VECTOR;
    lvt_timer &= ~(@as(u32, 1) << 16);  // Clear mask bit

    if (use_x2apic) {
        wrmsr(X2APIC_LVT_TIMER, lvt_timer);
    } else {
        apic_write(APIC_TIMER_LVT, lvt_timer);
    }

    // Set initial count (this starts the timer)
    // Lower value = faster interrupts. 10000000 gives roughly 10 Hz with divide-by-16
    const initial_count: u32 = 10000000;
    if (use_x2apic) {
        wrmsr(X2APIC_TIMER_ICR, initial_count);
    } else {
        apic_write(APIC_TIMER_ICR, initial_count);
    }

    serial.write_string("[TIMER] APIC timer started (periodic mode, vector 32)\n");
    if (use_x2apic) {
        serial.write_string("[TIMER] Using x2APIC mode (MSR-based, faster!)\n");
    } else {
        serial.write_string("[TIMER] Using xAPIC mode (MMIO-based)\n");
    }
}

// Send End of Interrupt to APIC
pub fn send_eoi() void {
    if (use_x2apic) {
        // x2APIC: write 0 to EOI MSR
        wrmsr(X2APIC_EOI, 0);
    } else {
        // xAPIC: write 0 to EOI MMIO register
        apic_write(APIC_EOI_REG, 0);
    }
}

// Get current CPU APIC ID
pub fn get_apic_id() u32 {
    if (use_x2apic) {
        return @as(u32, @truncate(rdmsr(X2APIC_APICID)));
    } else {
        return apic_read(APIC_ID_REG) >> 24;
    }
}
