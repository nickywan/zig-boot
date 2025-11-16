// APIC (Advanced Programmable Interrupt Controller)
const serial = @import("serial.zig");

const APIC_BASE_MSR: u32 = 0x1B;
const APIC_BASE_ENABLE: u64 = 1 << 11;

const APIC_ID_REG: u32 = 0x20;
const APIC_SVR_REG: u32 = 0xF0;
const APIC_ENABLE: u32 = 1 << 8;
const SPURIOUS_VECTOR: u32 = 0xFF;

var apic_base: usize = 0xFEE00000; // Default APIC base

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

fn apic_read(offset: u32) u32 {
    const addr = apic_base + offset;
    return @as(*volatile u32, @ptrFromInt(addr)).*;
}

fn apic_write(offset: u32, value: u32) void {
    const addr = apic_base + offset;
    @as(*volatile u32, @ptrFromInt(addr)).* = value;
}

pub fn init() !void {
    serial.write_string("[APIC] x2APIC not available - using xAPIC mode\n");
    serial.write_string("[APIC] Physical address: ");
    serial.write_hex_u64(apic_base);
    serial.write_string(" (default)\n");

    // Enable APIC in MSR
    const apic_msr = rdmsr(APIC_BASE_MSR);
    if ((apic_msr & APIC_BASE_ENABLE) == 0) {
        wrmsr(APIC_BASE_MSR, apic_msr | APIC_BASE_ENABLE);
    }

    // Enable APIC via Spurious Interrupt Vector Register
    serial.write_string("[APIC] Enabling APIC (SVR register)...\n");
    apic_write(APIC_SVR_REG, APIC_ENABLE | SPURIOUS_VECTOR);

    const apic_id = apic_read(APIC_ID_REG) >> 24;
    serial.write_string("[APIC] BSP APIC ID: ");
    serial.write_dec_u32(apic_id);
    serial.write_string("\n");
}

pub fn start_timer() void {
    // Timer stub - to be implemented
    serial.write_string("[TIMER] Timer implementation pending\n");
}
