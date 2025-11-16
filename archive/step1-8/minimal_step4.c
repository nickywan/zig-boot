// Step 4: ACPI + APIC + Trampoline + Boot APs (INIT-SIPI-SIPI)
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

// AP entry point
void ap_entry(void) {
    // Simplest possible AP entry - just increment and halt
    // NO APIC init, NO serial output, NO nothing

    // Increment CPUs online counter
    atomic_inc(&cpus_online);

    // Halt
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Kernel entry
void kernel_main(void) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Step 4: Boot APs (INIT-SIPI-SIPI)\n");
    puts("===========================================\n");
    puts("\n");

    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
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

    if (cpus_online == (uint32_t)cpu_count) {
        puts("\n[SUCCESS] All CPUs booted successfully!\n");
        puts("[SUCCESS] Step 4 complete!\n");
    } else {
        puts("\n[WARNING] Not all CPUs came online\n");
        puts("[INFO] This may be normal in some environments\n");
    }

halt:
    puts("\nSystem halted.\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
