#include "../include/acpi.h"
#include "../include/serial.h"
#include "../include/types.h"

#define ACPI_SEARCH_START 0xE0000
#define ACPI_SEARCH_END   0xFFFFF

static acpi_rsdp_t *rsdp = NULL;
static acpi_sdt_header_t *madt = NULL;
static uint8_t cpu_apic_ids[16];
static int cpu_count = 0;

// Helper: Verify checksum
static int acpi_checksum(void *ptr, int length) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < length; i++)
        sum += p[i];
    return (sum == 0);
}

// Search for RSDP in BIOS memory area
static acpi_rsdp_t *acpi_find_rsdp(void) {
    uint8_t *ptr = (uint8_t*)ACPI_SEARCH_START;

    for (; ptr < (uint8_t*)ACPI_SEARCH_END; ptr += 16) {
        if (ptr[0] == 'R' && ptr[1] == 'S' && ptr[2] == 'D' &&
            ptr[3] == ' ' && ptr[4] == 'P' && ptr[5] == 'T' &&
            ptr[6] == 'R' && ptr[7] == ' ') {

            acpi_rsdp_t *rsdp_candidate = (acpi_rsdp_t*)ptr;
            if (acpi_checksum(rsdp_candidate, 20)) {
                return rsdp_candidate;
            }
        }
    }
    return NULL;
}

// Find MADT table
static acpi_sdt_header_t *acpi_find_madt(acpi_rsdp_t *rsdp) {
    acpi_sdt_header_t *rsdt;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        // Use XSDT (64-bit)
        rsdt = (acpi_sdt_header_t*)(uintptr_t)rsdp->xsdt_address;
    } else {
        // Use RSDT (32-bit)
        rsdt = (acpi_sdt_header_t*)(uintptr_t)rsdp->rsdt_address;
    }

    if (!acpi_checksum(rsdt, rsdt->length)) {
        serial_puts("[ACPI] RSDT checksum failed\n");
        return NULL;
    }

    int entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    uint32_t *entry_ptr = (uint32_t*)((uint8_t*)rsdt + sizeof(acpi_sdt_header_t));

    for (int i = 0; i < entries; i++) {
        acpi_sdt_header_t *header = (acpi_sdt_header_t*)(uintptr_t)entry_ptr[i];
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            return header;
        }
    }

    return NULL;
}

// Parse MADT entries
static void acpi_parse_madt(acpi_sdt_header_t *madt_header) {
    uint8_t *ptr = (uint8_t*)madt_header + sizeof(acpi_sdt_header_t) + 8; // Skip Local APIC address
    uint8_t *end = (uint8_t*)madt_header + madt_header->length;

    cpu_count = 0;

    while (ptr < end && cpu_count < 16) {
        acpi_madt_entry_header_t *entry = (acpi_madt_entry_header_t*)ptr;

        if (entry->type == ACPI_MADT_TYPE_LAPIC) {
            acpi_madt_lapic_t *lapic = (acpi_madt_lapic_t*)entry;
            if (lapic->flags & 0x1) {  // Enabled flag
                cpu_apic_ids[cpu_count++] = lapic->apic_id;
                serial_printf("[ACPI] CPU %d: APIC ID = %d\n", cpu_count - 1, lapic->apic_id);
            }
        }

        ptr += entry->length;
    }
}

void acpi_init(void *rsdp_addr) {
    if (rsdp_addr != NULL) {
        rsdp = (acpi_rsdp_t*)rsdp_addr;
    } else {
        serial_puts("[ACPI] Searching for RSDP...\n");
        rsdp = acpi_find_rsdp();
    }

    if (!rsdp) {
        serial_puts("[ACPI] RSDP not found!\n");
        return;
    }

    serial_printf("[ACPI] RSDP found at 0x%lx\n", (uint64_t)rsdp);

    madt = acpi_find_madt(rsdp);
    if (!madt) {
        serial_puts("[ACPI] MADT not found!\n");
        return;
    }

    serial_printf("[ACPI] MADT found at 0x%lx\n", (uint64_t)madt);

    acpi_parse_madt(madt);
    serial_printf("[ACPI] Detected %d CPUs\n", cpu_count);
}

int acpi_get_cpu_count(void) {
    return cpu_count;
}

uint8_t acpi_get_apic_id(int index) {
    if (index >= 0 && index < cpu_count)
        return cpu_apic_ids[index];
    return 0xFF;
}
