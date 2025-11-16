// CPU Feature Detection via CPUID

const serial = @import("serial.zig");

// CPUID instruction wrapper
fn cpuid(leaf: u32, subleaf: u32) struct { eax: u32, ebx: u32, ecx: u32, edx: u32 } {
    var eax: u32 = undefined;
    var ebx: u32 = undefined;
    var ecx: u32 = undefined;
    var edx: u32 = undefined;

    asm volatile ("cpuid"
        : [eax] "={eax}" (eax),
          [ebx] "={ebx}" (ebx),
          [ecx] "={ecx}" (ecx),
          [edx] "={edx}" (edx),
        : [leaf] "{eax}" (leaf),
          [subleaf] "{ecx}" (subleaf),
    );

    return .{ .eax = eax, .ebx = ebx, .ecx = ecx, .edx = edx };
}

pub fn detect_features() void {
    serial.write_string("\n[CPU] Detecting CPU features...\n");

    // CPUID leaf 0: Get vendor string
    const vendor_info = cpuid(0, 0);
    const max_basic_leaf = vendor_info.eax;

    // Extract vendor string
    var vendor: [13]u8 = undefined;
    @memcpy(vendor[0..4], @as(*const [4]u8, @ptrCast(&vendor_info.ebx)));
    @memcpy(vendor[4..8], @as(*const [4]u8, @ptrCast(&vendor_info.edx)));
    @memcpy(vendor[8..12], @as(*const [4]u8, @ptrCast(&vendor_info.ecx)));
    vendor[12] = 0;

    serial.write_string("[CPU] Vendor: ");
    serial.write_string(&vendor);
    serial.write_string("\n");

    if (max_basic_leaf >= 1) {
        const features = cpuid(1, 0);
        serial.write_string("[CPU] Features detected:\n");

        // EDX features (leaf 1)
        if (features.edx & (1 << 0) != 0) serial.write_string("  [✓] FPU - x87 Floating Point Unit\n");
        if (features.edx & (1 << 4) != 0) serial.write_string("  [✓] TSC - Time Stamp Counter\n");
        if (features.edx & (1 << 5) != 0) serial.write_string("  [✓] MSR - Model Specific Registers\n");
        if (features.edx & (1 << 6) != 0) serial.write_string("  [✓] PAE - Physical Address Extension\n");
        if (features.edx & (1 << 8) != 0) serial.write_string("  [✓] CX8 - CMPXCHG8B\n");
        if (features.edx & (1 << 9) != 0) serial.write_string("  [✓] APIC - On-chip APIC\n");
        if (features.edx & (1 << 13) != 0) serial.write_string("  [✓] PGE - Page Global Enable\n");
        if (features.edx & (1 << 15) != 0) serial.write_string("  [✓] CMOV - Conditional Move\n");
        if (features.edx & (1 << 23) != 0) serial.write_string("  [✓] MMX - MMX instructions\n");
        if (features.edx & (1 << 24) != 0) serial.write_string("  [✓] FXSR - FXSAVE/FXRSTOR\n");
        if (features.edx & (1 << 25) != 0) serial.write_string("  [✓] SSE - Streaming SIMD Extensions\n");
        if (features.edx & (1 << 26) != 0) serial.write_string("  [✓] SSE2 - Streaming SIMD Extensions 2\n");

        // ECX features (leaf 1)
        if (features.ecx & (1 << 0) != 0) serial.write_string("  [✓] SSE3 - Streaming SIMD Extensions 3\n");
        if (features.ecx & (1 << 9) != 0) serial.write_string("  [✓] SSSE3 - Supplemental SSE3\n");
        if (features.ecx & (1 << 19) != 0) serial.write_string("  [✓] SSE4.1 - Streaming SIMD Extensions 4.1\n");
        if (features.ecx & (1 << 20) != 0) serial.write_string("  [✓] SSE4.2 - Streaming SIMD Extensions 4.2\n");
        if (features.ecx & (1 << 21) != 0) serial.write_string("  [✓] x2APIC - Extended xAPIC\n");
        if (features.ecx & (1 << 28) != 0) serial.write_string("  [✓] AVX - Advanced Vector Extensions\n");
    }

    // Extended CPUID features
    const ext_info = cpuid(0x80000000, 0);
    if (ext_info.eax >= 0x80000001) {
        const ext_features = cpuid(0x80000001, 0);

        if (ext_features.edx & (1 << 11) != 0) serial.write_string("  [✓] SYSCALL/SYSRET\n");
        if (ext_features.edx & (1 << 20) != 0) serial.write_string("  [✓] NX - No-Execute bit\n");
        if (ext_features.edx & (1 << 29) != 0) serial.write_string("  [✓] Long Mode (64-bit)\n");
    }

    serial.write_string("\n");
}
