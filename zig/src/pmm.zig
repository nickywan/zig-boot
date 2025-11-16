// Physical Memory Manager - Bitmap allocator
const std = @import("std");
const serial = @import("serial.zig");
const multiboot = @import("multiboot.zig");

const PAGE_SIZE: usize = 4096;
const MAX_PAGES: usize = 16384; // 64 MB

var bitmap: [MAX_PAGES / 8]u8 = undefined;
var total_pages: usize = 0;
var free_pages: usize = 0;

pub fn init(multiboot_addr: u32) !void {
    // Clear bitmap manually (avoid @memset issues)
    {
        var idx: usize = 0;
        while (idx < bitmap.len) : (idx += 1) {
            bitmap[idx] = 0xFF; // All pages marked as used initially
        }
    }

    // Parse memory map
    const mmap_tag = multiboot.find_mmap_tag(multiboot_addr) orelse {
        serial.write_string("[PMM] ERROR: No memory map found!\n");
        return error.NoMemoryMap;
    };

    const entry_count = (mmap_tag.header.size - @sizeOf(multiboot.MMapTag)) / mmap_tag.entry_size;
    const entries_ptr = @as([*]const multiboot.MMapEntry, @ptrCast(@alignCast(@as([*]const u8, @ptrCast(mmap_tag)) + @sizeOf(multiboot.MMapTag))));

    var i: usize = 0;
    while (i < entry_count) : (i += 1) {
        const entry = entries_ptr[i];

        // Only process available memory
        if (entry.type == 1) { // Available
            const start_page = entry.base_addr / PAGE_SIZE;
            const end_page = (entry.base_addr + entry.length) / PAGE_SIZE;

            var page = start_page;
            while (page < end_page and page < MAX_PAGES) : (page += 1) {
                mark_free(page);
            }
        }
    }

    // Mark kernel and low memory as used (from 0 to ~1.3 MB)
    const kernel_end: usize = 0x150000; // ~1.3 MB (conservative)

    var page: usize = 0;
    while (page < kernel_end / PAGE_SIZE) : (page += 1) {
        mark_used(page);
    }

    serial.write_string("[PMM] Free pages: ");
    serial.write_dec_u32(@truncate(free_pages));
    serial.write_string(" / ");
    serial.write_dec_u32(MAX_PAGES);
    serial.write_string(" (");
    serial.write_dec_u32(@truncate((free_pages * PAGE_SIZE) / 1024));
    serial.write_string(" KB free)\n");
}

fn mark_free(page: usize) void {
    if (page >= MAX_PAGES) return;
    const byte_idx = page / 8;
    const bit_idx = @as(u3, @truncate(page % 8));
    if ((bitmap[byte_idx] & (@as(u8, 1) << bit_idx)) != 0) {
        bitmap[byte_idx] &= ~(@as(u8, 1) << bit_idx);
        free_pages += 1;
    }
}

fn mark_used(page: usize) void {
    if (page >= MAX_PAGES) return;
    const byte_idx = page / 8;
    const bit_idx = @as(u3, @truncate(page % 8));
    if ((bitmap[byte_idx] & (@as(u8, 1) << bit_idx)) == 0) {
        bitmap[byte_idx] |= (@as(u8, 1) << bit_idx);
        free_pages -= 1;
    }
}

fn is_free(page: usize) bool {
    if (page >= MAX_PAGES) return false;
    const byte_idx = page / 8;
    const bit_idx = @as(u3, @truncate(page % 8));
    return (bitmap[byte_idx] & (@as(u8, 1) << bit_idx)) == 0;
}

pub fn alloc_page() !usize {
    var page: usize = 0;
    while (page < MAX_PAGES) : (page += 1) {
        if (is_free(page)) {
            mark_used(page);
            return page * PAGE_SIZE;
        }
    }
    return error.OutOfMemory;
}

pub fn free_page(phys_addr: usize) void {
    const page = phys_addr / PAGE_SIZE;
    mark_free(page);
}

pub fn get_free_pages() usize {
    return free_pages;
}
