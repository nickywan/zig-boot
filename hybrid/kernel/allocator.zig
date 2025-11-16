// Zig allocator that uses C kernel heap (kmalloc/kfree)

const std = @import("std");
const c_write_serial = @import("boot_info.zig").c_write_serial;

// C memory allocation functions
extern fn c_kmalloc(size: u64) ?*anyopaque;
extern fn c_kfree(ptr: *anyopaque) void;

// Custom allocator using C heap
const CHeapAllocator = struct {
    fn alloc(
        _: *anyopaque,
        len: usize,
        ptr_align: std.mem.Alignment,
        ret_addr: usize,
    ) ?[*]u8 {
        _ = ptr_align;
        _ = ret_addr;

        const ptr = c_kmalloc(len) orelse return null;
        return @ptrCast(ptr);
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

        // Simple bump allocator doesn't support resize
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

        c_kfree(@ptrCast(buf.ptr));
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

        // Simple bump allocator doesn't support remap
        return null;
    }
};

// Global allocator instance
var c_heap_allocator = CHeapAllocator{};

// Get the allocator
pub fn get_allocator() std.mem.Allocator {
    return .{
        .ptr = &c_heap_allocator,
        .vtable = &.{
            .alloc = CHeapAllocator.alloc,
            .resize = CHeapAllocator.resize,
            .free = CHeapAllocator.free,
            .remap = CHeapAllocator.remap,
        },
    };
}

// Test allocation
pub fn test_allocator() void {
    c_write_serial("\n[Allocator Test] Testing Zig allocator with C heap...\n");

    const allocator = get_allocator();

    // Test 1: Allocate a single u64
    c_write_serial("[Test 1] Allocating single u64...\n");
    const single = allocator.create(u64) catch {
        c_write_serial("[Test 1] FAILED - allocation error\n");
        return;
    };
    single.* = 0xDEADBEEF;
    c_write_serial("[Test 1] PASSED - allocated and wrote value\n");
    allocator.destroy(single);

    // Test 2: Allocate an array
    c_write_serial("[Test 2] Allocating array of 100 u32...\n");
    const array = allocator.alloc(u32, 100) catch {
        c_write_serial("[Test 2] FAILED - allocation error\n");
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
        c_write_serial("[Test 2] PASSED - array allocated and verified\n");
    } else {
        c_write_serial("[Test 2] FAILED - data corruption\n");
    }

    allocator.free(array);

    // Test 3: Allocate struct
    c_write_serial("[Test 3] Allocating struct...\n");

    const TestStruct = struct {
        a: u64,
        b: u32,
        c: [16]u8,
    };

    const s = allocator.create(TestStruct) catch {
        c_write_serial("[Test 3] FAILED - allocation error\n");
        return;
    };

    s.a = 0x123456789ABCDEF0;
    s.b = 0xCAFEBABE;
    for (&s.c, 0..) |*byte, i| {
        byte.* = @intCast(i);
    }

    c_write_serial("[Test 3] PASSED - struct allocated and initialized\n");
    allocator.destroy(s);

    c_write_serial("[Allocator Test] All tests passed!\n\n");
}
