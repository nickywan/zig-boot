#ifndef VGA_H
#define VGA_H

// Bare-metal types (no standard headers with -nostdinc)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned long size_t;

// VGA Text Mode
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// VGA Colors
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_color;
static uint16_t* vga_buffer;

void vga_init(void) {
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t*) VGA_MEMORY;

    // Clear screen
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_color);
        }
    }
}

void vga_setcolor(uint8_t color) {
    vga_color = color;
}

void vga_scroll(void) {
    // Move all lines up by one
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    // Clear last line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    }

    vga_row = VGA_HEIGHT - 1;
}

void vga_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    vga_buffer[index] = vga_entry(c, color);
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    if (c == '\r') {
        vga_column = 0;
        return;
    }

    vga_putentryat(c, vga_color, vga_column, vga_row);

    if (++vga_column == VGA_WIDTH) {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

void vga_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}

void vga_writestring(const char* data) {
    size_t len = 0;
    while (data[len]) len++;
    vga_write(data, len);
}

void vga_write_hex(uint64_t value) {
    const char* hex = "0123456789ABCDEF";
    char buf[19]; // "0x" + 16 hex digits + null
    buf[0] = '0';
    buf[1] = 'x';

    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(value >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';

    vga_writestring(buf);
}

void vga_write_dec(uint64_t value) {
    if (value == 0) {
        vga_putchar('0');
        return;
    }

    char buf[21]; // Max uint64_t is 20 digits + null
    int i = 19;
    buf[20] = '\0';

    while (value > 0) {
        buf[i--] = '0' + (value % 10);
        value /= 10;
    }

    vga_writestring(&buf[i + 1]);
}

#endif // VGA_H
