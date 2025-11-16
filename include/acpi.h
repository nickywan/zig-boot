#ifndef ACPI_H
#define ACPI_H

#include "types.h"

// ACPI RSDP (Root System Description Pointer)
typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

// ACPI SDT Header
typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

// ACPI MADT (Multiple APIC Description Table)
#define ACPI_MADT_TYPE_LAPIC 0
#define ACPI_MADT_TYPE_IOAPIC 1

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_lapic_t;

// Functions
void acpi_init(void *rsdp_addr);
int acpi_get_cpu_count(void);
uint8_t acpi_get_apic_id(int index);

#endif
