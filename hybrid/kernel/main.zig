// Zig Kernel Entry Point - receives control from C bootstrap
const BootInfo = @import("boot_info.zig").BootInfo;
const c_write_serial = @import("boot_info.zig").c_write_serial;
const c_write_serial_hex = @import("boot_info.zig").c_write_serial_hex;
const tests = @import("tests.zig");
const allocator_mod = @import("allocator.zig");

// Panic handler (required for freestanding)
pub const panic = @import("panic.zig").panic;

// Entry point called from C bootstrap
export fn zig_kernel_main(boot_info: *const BootInfo) callconv(.C) noreturn {
    c_write_serial("\n");
    c_write_serial("===========================================\n");
    c_write_serial("  Zig Kernel Started!\n");
    c_write_serial("===========================================\n");
    c_write_serial("\n");

    // Display boot information received from C
    c_write_serial("[Zig] Received BootInfo from C bootstrap\n");
    c_write_serial("[Zig] CPUs detected: ");
    write_dec_u32(boot_info.cpu_count);
    c_write_serial("\n");

    c_write_serial("[Zig] x2APIC mode: ");
    if (boot_info.use_x2apic) {
        c_write_serial("ENABLED (MSR-based)\n");
    } else {
        c_write_serial("DISABLED (xAPIC MMIO)\n");
    }

    c_write_serial("[Zig] IDT loaded: ");
    if (boot_info.idt_loaded) {
        c_write_serial("YES (interrupts enabled)\n");
    } else {
        c_write_serial("NO\n");
    }

    c_write_serial("[Zig] Free memory start: ");
    c_write_serial_hex(boot_info.free_mem_start);
    c_write_serial("\n");

    c_write_serial("[Zig] Free memory size: ");
    write_dec_u64(boot_info.free_mem_size);
    c_write_serial(" bytes\n\n");

    // Display per-CPU information
    c_write_serial("[Zig] Per-CPU Information:\n");
    var i: u32 = 0;
    while (i < boot_info.cpu_count) : (i += 1) {
        c_write_serial("  CPU ");
        write_dec_u32(i);
        c_write_serial(": APIC ID ");
        write_dec_u32(boot_info.cpus[i].apic_id);
        c_write_serial(", Online: ");
        if (boot_info.cpus[i].online) {
            c_write_serial("YES\n");
        } else {
            c_write_serial("NO\n");
        }
    }

    // Test memory allocator
    allocator_mod.test_allocator();

    c_write_serial("\n");
    c_write_serial("===========================================\n");
    c_write_serial("  Running Parallel Computation Tests\n");
    c_write_serial("===========================================\n");
    c_write_serial("\n");

    // Run parallel tests (APs already running, waiting for work)
    tests.run_all(boot_info);

    c_write_serial("\n");
    c_write_serial("===========================================\n");
    c_write_serial("[SUCCESS] Zig kernel completed!\n");
    c_write_serial("===========================================\n");

    // Halt
    while (true) {
        asm volatile ("hlt");
    }
}

// Helper to write decimal u32 to serial
fn write_dec_u32(value: u32) void {
    var buf: [16]u8 = undefined;
    var i: usize = 0;
    var v = value;

    if (v == 0) {
        c_write_serial("0");
        return;
    }

    while (v > 0) {
        buf[i] = @as(u8, @intCast((v % 10))) + '0';
        v /= 10;
        i += 1;
    }

    while (i > 0) {
        i -= 1;
        var ch = [2]u8{ buf[i], 0 };
        c_write_serial(@ptrCast(&ch));
    }
}

// Helper to write decimal u64 to serial
fn write_dec_u64(value: u64) void {
    var buf: [32]u8 = undefined;
    var i: usize = 0;
    var v = value;

    if (v == 0) {
        c_write_serial("0");
        return;
    }

    while (v > 0) {
        buf[i] = @as(u8, @intCast((v % 10))) + '0';
        v /= 10;
        i += 1;
    }

    while (i > 0) {
        i -= 1;
        var ch = [2]u8{ buf[i], 0 };
        c_write_serial(@ptrCast(&ch));
    }
}
