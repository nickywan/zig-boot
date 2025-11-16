// VGA Text Mode Driver
const serial = @import("serial.zig");

pub const VGA_WIDTH = 80;
pub const VGA_HEIGHT = 25;
const VGA_MEMORY: usize = 0xB8000;

// VGA Colors
pub const Color = enum(u8) {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGrey = 7,
    DarkGrey = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    LightMagenta = 13,
    LightBrown = 14,
    White = 15,
};

var row: usize = 0;
var column: usize = 0;
var color: u8 = undefined;
var buffer: [*]volatile u16 = undefined;

inline fn entry_color(fg: Color, bg: Color) u8 {
    return @intFromEnum(fg) | (@intFromEnum(bg) << 4);
}

inline fn entry(uc: u8, col: u8) u16 {
    return @as(u16, uc) | (@as(u16, col) << 8);
}

pub fn init() void {
    row = 0;
    column = 0;
    color = entry_color(Color.LightGrey, Color.Black);
    buffer = @ptrFromInt(VGA_MEMORY);

    // Clear screen
    var y: usize = 0;
    while (y < VGA_HEIGHT) : (y += 1) {
        var x: usize = 0;
        while (x < VGA_WIDTH) : (x += 1) {
            const index = y * VGA_WIDTH + x;
            buffer[index] = entry(' ', color);
        }
    }
}

pub fn set_color(col: u8) void {
    color = col;
}

fn scroll() void {
    // Move all lines up by one
    var y: usize = 0;
    while (y < VGA_HEIGHT - 1) : (y += 1) {
        var x: usize = 0;
        while (x < VGA_WIDTH) : (x += 1) {
            buffer[y * VGA_WIDTH + x] = buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    // Clear last line
    var x: usize = 0;
    while (x < VGA_WIDTH) : (x += 1) {
        buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = entry(' ', color);
    }

    row = VGA_HEIGHT - 1;
}

fn putentryat(c: u8, col: u8, x: usize, y: usize) void {
    const index = y * VGA_WIDTH + x;
    buffer[index] = entry(c, col);
}

pub fn putchar(c: u8) void {
    if (c == '\n') {
        column = 0;
        row += 1;
        if (row == VGA_HEIGHT) {
            scroll();
        }
        return;
    }

    if (c == '\r') {
        column = 0;
        return;
    }

    putentryat(c, color, column, row);

    column += 1;
    if (column == VGA_WIDTH) {
        column = 0;
        row += 1;
        if (row == VGA_HEIGHT) {
            scroll();
        }
    }
}

pub fn write_string(string: []const u8) void {
    for (string) |byte| {
        putchar(byte);
    }
}

pub fn write_hex_u64(value: u64) void {
    const hex_digits = "0123456789ABCDEF";
    write_string("0x");

    var i: u6 = 60;
    while (true) : (i -%= 4) {
        const nibble = @as(u8, @truncate((value >> i) & 0xF));
        putchar(hex_digits[nibble]);
        if (i == 0) break;
    }
}

pub fn write_dec_u64(value: u64) void {
    if (value == 0) {
        putchar('0');
        return;
    }

    // Initialize buffer to avoid bare-metal stack issues
    var buf: [20]u8 = [_]u8{0} ** 20;
    var idx: usize = buf.len;
    var n = value;

    while (n > 0) {
        idx -= 1;
        buf[idx] = @as(u8, @truncate(n % 10)) + '0';
        n /= 10;
    }

    write_string(buf[idx..]);
}
