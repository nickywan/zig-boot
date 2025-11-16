// Minimal kernel with ACPI detection
// Version: Step 1 - ACPI CPU detection

#define COM1 0x3F8
#define ACPI_SEARCH_START 0x000E0000
#define ACPI_SEARCH_END   0x000FFFFF

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

// Simple printf for numbers
static void print_hex(uint64_t num) {
    const char hex[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    for (i = 15; i >= 0; i--) {
        buf[i] = hex[num & 0xF];
        num >>= 4;
    }
    buf[16] = 0;

    // Skip leading zeros
    for (i = 0; i < 15 && buf[i] == '0'; i++);

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

// ACPI functions
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
            if (lapic->flags & 0x1) {  // Enabled
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

// Kernel entry
void kernel_main(void) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Minimal Kernel - Step 1: ACPI Detection\n");
    puts("===========================================\n");
    puts("\n");

    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
    puts("\n");

    // Find RSDP
    puts("[ACPI] Searching for RSDP...\n");
    struct acpi_rsdp *rsdp = acpi_find_rsdp();

    if (!rsdp) {
        puts("[ERROR] RSDP not found!\n");
        goto halt;
    }

    puts("[ACPI] RSDP found!\n");

    // Find MADT
    puts("[ACPI] Searching for MADT...\n");
    struct acpi_sdt_header *madt = acpi_find_madt(rsdp);

    if (!madt) {
        puts("[ERROR] MADT not found!\n");
        goto halt;
    }

    puts("[ACPI] MADT found!\n");

    // Parse MADT
    puts("[ACPI] Parsing MADT entries...\n");
    int cpu_count = acpi_parse_madt(madt);

    puts("\n[SUCCESS] Detected ");
    print_dec(cpu_count);
    puts(" CPU(s)\n");

halt:
    puts("System halted.\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
