// Ultra-minimal kernel - Just print "Hello" and halt

#define COM1 0x3F8

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

// Port I/O
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Serial init
static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

// Print char
static void putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

// Print string
static void puts(const char *s) {
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

// Kernel entry
void kernel_main(void) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Ultra-Minimal 64-bit Kernel\n");
    puts("===========================================\n");
    puts("\n");
    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
    puts("[OK] Kernel started successfully!\n");
    puts("\n");
    puts("Hello from minimal kernel!\n");
    puts("\n");
    puts("System halted.\n");

    // Halt forever
    while (1) {
        __asm__ volatile("hlt");
    }
}
