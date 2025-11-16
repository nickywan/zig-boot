// Step 6: IDT Setup (Interrupt Descriptor Table)
// Tested on QEMU TCG and KVM

#define COM1 0x3F8
#define ACPI_SEARCH_START 0x000E0000
#define ACPI_SEARCH_END   0x000FFFFF
#define MAX_CPUS 16
#define AP_STACK_SIZE 8192  // 8KB per CPU

// APIC MSR and registers
#define APIC_BASE_MSR     0x1B
#define APIC_ID_REG       0x20
#define APIC_SVR_REG      0xF0
#define APIC_ENABLE       0x100
#define APIC_ICR_LOW      0x300
#define APIC_ICR_HIGH     0x310

// IPI types (from Linux)
#define APIC_DM_INIT          0x00000500
#define APIC_DM_STARTUP       0x00000600
#define APIC_INT_LEVELTRIG    0x00008000
#define APIC_INT_ASSERT       0x00004000
#define APIC_DEST_PHYSICAL    0x00000000

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

// Port I/O
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// MSR operations
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// TSC (Time Stamp Counter)
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// APIC MMIO
static volatile uint32_t *apic_base = 0;

static inline uint32_t apic_read(uint32_t reg) {
    return apic_base[reg >> 2];
}

static inline void apic_write(uint32_t reg, uint32_t value) {
    apic_base[reg >> 2] = value;
}

// Atomic operations
static volatile uint32_t cpus_online = 0;

static inline void atomic_inc(volatile uint32_t *ptr) {
    __asm__ volatile("lock incl %0" : "+m"(*ptr) : : "memory");
}

// Per-CPU stacks (8KB each, aligned)
static uint8_t __attribute__((aligned(16))) ap_stacks[MAX_CPUS][AP_STACK_SIZE];

// Trampoline symbols (from trampoline.S)
extern char trampoline_start[];
extern char trampoline_end[];

// ============================================================================
// PARALLEL COMPUTATION DATA STRUCTURES
// ============================================================================

// Test 1: Parallel counters
static volatile uint64_t per_cpu_counters[MAX_CPUS] = {0};

// Test 2: Distributed sum (sum of 1 to 10,000,000)
#define SUM_TARGET 10000000UL
static volatile uint64_t partial_sums[MAX_CPUS] = {0};
static volatile uint64_t total_sum = 0;

// Test 3: Barrier synchronization
static volatile uint32_t barrier_count = 0;
static volatile uint32_t barrier_sense = 0;

// ============================================================================
// IDT (INTERRUPT DESCRIPTOR TABLE)
// ============================================================================

// IDT Gate Descriptor (64-bit mode)
struct idt_entry {
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  ist;           // Interrupt Stack Table (0 = don't use)
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_mid;    // Offset bits 16-31
    uint32_t offset_high;   // Offset bits 32-63
    uint32_t zero;          // Reserved (must be 0)
} __attribute__((packed));

typedef struct idt_entry idt_entry_t;

// IDT Pointer (IDTR)
struct idt_ptr {
    uint16_t limit;         // Size of IDT - 1
    uint64_t base;          // Base address of IDT
} __attribute__((packed));

typedef struct idt_ptr idt_ptr_t;

// IDT with 256 entries
static idt_entry_t idt[256] __attribute__((aligned(16)));
static idt_ptr_t idtr;

// Exception names
static const char *exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

// Exception counter
static volatile uint32_t exception_count = 0;

// ============================================================================
// IDT FUNCTIONS
// ============================================================================

// Forward declarations (serial functions needed by exception handlers)
static void puts(const char *s);
static void print_hex(uint32_t n);
static void print_hex_64(uint64_t n);

// Set an IDT entry
static void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;  // No IST for now
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

// Generic exception handler (called from assembly stubs)
__attribute__((used))
static void exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {
    __atomic_fetch_add(&exception_count, 1, __ATOMIC_SEQ_CST);

    puts("\n[EXCEPTION] ");
    if (vector < 32) {
        puts(exception_names[vector]);
    } else {
        puts("Unknown Exception");
    }
    puts(" (Vector ");
    print_hex(vector);
    puts(")\n");

    puts("  Error Code: ");
    print_hex_64(error_code);
    puts("\n");

    puts("  RIP: ");
    print_hex_64(rip);
    puts("\n");

    // For now, just halt
    puts("[HALT] System halted due to exception\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Exception stub macros - these will be our exception entry points
#define EXCEPTION_STUB_NOERR(num) \
    __attribute__((naked)) void exception_stub_##num(void) { \
        __asm__ volatile( \
            "pushq $0\n\t"          /* Push dummy error code */ \
            "pushq $" #num "\n\t"   /* Push exception number */ \
            "jmp exception_common\n\t" \
            ::: "memory" \
        ); \
    }

// Exceptions with error code (CPU pushes it automatically)
#define EXCEPTION_STUB_ERR(num) \
    __attribute__((naked)) void exception_stub_##num(void) { \
        __asm__ volatile( \
            "pushq $" #num "\n\t"   /* Push exception number */ \
            "jmp exception_common\n\t" \
            ::: "memory" \
        ); \
    }

// Common exception handler (saves state and calls C handler)
__attribute__((naked)) void exception_common(void) {
    __asm__ volatile(
        // Stack at entry (from top to bottom):
        // RIP (pushed by CPU)
        // CS (pushed by CPU)
        // RFLAGS (pushed by CPU)
        // RSP (pushed by CPU)
        // SS (pushed by CPU)
        // Error code (pushed by stub or CPU)
        // Vector number (pushed by stub)

        // Save registers
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        "push %%rdx\n"
        "push %%rsi\n"
        "push %%rdi\n"
        "push %%rbp\n"
        "push %%r8\n"
        "push %%r9\n"
        "push %%r10\n"
        "push %%r11\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"

        // Get vector and error code from stack
        "mov 15*8(%%rsp), %%rdi\n"    // vector (arg 1)
        "mov 16*8(%%rsp), %%rsi\n"    // error_code (arg 2)
        "mov 17*8(%%rsp), %%rdx\n"    // RIP (arg 3)

        // Call C handler
        "call exception_handler\n"

        // Restore registers (we'll never get here if handler halts)
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"
        "pop %%rbp\n"
        "pop %%rdi\n"
        "pop %%rsi\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"

        // Remove vector and error code
        "add $16, %%rsp\n"

        // Return from interrupt
        "iretq\n"
        ::: "memory"
    );
}

// Define exception stubs for all CPU exceptions (0-31)
EXCEPTION_STUB_NOERR(0)   // Division By Zero
EXCEPTION_STUB_NOERR(1)   // Debug
EXCEPTION_STUB_NOERR(2)   // NMI
EXCEPTION_STUB_NOERR(3)   // Breakpoint
EXCEPTION_STUB_NOERR(4)   // Overflow
EXCEPTION_STUB_NOERR(5)   // Bound Range Exceeded
EXCEPTION_STUB_NOERR(6)   // Invalid Opcode
EXCEPTION_STUB_NOERR(7)   // Device Not Available
EXCEPTION_STUB_ERR(8)     // Double Fault
EXCEPTION_STUB_NOERR(9)   // Coprocessor Segment Overrun
EXCEPTION_STUB_ERR(10)    // Invalid TSS
EXCEPTION_STUB_ERR(11)    // Segment Not Present
EXCEPTION_STUB_ERR(12)    // Stack-Segment Fault
EXCEPTION_STUB_ERR(13)    // General Protection Fault
EXCEPTION_STUB_ERR(14)    // Page Fault
EXCEPTION_STUB_NOERR(15)  // Reserved
EXCEPTION_STUB_NOERR(16)  // x87 FPU Error
EXCEPTION_STUB_ERR(17)    // Alignment Check
EXCEPTION_STUB_NOERR(18)  // Machine Check
EXCEPTION_STUB_NOERR(19)  // SIMD Exception
EXCEPTION_STUB_NOERR(20)  // Virtualization Exception
EXCEPTION_STUB_ERR(21)    // Control Protection Exception
EXCEPTION_STUB_NOERR(22)  // Reserved
EXCEPTION_STUB_NOERR(23)  // Reserved
EXCEPTION_STUB_NOERR(24)  // Reserved
EXCEPTION_STUB_NOERR(25)  // Reserved
EXCEPTION_STUB_NOERR(26)  // Reserved
EXCEPTION_STUB_NOERR(27)  // Reserved
EXCEPTION_STUB_NOERR(28)  // Hypervisor Injection Exception
EXCEPTION_STUB_NOERR(29)  // VMM Communication Exception
EXCEPTION_STUB_ERR(30)    // Security Exception
EXCEPTION_STUB_NOERR(31)  // Reserved

// Function pointer array for exception stubs - marked volatile to prevent optimization issues
static void (* volatile exception_stubs[32])(void) = {
    exception_stub_0, exception_stub_1, exception_stub_2, exception_stub_3,
    exception_stub_4, exception_stub_5, exception_stub_6, exception_stub_7,
    exception_stub_8, exception_stub_9, exception_stub_10, exception_stub_11,
    exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15,
    exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19,
    exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23,
    exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27,
    exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31
};

// Load IDT
static void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

// Initialize IDT
static void idt_init(void) {
    // Clear IDT
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].zero = 0;
    }

    // Set up exception handlers (0-31)
    // Type: 0x8E = Present, DPL=0, Type=Interrupt Gate (32-bit)
    // In 64-bit mode: 0x8E for interrupt gate
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)exception_stubs[i], 0x08, 0x8E);
    }

    // Set up IDTR
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    // Load IDT
    idt_load();
}

// Serial functions
static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static void puts(const char *s) {
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

static void print_dec(uint32_t num) {
    char buf[12];
    int i = 11;
    buf[i--] = 0;

    if (num == 0) {
        putc('0');
        return;
    }

    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    puts(&buf[i + 1]);
}

static void print_dec_64(uint64_t num) {
    char buf[24];
    int i = 23;
    buf[i--] = 0;

    if (num == 0) {
        putc('0');
        return;
    }

    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    puts(&buf[i + 1]);
}

static void print_hex(uint32_t num) {
    char buf[12];
    buf[0] = '0';
    buf[1] = 'x';
    int i;
    for (i = 0; i < 8; i++) {
        int nibble = (num >> (28 - i * 4)) & 0xF;
        buf[i + 2] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buf[10] = 0;
    puts(buf);
}

static void print_hex_64(uint64_t num) {
    char buf[20];
    buf[0] = '0';
    buf[1] = 'x';
    int i;
    for (i = 0; i < 16; i++) {
        int nibble = (num >> (60 - i * 4)) & 0xF;
        buf[i + 2] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buf[18] = 0;
    puts(buf);
}

// Minimal memcpy
static void *memcpy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

// TSC calibration and delays
static uint64_t tsc_khz = 0;

static void calibrate_tsc(void) {
    // For this minimal kernel, we use a simple fixed estimate
    // TCG: ~500 MHz, KVM: ~2-6 GHz
    // We'll use 2GHz as a reasonable middle ground
    // The delays don't need to be exact for SMP boot to work

    tsc_khz = 2000000;  // 2 GHz = 2,000,000 kHz
}

// Ultra-simple delays - NO I/O operations during SMP boot!
static void udelay(uint64_t usec) {
    // Simple busy loop - very approximate
    for (volatile uint64_t i = 0; i < usec * 10; i++) {
        __asm__ volatile("pause");
    }
}

static void mdelay(uint64_t msec) {
    for (uint64_t i = 0; i < msec; i++) {
        udelay(1000);
    }
}

// ACPI structures
struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt_header {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_lapic {
    uint8_t type;
    uint8_t length;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

static int acpi_checksum(void *ptr, int length) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < length; i++)
        sum += p[i];
    return (sum == 0);
}

static struct acpi_rsdp *acpi_find_rsdp(void) {
    uint8_t *ptr = (uint8_t*)ACPI_SEARCH_START;

    for (; ptr < (uint8_t*)ACPI_SEARCH_END; ptr += 16) {
        if (ptr[0] == 'R' && ptr[1] == 'S' && ptr[2] == 'D' &&
            ptr[3] == ' ' && ptr[4] == 'P' && ptr[5] == 'T' &&
            ptr[6] == 'R' && ptr[7] == ' ') {

            struct acpi_rsdp *rsdp = (struct acpi_rsdp*)ptr;
            if (acpi_checksum(rsdp, 20)) {
                return rsdp;
            }
        }
    }
    return 0;
}

static struct acpi_sdt_header *acpi_find_madt(struct acpi_rsdp *rsdp) {
    struct acpi_sdt_header *rsdt;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        rsdt = (struct acpi_sdt_header*)(uint64_t)rsdp->xsdt_address;
    } else {
        rsdt = (struct acpi_sdt_header*)(uint64_t)rsdp->rsdt_address;
    }

    if (!acpi_checksum(rsdt, rsdt->length)) {
        return 0;
    }

    int entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    uint32_t *entry_ptr = (uint32_t*)((uint8_t*)rsdt + sizeof(struct acpi_sdt_header));

    for (int i = 0; i < entries; i++) {
        struct acpi_sdt_header *header = (struct acpi_sdt_header*)(uint64_t)entry_ptr[i];
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            return header;
        }
    }

    return 0;
}

// CPU tracking
static uint8_t cpu_apic_ids[MAX_CPUS];
static int cpu_count = 0;

static int acpi_parse_madt(struct acpi_sdt_header *madt_header) {
    struct acpi_madt_header *madt = (struct acpi_madt_header*)madt_header;
    uint8_t *ptr = (uint8_t*)madt + sizeof(struct acpi_madt_header);
    uint8_t *end = (uint8_t*)madt + madt->header.length;
    int count = 0;

    while (ptr < end) {
        uint8_t type = *ptr;
        uint8_t length = *(ptr + 1);

        if (type == 0) {  // Local APIC
            struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)ptr;
            if (lapic->flags & 0x1) {
                puts("[ACPI] CPU ");
                print_dec(count);
                puts(" detected (APIC ID ");
                print_dec(lapic->apic_id);
                puts(")\n");

                if (count < MAX_CPUS) {
                    cpu_apic_ids[count] = lapic->apic_id;
                }
                count++;
            }
        }

        ptr += length;
    }

    return count;
}

// APIC functions
static void apic_init(void) {
    puts("\n[APIC] Initializing Local APIC...\n");

    uint64_t apic_msr = rdmsr(APIC_BASE_MSR);
    uint64_t apic_phys_addr = apic_msr & 0xFFFFF000;

    puts("[APIC] Physical address: 0xFEE00000 (default)\n");

    apic_base = (volatile uint32_t*)apic_phys_addr;

    if (!(apic_msr & (1 << 11))) {
        puts("[APIC] Enabling APIC in MSR...\n");
        wrmsr(APIC_BASE_MSR, apic_msr | (1 << 11));
    }

    uint32_t svr = apic_read(APIC_SVR_REG);
    puts("[APIC] Enabling APIC (SVR register)...\n");

    apic_write(APIC_SVR_REG, svr | APIC_ENABLE);

    uint32_t apic_id = apic_read(APIC_ID_REG) >> 24;
    puts("[APIC] BSP APIC ID: ");
    print_dec(apic_id);
    puts("\n");

    puts("[APIC] Local APIC initialized successfully!\n");
}

// IPI functions
static void apic_wait_icr(void) {
    uint32_t timeout = 1000000;
    while ((apic_read(APIC_ICR_LOW) & (1 << 12)) && timeout--) {
        __asm__ volatile("pause");
    }
}

static void send_ipi(uint32_t apic_id, uint32_t flags) {
    apic_wait_icr();
    apic_write(APIC_ICR_HIGH, apic_id << 24);
    apic_write(APIC_ICR_LOW, flags);
    apic_wait_icr();
}

// Trampoline functions
static void setup_trampoline(void) {
    puts("\n[SMP] Setting up trampoline...\n");

    uint64_t trampoline_size = trampoline_end - trampoline_start;
    puts("[SMP] Trampoline size: ");
    print_dec(trampoline_size);
    puts(" bytes\n");

    // Copy trampoline to 0x8000
    uint8_t *dest = (uint8_t*)0x8000;
    memcpy(dest, trampoline_start, trampoline_size);

    puts("[SMP] Trampoline copied to 0x8000\n");

    // Get current CR3
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // Patch trampoline variables at end (like linux-minimal)
    // Offsets from end: -24: cr3, -16: stack, -8: entry
    uint64_t *cr3_ptr = (uint64_t*)(0x8000 + trampoline_size - 24);
    uint64_t *stack_ptr = (uint64_t*)(0x8000 + trampoline_size - 16);
    uint64_t *entry_ptr = (uint64_t*)(0x8000 + trampoline_size - 8);

    *cr3_ptr = cr3;
    *stack_ptr = 0;  // Will be patched per-AP
    extern void ap_entry(void);
    *entry_ptr = (uint64_t)ap_entry;

    // CRITICAL: Flush caches after patching
    __asm__ volatile("wbinvd" ::: "memory");

    puts("[SMP] Trampoline configured\n");
}

// Boot a single AP (adapted from Linux smpboot.c)
static void boot_ap(int cpu_idx) {
    if (cpu_idx == 0) return;  // Skip BSP
    if (cpu_idx >= cpu_count) return;

    uint8_t apic_id = cpu_apic_ids[cpu_idx];
    unsigned long start_eip = 0x8000;  // Trampoline address

    // Patch per-CPU stack (like linux-minimal)
    uint64_t trampoline_size = trampoline_end - trampoline_start;
    uint64_t *stack_ptr = (uint64_t*)(0x8000 + trampoline_size - 16);
    *stack_ptr = (uint64_t)&ap_stacks[cpu_idx][AP_STACK_SIZE];

    // CRITICAL: Flush caches after patching
    __asm__ volatile("wbinvd" ::: "memory");

    // INIT-SIPI-SIPI sequence
    // INIT IPI
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
    apic_wait_icr();

    // Wait 10ms
    mdelay(10);

    // INIT deassert
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_DM_INIT);
    apic_wait_icr();

    // SIPI #1
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();

    // Wait 200μs
    udelay(200);

    // SIPI #2
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();

    // Wait 200μs
    udelay(200);
}

// Boot all APs
static void boot_all_aps(void) {
    // NO SERIAL OUTPUT AT ALL in this function - it's fragile!

    // BSP is already online
    cpus_online = 1;

    // Boot all APs
    for (int i = 1; i < cpu_count; i++) {
        boot_ap(i);
    }

    // Wait for APs
    for (volatile int i = 0; i < 1000000; i++) __asm__ volatile("pause");

    // NO PUTS HERE! Move to kernel_main after this function returns
}

// ============================================================================
// PARALLEL COMPUTATION FUNCTIONS
// ============================================================================

// Simple barrier (sense-reversing)
static void barrier_wait(int cpu_id) {
    uint32_t my_sense = barrier_sense;

    // Last CPU to arrive flips the sense
    uint32_t arrived = __atomic_add_fetch(&barrier_count, 1, __ATOMIC_SEQ_CST);

    if (arrived == (uint32_t)cpu_count) {
        // Last one - reset and flip sense
        barrier_count = 0;
        __atomic_store_n(&barrier_sense, !my_sense, __ATOMIC_SEQ_CST);
    } else {
        // Wait for sense to flip
        while (__atomic_load_n(&barrier_sense, __ATOMIC_SEQ_CST) == my_sense) {
            __asm__ volatile("pause");
        }
    }
}

// Test 1: Parallel counter (each CPU counts to 1 million)
static void test_parallel_counters(int cpu_id) {
    for (uint64_t i = 0; i < 1000000; i++) {
        per_cpu_counters[cpu_id]++;
        if (i % 100000 == 0) {
            __asm__ volatile("pause");
        }
    }
}

// Test 2: Distributed sum (divide work among CPUs)
static void test_distributed_sum(int cpu_id) {
    uint64_t per_cpu_work = SUM_TARGET / cpu_count;
    uint64_t start = cpu_id * per_cpu_work + 1;
    uint64_t end = (cpu_id + 1) * per_cpu_work;

    // Last CPU handles remainder
    if (cpu_id == cpu_count - 1) {
        end = SUM_TARGET;
    }

    uint64_t local_sum = 0;
    for (uint64_t i = start; i <= end; i++) {
        local_sum += i;
    }

    partial_sums[cpu_id] = local_sum;

    // Atomically add to total
    __atomic_add_fetch(&total_sum, local_sum, __ATOMIC_SEQ_CST);
}

// Test 3: Barrier synchronization test
static void test_barrier_sync(int cpu_id) {
    // Phase 1: Count to 500k
    for (uint64_t i = 0; i < 500000; i++) {
        per_cpu_counters[cpu_id]++;
    }

    // Barrier: wait for all CPUs
    barrier_wait(cpu_id);

    // Phase 2: Count to 1M (everyone starts together)
    for (uint64_t i = 500000; i < 1000000; i++) {
        per_cpu_counters[cpu_id]++;
    }
}

// AP entry point - now with parallel computation!
void ap_entry(void) {
    // Load IDT on this AP (IDT is already set up by BSP)
    idt_load();

    // Increment CPUs online counter
    uint32_t my_id = __atomic_fetch_add(&cpus_online, 1, __ATOMIC_SEQ_CST);

    // Wait a bit for BSP to finish setup
    for (volatile int i = 0; i < 100000; i++) __asm__ volatile("pause");

    // Test 1: Parallel counters
    test_parallel_counters(my_id);
    barrier_wait(my_id);  // Sync before next test

    // Test 2: Distributed sum
    test_distributed_sum(my_id);
    barrier_wait(my_id);  // Sync before next test

    // Test 3: Barrier sync (resets counters first)
    per_cpu_counters[my_id] = 0;  // Reset for test 3
    barrier_wait(my_id);  // Everyone resets together
    test_barrier_sync(my_id);

    // Done - halt
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Kernel entry
void kernel_main(void) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Step 6: IDT Setup\n");
    puts("===========================================\n");
    puts("\n");

    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
    puts("\n");

    // Initialize IDT
    puts("[IDT] Initializing Interrupt Descriptor Table...\n");
    idt_init();
    puts("[IDT] IDT initialized with 32 exception handlers\n");
    puts("[IDT] IDT loaded successfully!\n");
    puts("\n");

    // TSC Calibration
    puts("[TSC] Calibrating Time Stamp Counter...\n");
    calibrate_tsc();
    puts("[TSC] TSC frequency: ");
    print_dec(tsc_khz);
    puts(" kHz\n");

    // ACPI Detection
    puts("\n[ACPI] Searching for RSDP...\n");
    struct acpi_rsdp *rsdp = acpi_find_rsdp();

    if (!rsdp) {
        puts("[ERROR] RSDP not found!\n");
        goto halt;
    }

    puts("[ACPI] RSDP found!\n");

    puts("[ACPI] Searching for MADT...\n");
    struct acpi_sdt_header *madt = acpi_find_madt(rsdp);

    if (!madt) {
        puts("[ERROR] MADT not found!\n");
        goto halt;
    }

    puts("[ACPI] MADT found!\n");
    puts("[ACPI] Parsing MADT entries...\n");
    cpu_count = acpi_parse_madt(madt);

    puts("\n[ACPI] Detected ");
    print_dec(cpu_count);
    puts(" CPU(s)\n");

    // Initialize Local APIC
    apic_init();

    // Setup trampoline
    setup_trampoline();

    // Boot APs
    puts("\n[SMP] Starting AP boot sequence...\n");
    boot_all_aps();

    // NOW we can print (SMP boot period is over)
    puts("\n[SMP] Application Processors booted\n");
    puts("[SMP] CPUs online: ");
    print_dec(cpus_online);
    puts(" / ");
    print_dec(cpu_count);
    puts("\n");

    if (cpus_online != (uint32_t)cpu_count) {
        puts("\n[WARNING] Not all CPUs came online\n");
        puts("[INFO] This may be normal in some environments\n");
    } else {
        puts("\n[SUCCESS] All CPUs booted successfully!\n");
    }

    // ========================================================================
    // PARALLEL COMPUTATION TESTS
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  Running Parallel Computation Tests\n");
    puts("===========================================\n");
    puts("\n");

    // Wait a bit for APs to finish boot
    puts("[TEST] Waiting for APs to initialize...\n");
    for (volatile int i = 0; i < 500000; i++) __asm__ volatile("pause");

    // BSP (CPU 0) runs tests too!
    puts("[TEST] BSP running tests...\n");

    // Test 1: Parallel counters
    test_parallel_counters(0);
    barrier_wait(0);  // Sync with APs

    // Test 2: Distributed sum
    test_distributed_sum(0);
    barrier_wait(0);  // Sync with APs

    // Test 3: Barrier sync (reset counters first)
    per_cpu_counters[0] = 0;
    barrier_wait(0);  // Everyone resets together
    test_barrier_sync(0);

    puts("[TEST] All tests completed!\n");

    // ========================================================================
    // DISPLAY RESULTS
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  Test Results\n");
    puts("===========================================\n");
    puts("\n");

    // Test 1: Parallel Counters
    puts("TEST 1: Parallel Counters\n");
    puts("---------------------------\n");
    for (int i = 0; i < cpu_count; i++) {
        puts("  CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(per_cpu_counters[i]);
        if (per_cpu_counters[i] == 1000000) {
            puts(" [OK]\n");
        } else {
            puts(" [FAIL]\n");
        }
    }

    // Test 2: Distributed Sum
    puts("\nTEST 2: Distributed Sum (1 to 10,000,000)\n");
    puts("-------------------------------------------\n");

    // Expected sum: n * (n+1) / 2 = 10000000 * 10000001 / 2
    uint64_t expected_sum = 50000005000000UL;

    puts("  Partial sums:\n");
    for (int i = 0; i < cpu_count; i++) {
        puts("    CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(partial_sums[i]);
        puts("\n");
    }

    puts("  Total sum: ");
    print_dec_64(total_sum);
    puts("\n");

    puts("  Expected:  ");
    print_dec_64(expected_sum);
    puts("\n");

    if (total_sum == expected_sum) {
        puts("  [OK] Sum is correct!\n");
    } else {
        puts("  [FAIL] Sum mismatch!\n");
    }

    // Test 3: Barrier Synchronization
    puts("\nTEST 3: Barrier Synchronization\n");
    puts("---------------------------------\n");
    puts("  (All CPUs should reach 1M after barrier)\n");

    int barrier_ok = 1;
    for (int i = 0; i < cpu_count; i++) {
        puts("  CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(per_cpu_counters[i]);
        if (per_cpu_counters[i] != 1000000) {
            puts(" [FAIL]\n");
            barrier_ok = 0;
        } else {
            puts(" [OK]\n");
        }
    }

    if (barrier_ok) {
        puts("  [OK] Barrier synchronization worked!\n");
    } else {
        puts("  [FAIL] Some CPUs didn't reach barrier\n");
    }

    // Final status
    puts("\n");
    puts("===========================================\n");
    if (total_sum == expected_sum && barrier_ok) {
        puts("[SUCCESS] All parallel tests passed!\n");
    } else {
        puts("[WARNING] Some tests failed\n");
    }
    puts("===========================================\n");

    // ========================================================================
    // IDT TEST: Trigger a division by zero exception
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  IDT Exception Test\n");
    puts("===========================================\n");
    puts("\n");
    puts("[IDT TEST] Testing exception handling...\n");
    puts("[IDT TEST] Triggering division by zero exception...\n");
    puts("[IDT TEST] This should be caught by the IDT handler!\n");
    puts("\n");

    // Trigger division by zero
    volatile int zero = 0;
    volatile int result = 42 / zero;
    (void)result;  // Prevent optimization

    // We should never reach here if the exception was handled
    puts("[ERROR] Exception was not caught! IDT failed!\n");

halt:
    puts("\nSystem halted.\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
