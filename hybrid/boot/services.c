// C services that Zig kernel can call back to
#include <stdint.h>

// Serial port I/O
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write string to serial port
void c_write_serial(const char* str) {
    const uint16_t COM1 = 0x3F8;
    while (*str) {
        // Wait for transmit buffer to be empty
        while ((inb(COM1 + 5) & 0x20) == 0);
        outb(COM1, *str++);
    }
}

// Write hex value to serial
void c_write_serial_hex(uint64_t value) {
    const char* hex = "0123456789ABCDEF";
    c_write_serial("0x");

    for (int i = 60; i >= 0; i -= 4) {
        char digit = hex[(value >> i) & 0xF];
        c_write_serial(&digit);
    }
}

// Send End-Of-Interrupt to APIC
extern int use_x2apic;  // Defined in init.c

void c_send_eoi(void) {
    if (use_x2apic) {
        // x2APIC: write 0 to EOI MSR (0x80B)
        uint32_t msr = 0x80B;
        __asm__ volatile ("wrmsr" : : "c"(msr), "a"(0), "d"(0));
    } else {
        // xAPIC: write 0 to EOI MMIO register
        volatile uint32_t* apic_eoi = (volatile uint32_t*)(0xFEE00000 + 0xB0);
        *apic_eoi = 0;
    }
}

// Memory allocation functions (from init.c)
extern void* kmalloc(uint64_t size);
extern void kfree(void* ptr);

// Expose kmalloc to Zig
void* c_kmalloc(uint64_t size) {
    return kmalloc(size);
}

// Expose kfree to Zig
void c_kfree(void* ptr) {
    kfree(ptr);
}
