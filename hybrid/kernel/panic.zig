// Panic handler for freestanding Zig
const c_write_serial = @import("boot_info.zig").c_write_serial;

pub fn panic(msg: []const u8, error_return_trace: ?*@import("std").builtin.StackTrace, ret_addr: ?usize) noreturn {
    _ = error_return_trace;
    _ = ret_addr;

    c_write_serial("\n[PANIC] ");
    var null_term_buf: [256]u8 = undefined;
    @memcpy(null_term_buf[0..msg.len], msg);
    null_term_buf[msg.len] = 0;
    c_write_serial(@ptrCast(&null_term_buf));
    c_write_serial("\n");

    while (true) {
        asm volatile ("hlt");
    }
}
