// Virtual Memory Manager - Stub for now
const serial = @import("serial.zig");

pub fn init() void {
    serial.write_string("[VMM] Using bootloader's page tables\n");
    // In Zig version, we'll use the same page tables set up by the bootloader
    // Future: implement full VMM with recursive mapping
}
