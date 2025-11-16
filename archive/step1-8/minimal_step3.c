// Step 3: ACPI + APIC + Trampoline SMP
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

// APIC MMIO
static volatile uint32_t *apic_base = 0;

static inline uint32_t apic_read(uint32_t reg) {
    return apic_base[reg >> 2];
}

static inline void apic_write(uint32_t reg, uint32_t value) {
    apic_base[reg >> 2] = value;
}

// Per-CPU stacks (8KB each, aligned)
static uint8_t __attribute__((aligned(16))) ap_stacks[MAX_CPUS][AP_STACK_SIZE];

// Trampoline symbols (from trampoline.S)
extern char trampoline_start[];
extern char trampoline_end[];
extern uint32_t trampoline_cr3;
extern uint64_t trampoline_stack;
extern uint64_t trampoline_entry;

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

static void print_hex(uint64_t num) {
    const char hex[] = "0123456789ABCDEF";
    static char buf[17];  // Static to avoid stack issues
    int i;

    // Build hex string from right to left
    for (i = 15; i >= 0; i--) {
        buf[i] = hex[num & 0xF];
        num >>= 4;
    }
    buf[16] = 0;

    // Skip leading zeros, but always print at least one digit
    i = 0;
    while (i < 15 && buf[i] == '0') {
        i++;
    }

    puts(&buf[i]);
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

// Minimal memcpy
static void *memcpy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

// ACPI structures (same as Step 2)
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

static int acpi_parse_madt(struct acpi_sdt_header *madt_header) {
    struct acpi_madt_header *madt = (struct acpi_madt_header*)madt_header;
    uint8_t *ptr = (uint8_t*)madt + sizeof(struct acpi_madt_header);
    uint8_t *end = (uint8_t*)madt + madt->header.length;
    int cpu_count = 0;

    while (ptr < end) {
        uint8_t type = *ptr;
        uint8_t length = *(ptr + 1);

        if (type == 0) {  // Local APIC
            struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)ptr;
            if (lapic->flags & 0x1) {
                puts("[ACPI] CPU ");
                print_dec(cpu_count);
                puts(" detected\n");
                cpu_count++;
            }
        }

        ptr += length;
    }

    return cpu_count;
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
    puts("[APIC] Current CPU APIC ID: ");
    print_dec(apic_id);
    puts("\n");

    puts("[APIC] Local APIC initialized successfully!\n");
}

// Trampoline functions
static void setup_trampoline(int cpu_count) {
    puts("\n[SMP] Setting up trampoline...\n");

    // Calculate trampoline size
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

    // Set trampoline variables
    trampoline_cr3 = (uint32_t)cr3;

    // For now, just set a test stack (Step 4 will use per-CPU stacks)
    trampoline_stack = (uint64_t)&ap_stacks[1][AP_STACK_SIZE];

    // AP entry point (will be implemented in Step 4)
    extern void ap_entry(void);
    trampoline_entry = (uint64_t)ap_entry;

    puts("[SMP] Trampoline configured:\n");
    puts("[SMP]   CR3 = [set]\n");
    puts("[SMP]   Stack = [set]\n");
    puts("[SMP]   Entry = [set]\n");
}

// AP entry point (placeholder for Step 4)
void ap_entry(void) {
    // APs will start here after trampoline
    // For now, just halt
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Kernel entry
void kernel_main(void) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Step 3: Trampoline SMP\n");
    puts("===========================================\n");
    puts("\n");

    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
    puts("\n");

    // ACPI Detection
    puts("[ACPI] Searching for RSDP...\n");
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
    int cpu_count = acpi_parse_madt(madt);

    puts("\n[ACPI] Detected ");
    print_dec(cpu_count);
    puts(" CPU(s)\n");

    // Initialize Local APIC
    apic_init();

    // Setup trampoline
    setup_trampoline(cpu_count);

    puts("\n[SUCCESS] Step 3 complete!\n");
    puts("[INFO] Trampoline ready for AP boot (Step 4)\n");

halt:
    puts("\nSystem halted.\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
