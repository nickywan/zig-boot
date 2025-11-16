// SMP (Symmetric Multiprocessing) - Boot Application Processors
const serial = @import("serial.zig");
const std = @import("std");

var cpus_online: u32 = 1; // BSP is online

pub fn boot_aps(cpu_count: u32) !void {
    _ = cpu_count;
    // SMP stub - to be fully implemented
    serial.write_string("[SMP] SMP boot implementation pending\n");
    serial.write_string("[SMP] Only BSP active for now\n");
}

pub fn get_online_count() u32 {
    return cpus_online;
}
