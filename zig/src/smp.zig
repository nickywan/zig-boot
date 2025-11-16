// SMP (Symmetric Multiprocessing) - Boot Application Processors
const serial = @import("serial.zig");
const std = @import("std");
const acpi = @import("acpi.zig");
const apic = @import("apic.zig");
const tests = @import("tests.zig");

// Global CPU count (set by BSP, read by APs)
pub var global_cpu_count: u32 = 1;

const TRAMPOLINE_ADDR: usize = 0x8000;
const AP_STACK_SIZE: usize = 8192;  // 8KB per AP
const MAX_CPUS: usize = 16;  // Maximum CPUs supported

// Trampoline symbols from trampoline.S
extern const trampoline_start: u8;
extern const trampoline_end: u8;

// Per-CPU stacks (8KB each, aligned)
var ap_stacks: [MAX_CPUS][AP_STACK_SIZE]u8 align(16) = undefined;

// CPUs online counter
var cpus_online: u32 = 1; // BSP is already online

pub fn boot_aps(cpu_count: u32) !void {
    if (cpu_count <= 1) {
        serial.write_string("[SMP] Only 1 CPU detected, skipping AP boot\n");
        return;
    }

    // Store CPU count globally for APs to use
    global_cpu_count = cpu_count;

    serial.write_string("[SMP] Setting up trampoline...\n");

    // Calculate trampoline size
    const trampoline_size = @intFromPtr(&trampoline_end) - @intFromPtr(&trampoline_start);
    serial.write_string("[SMP] Trampoline size: ");
    serial.write_dec_u32(@truncate(trampoline_size));
    serial.write_string(" bytes\n");

    // Copy trampoline to 0x8000
    const dest = @as([*]volatile u8, @ptrFromInt(TRAMPOLINE_ADDR));
    const src = @as([*]const u8, @ptrCast(&trampoline_start));
    var i: usize = 0;
    while (i < trampoline_size) : (i += 1) {
        dest[i] = src[i];
    }
    serial.write_string("[SMP] Trampoline copied to 0x8000\n");

    // Get current CR3
    const cr3 = asm volatile ("mov %%cr3, %[ret]"
        : [ret] "=r" (-> u64),
    );

    // Patch trampoline variables (at end of trampoline)
    // Offsets from end: -24: cr3, -16: stack, -8: entry
    const cr3_ptr = @as(*volatile u64, @ptrFromInt(TRAMPOLINE_ADDR + trampoline_size - 24));
    const stack_ptr = @as(*volatile u64, @ptrFromInt(TRAMPOLINE_ADDR + trampoline_size - 16));
    const entry_ptr = @as(*volatile u64, @ptrFromInt(TRAMPOLINE_ADDR + trampoline_size - 8));

    cr3_ptr.* = cr3;
    stack_ptr.* = 0;  // Will be patched per-AP
    entry_ptr.* = @intFromPtr(&ap_entry);

    // CRITICAL: Flush caches after patching
    asm volatile ("wbinvd" ::: "memory");

    serial.write_string("[SMP] Trampoline configured\n");

    // Boot each AP
    serial.write_string("[SMP] Starting Application Processors...\n");
    var cpu_idx: u32 = 1;
    while (cpu_idx < cpu_count) : (cpu_idx += 1) {
        boot_ap(cpu_idx, trampoline_size);

        // Wait a bit between APs
        delay_ms(10);
    }

    serial.write_string("[SMP] Application Processors booted\n");
}

fn boot_ap(cpu_idx: u32, trampoline_size: usize) void {
    const apic_id = acpi.get_apic_id(cpu_idx);

    serial.write_string("[SMP] Booting AP ");
    serial.write_dec_u32(cpu_idx);
    serial.write_string(" (APIC ID ");
    serial.write_dec_u32(apic_id);
    serial.write_string(")...\n");

    // Patch per-CPU stack (point to end of stack - stack grows down)
    const stack_ptr = @as(*volatile u64, @ptrFromInt(TRAMPOLINE_ADDR + trampoline_size - 16));
    const stack_top = @intFromPtr(&ap_stacks[cpu_idx]) + AP_STACK_SIZE;
    stack_ptr.* = stack_top;

    // CRITICAL: Flush caches after patching
    asm volatile ("wbinvd" ::: "memory");

    // INIT-SIPI-SIPI sequence
    // INIT IPI
    send_ipi(apic_id, 0x00004500);  // INIT | LEVEL | ASSERT
    delay_ms(10);

    // SIPI #1
    send_ipi(apic_id, 0x00004608);  // STARTUP | vector 0x08 (0x8000>>12)
    delay_us(200);

    // SIPI #2
    send_ipi(apic_id, 0x00004608);  // STARTUP | vector 0x08
    delay_us(200);
}

fn send_ipi(apic_id: u8, flags: u32) void {
    if (apic.use_x2apic) {
        // x2APIC: ICR is a single 64-bit MSR
        // Bits 0-31: flags (ICR low)
        // Bits 32-63: destination APIC ID
        const icr: u64 = (@as(u64, apic_id) << 32) | flags;
        const X2APIC_ICR: u32 = 0x830;
        wrmsr(X2APIC_ICR, icr);
    } else {
        // xAPIC: ICR is two 32-bit MMIO registers
        const APIC_ICR_LOW: u32 = 0x300;
        const APIC_ICR_HIGH: u32 = 0x310;
        const apic_base: usize = 0xFEE00000;

        // Wait for ICR to be ready
        const icr_low_ptr = @as(*volatile u32, @ptrFromInt(apic_base + APIC_ICR_LOW));
        while ((icr_low_ptr.* & 0x1000) != 0) {
            asm volatile ("pause");
        }

        // Write destination
        const icr_high_ptr = @as(*volatile u32, @ptrFromInt(apic_base + APIC_ICR_HIGH));
        icr_high_ptr.* = @as(u32, apic_id) << 24;

        // Write command
        icr_low_ptr.* = flags;

        // Wait for delivery
        while ((icr_low_ptr.* & 0x1000) != 0) {
            asm volatile ("pause");
        }
    }
}

// MSR operations (needed for x2APIC IPIs)
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

fn delay_ms(ms: u32) void {
    var i: u32 = 0;
    while (i < ms * 1000) : (i += 1) {
        asm volatile ("pause");
    }
}

fn delay_us(us: u32) void {
    var i: u32 = 0;
    while (i < us) : (i += 1) {
        asm volatile ("pause");
    }
}

// MSR read/write for AP APIC initialization
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

// AP entry point (called from trampoline after 64-bit transition)
export fn ap_entry() callconv(.C) noreturn {
    // Get my CPU ID by atomically incrementing counter
    const my_id = @atomicRmw(u32, &cpus_online, .Add, 1, .seq_cst);

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] Started!\n");

    // Initialize APIC on this AP (same mode as BSP)
    const APIC_BASE_MSR: u32 = 0x1B;
    const APIC_BASE_ENABLE: u64 = 1 << 11;
    const X2APIC_ENABLE: u64 = 1 << 10;
    const APIC_ENABLE: u32 = 1 << 8;
    const SPURIOUS_VECTOR: u32 = 0xFF;

    if (apic.use_x2apic) {
        // x2APIC mode
        const X2APIC_SVR: u32 = 0x80F;
        const X2APIC_TPR: u32 = 0x808;

        var apic_msr = rdmsr(APIC_BASE_MSR);

        // Enable xAPIC first if not enabled
        if ((apic_msr & APIC_BASE_ENABLE) == 0) {
            apic_msr |= APIC_BASE_ENABLE;
            wrmsr(APIC_BASE_MSR, apic_msr);
        }

        // Enable x2APIC mode
        apic_msr |= X2APIC_ENABLE;
        wrmsr(APIC_BASE_MSR, apic_msr);

        // Enable APIC via SVR MSR with spurious vector
        wrmsr(X2APIC_SVR, APIC_ENABLE | SPURIOUS_VECTOR);

        // CRITICAL: Set Task Priority Register to 0 to accept all interrupts
        wrmsr(X2APIC_TPR, 0);
    } else {
        // xAPIC mode (MMIO)
        const apic_base: usize = 0xFEE00000;
        const APIC_SVR_REG: u32 = 0xF0;

        // Enable APIC in MSR if needed
        const apic_msr = rdmsr(APIC_BASE_MSR);
        if ((apic_msr & APIC_BASE_ENABLE) == 0) {
            wrmsr(APIC_BASE_MSR, apic_msr | APIC_BASE_ENABLE);
        }

        // Enable APIC via SVR register with spurious vector
        const svr_ptr = @as(*volatile u32, @ptrFromInt(apic_base + APIC_SVR_REG));
        svr_ptr.* = APIC_ENABLE | SPURIOUS_VECTOR;

        // CRITICAL: Set Task Priority Register to 0 to accept all interrupts
        const tpr_ptr = @as(*volatile u32, @ptrFromInt(apic_base + 0x80));
        tpr_ptr.* = 0;
    }

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] APIC initialized\n");

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] Ready for tests\n");

    // Wait a bit for all APs to initialize
    delay_ms(50);

    // Run parallel tests
    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] Running Test 1: Parallel counters...\n");
    tests.test_parallel_counters(my_id);
    tests.barrier_wait(my_id, global_cpu_count);

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] Running Test 2: Distributed sum...\n");
    tests.test_distributed_sum(my_id, global_cpu_count);
    tests.barrier_wait(my_id, global_cpu_count);

    // Reset counter for test 3
    tests.per_cpu_counters[my_id] = 0;
    tests.barrier_wait(my_id, global_cpu_count);

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] Running Test 3: Barrier sync...\n");
    tests.test_barrier_sync(my_id, global_cpu_count);

    serial.write_string("[AP ");
    serial.write_dec_u32(my_id);
    serial.write_string("] All tests completed!\n");

    // Halt after tests
    while (true) {
        asm volatile ("hlt");
    }
}

pub fn get_online_count() u32 {
    return @atomicLoad(u32, &cpus_online, .seq_cst);
}
