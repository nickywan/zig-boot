// Parallel computation tests for Zig kernel
// APs are already running, ready to receive work from BSP
const BootInfo = @import("boot_info.zig").BootInfo;
const c_write_serial = @import("boot_info.zig").c_write_serial;

const MAX_CPUS = 16;

// Shared test data (accessed by all CPUs)
pub var per_cpu_counters: [MAX_CPUS]u64 = [_]u64{0} ** MAX_CPUS;
pub var partial_sums: [MAX_CPUS]u64 = [_]u64{0} ** MAX_CPUS;
pub var total_sum: u64 = 0;

// For now, just simple tests on BSP
// TODO: Implement IPI mechanism to wake APs for parallel work
pub fn run_all(boot_info: *const BootInfo) void {
    c_write_serial("[Zig Test] Running on BSP (CPU 0)...\n\n");

    // Test 1: Simple counter
    c_write_serial("[Test 1] Counting to 1,000,000...\n");
    var i: u64 = 0;
    while (i < 1000000) : (i += 1) {
        per_cpu_counters[0] += 1;
    }
    c_write_serial("[Test 1] Counter reached: ");
    write_dec_u64(per_cpu_counters[0]);
    c_write_serial("\n\n");

    // Test 2: Sum of 1 to 10,000,000
    c_write_serial("[Test 2] Computing sum of 1 to 10,000,000...\n");
    const target: u64 = 10000000;
    var sum: u64 = 0;
    i = 1;
    while (i <= target) : (i += 1) {
        sum += i;
    }

    const expected: u64 = 50000005000000;  // n*(n+1)/2
    c_write_serial("[Test 2] Sum: ");
    write_dec_u64(sum);
    c_write_serial("\n");
    c_write_serial("[Test 2] Expected: ");
    write_dec_u64(expected);
    c_write_serial("\n");

    if (sum == expected) {
        c_write_serial("[Test 2] PASSED ✓\n\n");
    } else {
        c_write_serial("[Test 2] FAILED ✗\n\n");
    }

    // Display CPU count from boot info
    c_write_serial("[Info] Total CPUs available: ");
    write_dec_u32(boot_info.cpu_count);
    c_write_serial("\n");
    c_write_serial("[Info] (Parallel tests will be implemented via IPI mechanism)\n");
}

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
