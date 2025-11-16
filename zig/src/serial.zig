// Serial port driver for COM1
const std = @import("std");

const COM1_PORT: u16 = 0x3F8;

// x86 I/O port functions
inline fn outb(port: u16, value: u8) void {
    asm volatile ("outb %[value], %[port]"
        :
        : [value] "{al}" (value),
          [port] "N{dx}" (port),
    );
}

inline fn inb(port: u16) u8 {
    return asm volatile ("inb %[port], %[result]"
        : [result] "={al}" (-> u8),
        : [port] "N{dx}" (port),
    );
}

pub fn init() void {
    outb(COM1_PORT + 1, 0x00); // Disable interrupts
    outb(COM1_PORT + 3, 0x80); // Enable DLAB
    outb(COM1_PORT + 0, 0x03); // Divisor low (38400 baud)
    outb(COM1_PORT + 1, 0x00); // Divisor high
    outb(COM1_PORT + 3, 0x03); // 8N1
    outb(COM1_PORT + 2, 0xC7); // Enable FIFO
    outb(COM1_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

fn is_transmit_empty() bool {
    return (inb(COM1_PORT + 5) & 0x20) != 0;
}

pub fn write_byte(byte: u8) void {
    while (!is_transmit_empty()) {}
    outb(COM1_PORT, byte);
}

pub fn write_string(string: []const u8) void {
    for (string) |byte| {
        if (byte == '\n') {
            write_byte('\r');
        }
        write_byte(byte);
    }
}

// Hex printing helpers
const hex_digits = "0123456789ABCDEF";

pub fn write_hex_u64(value: u64) void {
    write_string("0x");
    var i: u6 = 60;
    while (true) : (i -%= 4) {
        const nibble = @as(u8, @truncate((value >> i) & 0xF));
        write_byte(hex_digits[nibble]);
        if (i == 0) break;
    }
}

pub fn write_hex_u32(value: u32) void {
    write_string("0x");
    var i: u5 = 28;
    while (true) : (i -%= 4) {
        const nibble = @as(u8, @truncate((value >> i) & 0xF));
        write_byte(hex_digits[nibble]);
        if (i == 0) break;
    }
}

pub fn write_dec_u64(value: u64) void {
    if (value == 0) {
        write_byte('0');
        return;
    }

    // Initialize buffer to avoid bare-metal stack issues
    var buf: [20]u8 = [_]u8{0} ** 20;
    var i: usize = buf.len;
    var n = value;

    while (n > 0) {
        i -= 1;
        buf[i] = @as(u8, @truncate(n % 10)) + '0';
        n /= 10;
    }

    write_string(buf[i..]);
}

pub fn write_dec_u32(value: u32) void {
    write_dec_u64(value);
}
