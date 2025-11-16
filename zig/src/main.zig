// Bare-Metal x86-64 Kernel in Zig
const std = @import("std");
const serial = @import("serial.zig");
const vga = @import("vga.zig");
const multiboot = @import("multiboot.zig");
const pmm = @import("pmm.zig");
const vmm = @import("vmm.zig");
const acpi = @import("acpi.zig");
const apic = @import("apic.zig");
const smp = @import("smp.zig");
const tests = @import("tests.zig");

// Disable runtime safety checks for bare metal
pub const panic = panicking.panic;
const panicking = @import("panic.zig");

// No standard library needed
pub const std_options: std.Options = .{
    .log_level = .debug,
    .logFn = log,
};

fn log(
    comptime level: std.log.Level,
    comptime scope: anytype,
    comptime format: []const u8,
    args: anytype,
) void {
    _ = level;
    _ = scope;
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, format, args) catch {
        serial.write_string("[LOG ERROR]\n");
        vga.write_string("[LOG ERROR]\n");
        return;
    };
    serial.write_string(msg);
    serial.write_string("\n");
    vga.write_string(msg);
    vga.write_string("\n");
}

// Helper to write to both serial and VGA
fn puts(string: []const u8) void {
    serial.write_string(string);
    vga.write_string(string);
}

// Entry point (called from boot code)
export fn kernel_main(multiboot_addr: u64) callconv(.C) noreturn {
    // Initialize output first
    serial.init();
    vga.init();

    // Simple test output
    puts("Hello from Zig!\n");

    puts("\n");
    puts("===========================================\n");
    puts("  Zig Kernel - x86-64 + VGA\n");
    puts("===========================================\n");
    puts("\n");

    puts("[INFO] Multiboot2 info at: ");
    serial.write_hex_u64(multiboot_addr);
    vga.write_hex_u64(multiboot_addr);
    puts("\n\n");

    // Initialize memory management
    serial.write_string("[PMM] Initializing Physical Memory Manager...\n");
    pmm.init(@truncate(multiboot_addr)) catch |err| {
        serial.write_string("[ERROR] PMM init failed: ");
        serial.write_dec_u32(@intFromError(err));
        serial.write_string("\n");
        halt();
    };
    serial.write_string("[PMM] Physical Memory Manager initialized!\n\n");

    serial.write_string("[VMM] Initializing Virtual Memory Manager...\n");
    vmm.init();
    serial.write_string("[VMM] Virtual Memory Manager initialized!\n\n");

    // Parse ACPI to detect CPUs
    serial.write_string("[ACPI] Searching for RSDP...\n");
    const cpu_count = acpi.detect_cpus() catch |err| {
        serial.write_string("[ERROR] ACPI detection failed: ");
        serial.write_dec_u32(@intFromError(err));
        serial.write_string("\n");
        halt();
    };

    serial.write_string("[ACPI] Detected ");
    serial.write_dec_u32(cpu_count);
    serial.write_string(" CPU(s)\n\n");

    // Initialize APIC
    serial.write_string("[APIC] Initializing Local APIC...\n");
    apic.init() catch |err| {
        serial.write_string("[ERROR] APIC init failed: ");
        serial.write_dec_u32(@intFromError(err));
        serial.write_string("\n");
        halt();
    };
    serial.write_string("[APIC] Local APIC initialized successfully!\n\n");

    // Boot Application Processors
    if (cpu_count > 1) {
        serial.write_string("[SMP] Starting Application Processors...\n");
        smp.boot_aps(cpu_count) catch |err| {
            serial.write_string("[ERROR] SMP boot failed: ");
            serial.write_dec_u32(@intFromError(err));
            serial.write_string("\n");
            halt();
        };

        serial.write_string("[SMP] CPUs online: ");
        serial.write_dec_u32(smp.get_online_count());
        serial.write_string(" / ");
        serial.write_dec_u32(cpu_count);
        serial.write_string("\n\n");
    }

    serial.write_string("[SUCCESS] All CPUs booted successfully!\n\n");

    // Start APIC timer on BSP
    serial.write_string("[TIMER] Starting APIC timer...\n");
    apic.start_timer();
    serial.write_string("[TIMER] BSP timer started successfully!\n\n");

    // Run parallel computation tests
    serial.write_string("===========================================\n");
    serial.write_string("  Running Parallel Computation Tests\n");
    serial.write_string("===========================================\n\n");

    tests.run_all(cpu_count);

    serial.write_string("\n[SUCCESS] All tests completed!\n");
    serial.write_string("\n=== System Halted ===\n");

    halt();
}

fn halt() noreturn {
    while (true) {
        asm volatile ("hlt");
    }
}

// Multiboot2 header is in boot.S
