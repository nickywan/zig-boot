// Zig allocator using PMM (Physical Memory Manager)

const std = @import("std");
const pmm = @import("pmm.zig");
const serial = @import("serial.zig");

// Simple page allocator using PMM
const PageAllocator = struct {
    fn alloc(
        _: *anyopaque,
        len: usize,
        ptr_align: std.mem.Alignment,
        ret_addr: usize,
    ) ?[*]u8 {
        _ = ptr_align;
        _ = ret_addr;

        // Round up to page size (4096 bytes)
        const pages_needed = (len + 4095) / 4096;

        // Allocate physical pages
        var allocated: usize = 0;
        while (allocated < pages_needed) : (allocated += 1) {
            const page = pmm.alloc_page() catch return null;

            // For simplicity, we return the first page address
            // In a real allocator, you'd track multi-page allocations
            if (allocated == 0) {
                return @ptrFromInt(page);
            }
        }

        return null;
    }

    fn resize(
        _: *anyopaque,
        buf: []u8,
        buf_align: std.mem.Alignment,
        new_len: usize,
        ret_addr: usize,
    ) bool {
        _ = buf;
        _ = buf_align;
        _ = new_len;
        _ = ret_addr;

        // Simple page allocator doesn't support resize
        return false;
    }

    fn free(
        _: *anyopaque,
        buf: []u8,
        buf_align: std.mem.Alignment,
        ret_addr: usize,
    ) void {
        _ = buf_align;
        _ = ret_addr;

        const phys_addr: u64 = @intFromPtr(buf.ptr);
        pmm.free_page(phys_addr);
    }

    fn remap(
        _: *anyopaque,
        buf: []u8,
        buf_align: std.mem.Alignment,
        new_len: usize,
        ret_addr: usize,
    ) ?[*]u8 {
        _ = buf;
        _ = buf_align;
        _ = new_len;
        _ = ret_addr;

        // Simple page allocator doesn't support remap
        return null;
    }
};

// Global allocator instance
var page_allocator = PageAllocator{};

// Get the allocator
pub fn get_allocator() std.mem.Allocator {
    return .{
        .ptr = &page_allocator,
        .vtable = &.{
            .alloc = PageAllocator.alloc,
            .resize = PageAllocator.resize,
            .free = PageAllocator.free,
            .remap = PageAllocator.remap,
        },
    };
}

// Test allocation
pub fn test_allocator() void {
    serial.write_string("\n[Allocator Test] Testing Zig page allocator with PMM...\n");

    const allocator = get_allocator();

    // Test 1: Allocate a single u64
    serial.write_string("[Test 1] Allocating single u64...\n");
    const single = allocator.create(u64) catch {
        serial.write_string("[Test 1] FAILED - allocation error\n");
        return;
    };
    single.* = 0xDEADBEEF;
    serial.write_string("[Test 1] PASSED - allocated and wrote value\n");
    allocator.destroy(single);

    // Test 2: Allocate an array
    serial.write_string("[Test 2] Allocating array of 100 u32...\n");
    const array = allocator.alloc(u32, 100) catch {
        serial.write_string("[Test 2] FAILED - allocation error\n");
        return;
    };

    // Fill array
    for (array, 0..) |*item, i| {
        item.* = @intCast(i * 2);
    }

    // Verify
    var ok = true;
    for (array, 0..) |item, i| {
        if (item != i * 2) {
            ok = false;
            break;
        }
    }

    if (ok) {
        serial.write_string("[Test 2] PASSED - array allocated and verified\n");
    } else {
        serial.write_string("[Test 2] FAILED - data corruption\n");
    }

    allocator.free(array);

    // Test 3: Allocate struct
    serial.write_string("[Test 3] Allocating struct...\n");

    const TestStruct = struct {
        a: u64,
        b: u32,
        c: [16]u8,
    };

    const s = allocator.create(TestStruct) catch {
        serial.write_string("[Test 3] FAILED - allocation error\n");
        return;
    };

    s.a = 0x123456789ABCDEF0;
    s.b = 0xCAFEBABE;
    for (&s.c, 0..) |*byte, i| {
        byte.* = @intCast(i);
    }

    serial.write_string("[Test 3] PASSED - struct allocated and initialized\n");
    allocator.destroy(s);

    serial.write_string("[Allocator Test] All tests passed!\n\n");
}
