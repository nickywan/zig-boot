// Parallel computation tests
const serial = @import("serial.zig");

pub fn run_all(cpu_count: u32) void {
    _ = cpu_count;
    serial.write_string("[TEST] Parallel tests implementation pending\n");
    serial.write_string("[TEST] BSP only for now\n");
}
