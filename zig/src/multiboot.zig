// Multiboot2 header and structures
const std = @import("std");

pub const MULTIBOOT2_BOOTLOADER_MAGIC: u32 = 0x36d76289;
const MULTIBOOT2_HEADER_MAGIC: u32 = 0xe85250d6;
const MULTIBOOT_ARCHITECTURE_I386: u32 = 0;

// Multiboot2 header tags
const MULTIBOOT_HEADER_TAG_END: u16 = 0;
const MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST: u16 = 1;

const MULTIBOOT_TAG_TYPE_END: u32 = 0;
const MULTIBOOT_TAG_TYPE_MMAP: u32 = 6;
const MULTIBOOT_TAG_TYPE_ACPI_OLD: u32 = 14;
const MULTIBOOT_TAG_TYPE_ACPI_NEW: u32 = 15;

// Multiboot2 header structure
const MultibootHeader = extern struct {
    magic: u32,
    architecture: u32,
    header_length: u32,
    checksum: u32,
    // End tag
    end_tag_type: u16,
    end_tag_flags: u16,
    end_tag_size: u32,
};

// Export multiboot header
pub const multiboot_header align(8) linksection(".multiboot") = MultibootHeader{
    .magic = MULTIBOOT2_HEADER_MAGIC,
    .architecture = MULTIBOOT_ARCHITECTURE_I386,
    .header_length = @sizeOf(MultibootHeader),
    .checksum = @as(u32, @truncate(0 -% (MULTIBOOT2_HEADER_MAGIC +% MULTIBOOT_ARCHITECTURE_I386 +% @sizeOf(MultibootHeader)))),
    .end_tag_type = MULTIBOOT_HEADER_TAG_END,
    .end_tag_flags = 0,
    .end_tag_size = 8,
};

// Multiboot2 tag header
pub const TagHeader = extern struct {
    type: u32,
    size: u32,
};

// Memory map entry
pub const MMapEntry = extern struct {
    base_addr: u64,
    length: u64,
    type: u32,
    reserved: u32,
};

// Memory map tag
pub const MMapTag = extern struct {
    header: TagHeader,
    entry_size: u32,
    entry_version: u32,
    // Entries follow
};

// ACPI RSDP tag
pub const AcpiTag = extern struct {
    header: TagHeader,
    // RSDP data follows
};

// Tag iterator
pub const TagIterator = struct {
    current: [*]const u8,
    end: [*]const u8,

    pub fn init(multiboot_addr: u32) TagIterator {
        const base = @as([*]const u8, @ptrFromInt(@as(usize, multiboot_addr)));
        const total_size = @as(*const u32, @ptrFromInt(@as(usize, multiboot_addr))).*;
        return .{
            .current = base + 8, // Skip total_size and reserved
            .end = base + total_size,
        };
    }

    pub fn next(self: *TagIterator) ?*const TagHeader {
        if (@intFromPtr(self.current) >= @intFromPtr(self.end)) {
            return null;
        }

        const tag = @as(*const TagHeader, @ptrCast(@alignCast(self.current)));
        if (tag.type == MULTIBOOT_TAG_TYPE_END) {
            return null;
        }

        // Align to 8 bytes manually
        const size = tag.size;
        const aligned_size = (size + 7) & ~@as(usize, 7);
        self.current += aligned_size;

        return tag;
    }
};

// Find memory map tag
pub fn find_mmap_tag(multiboot_addr: u32) ?*const MMapTag {
    const serial = @import("serial.zig");
    serial.write_string("[MBOOT] Searching for MMAP tag...\n");

    var iter = TagIterator.init(multiboot_addr);
    var count: u32 = 0;
    while (iter.next()) |tag| {
        count += 1;
        serial.write_string("[MBOOT] Tag #");
        serial.write_dec_u32(count);
        serial.write_string(" type=");
        serial.write_dec_u32(tag.type);
        serial.write_string("\n");

        if (tag.type == MULTIBOOT_TAG_TYPE_MMAP) {
            serial.write_string("[MBOOT] Found MMAP tag!\n");
            return @ptrCast(@alignCast(tag));
        }
    }
    serial.write_string("[MBOOT] MMAP tag not found!\n");
    return null;
}

// Find ACPI RSDP tag
pub fn find_acpi_tag(multiboot_addr: u32) ?*const AcpiTag {
    var iter = TagIterator.init(multiboot_addr);
    while (iter.next()) |tag| {
        if (tag.type == MULTIBOOT_TAG_TYPE_ACPI_NEW or tag.type == MULTIBOOT_TAG_TYPE_ACPI_OLD) {
            return @ptrCast(@alignCast(tag));
        }
    }
    return null;
}
