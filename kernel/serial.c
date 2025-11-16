#include "../include/serial.h"
#include "../include/types.h"

// Port I/O helpers
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);  // Disable interrupts
    outb(COM1 + 3, 0x80);  // Enable DLAB
    outb(COM1 + 0, 0x03);  // Divisor low byte (38400 baud)
    outb(COM1 + 1, 0x00);  // Divisor high byte
    outb(COM1 + 3, 0x03);  // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);  // Enable FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);  // IRQs enabled, RTS/DSR set
}

void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

// Simple printf implementation
static void print_num(uint64_t num, int base) {
    char buf[32];
    int i = 0;

    if (num == 0) {
        serial_putc('0');
        return;
    }

    while (num > 0) {
        int digit = num % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        num /= base;
    }

    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

void serial_printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = __builtin_va_arg(args, int);
                    if (val < 0) {
                        serial_putc('-');
                        val = -val;
                    }
                    print_num(val, 10);
                    break;
                }
                case 'u': {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    print_num(val, 10);
                    break;
                }
                case 'x': {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    print_num(val, 16);
                    break;
                }
                case 'l': {
                    fmt++;
                    if (*fmt == 'u') {
                        uint64_t val = __builtin_va_arg(args, uint64_t);
                        print_num(val, 10);
                    } else if (*fmt == 'x') {
                        uint64_t val = __builtin_va_arg(args, uint64_t);
                        print_num(val, 16);
                    }
                    break;
                }
                case 's': {
                    char *s = __builtin_va_arg(args, char*);
                    serial_puts(s);
                    break;
                }
                case '%':
                    serial_putc('%');
                    break;
            }
        } else {
            if (*fmt == '\n') serial_putc('\r');
            serial_putc(*fmt);
        }
        fmt++;
    }

    __builtin_va_end(args);
}
