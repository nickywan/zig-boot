// ACPI parser for CPU detection
const std = @import("std");
const serial = @import("serial.zig");
const multiboot = @import("multiboot.zig");

const RSDP_SIGNATURE = "RSD PTR ";

const RSDP = extern struct {
    signature: [8]u8,
    checksum: u8,
    oem_id: [6]u8,
    revision: u8,
    rsdt_address: u32,
    // ACPI 2.0+
    length: u32,
    xsdt_address: u64,
    extended_checksum: u8,
    reserved: [3]u8,
};

const SDTHeader = extern struct {
    signature: [4]u8,
    length: u32,
    revision: u8,
    checksum: u8,
    oem_id: [6]u8,
    oem_table_id: [8]u8,
    oem_revision: u32,
    creator_id: u32,
    creator_revision: u32,
};

const MADT = extern struct {
    header: SDTHeader,
    local_apic_address: u32,
    flags: u32,
    // Entries follow
};

const MADTEntryHeader = extern struct {
    type: u8,
    length: u8,
};

const MADTLocalAPIC = extern struct {
    header: MADTEntryHeader,
    acpi_processor_id: u8,
    apic_id: u8,
    flags: u32,
};

var cpu_count: u32 = 0;
pub var cpu_apic_ids: [16]u8 = undefined;

pub fn detect_cpus() !u32 {
    // Search for RSDP in BIOS area
    const rsdp = find_rsdp() orelse return error.NoRSDP;

    serial.write_string("[ACPI] RSDP found!\n");

    // Get RSDT
    const rsdt = @as(*const SDTHeader, @ptrFromInt(@as(usize, rsdp.rsdt_address)));

    // Find MADT
    const madt = find_madt(rsdt) orelse return error.NoMADT;

    serial.write_string("[ACPI] MADT found!\n");
    serial.write_string("[ACPI] Parsing MADT entries...\n");

    // Parse MADT entries to find CPUs
    cpu_count = 0;
    const madt_data = @as([*]const u8, @ptrCast(madt));
    var offset: usize = @sizeOf(MADT);
    const end = madt.header.length;

    while (offset < end) {
        const entry_header = @as(*const MADTEntryHeader, @ptrCast(@alignCast(madt_data + offset)));

        if (entry_header.type == 0) { // Local APIC
            const local_apic = @as(*const MADTLocalAPIC, @ptrCast(@alignCast(entry_header)));
            if ((local_apic.flags & 1) != 0) { // CPU enabled
                if (cpu_count < 16) {
                    cpu_apic_ids[cpu_count] = local_apic.apic_id;
                    serial.write_string("[ACPI] CPU ");
                    serial.write_dec_u32(cpu_count);
                    serial.write_string(" detected (APIC ID ");
                    serial.write_dec_u32(local_apic.apic_id);
                    serial.write_string(")\n");
                    cpu_count += 1;
                }
            }
        }

        offset += entry_header.length;
    }

    return cpu_count;
}

fn find_rsdp() ?*const RSDP {
    // Search in EBDA (Extended BIOS Data Area)
    const ebda_base = @as(usize, @as(*const u16, @ptrFromInt(0x40E)).*) << 4;
    if (ebda_base != 0) {
        if (search_rsdp(ebda_base, ebda_base + 1024)) |rsdp| {
            return rsdp;
        }
    }

    // Search in main BIOS area (0xE0000 - 0xFFFFF)
    return search_rsdp(0xE0000, 0x100000);
}

fn search_rsdp(start: usize, end: usize) ?*const RSDP {
    var addr = start;
    while (addr < end) : (addr += 16) {
        const rsdp = @as(*const RSDP, @ptrFromInt(addr));
        if (std.mem.eql(u8, &rsdp.signature, RSDP_SIGNATURE)) {
            return rsdp;
        }
    }
    return null;
}

fn find_madt(rsdt: *const SDTHeader) ?*const MADT {
    const entry_count = (rsdt.length - @sizeOf(SDTHeader)) / 4;
    const entries = @as([*]const u32, @ptrCast(@alignCast(@as([*]const u8, @ptrCast(rsdt)) + @sizeOf(SDTHeader))));

    var i: usize = 0;
    while (i < entry_count) : (i += 1) {
        const table = @as(*const SDTHeader, @ptrFromInt(@as(usize, entries[i])));
        if (std.mem.eql(u8, &table.signature, "APIC")) {
            return @ptrCast(table);
        }
    }
    return null;
}

pub fn get_cpu_count() u32 {
    return cpu_count;
}

pub fn get_apic_id(cpu_index: u32) u8 {
    if (cpu_index < cpu_count) {
        return cpu_apic_ids[cpu_index];
    }
    return 0;
}
