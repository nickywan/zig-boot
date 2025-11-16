// Panic handler for bare-metal kernel
const serial = @import("serial.zig");

pub fn panic(msg: []const u8, error_return_trace: ?*@import("std").builtin.StackTrace, ret_addr: ?usize) noreturn {
    _ = error_return_trace;
    _ = ret_addr;

    serial.write_string("\n[PANIC] ");
    serial.write_string(msg);
    serial.write_string("\n");

    while (true) {
        asm volatile ("hlt");
    }
}
