// IDT (Interrupt Descriptor Table) - 64-bit mode
const serial = @import("serial.zig");
const apic = @import("apic.zig");

// IDT Gate Descriptor (64-bit mode)
const IDTEntry = packed struct {
    offset_low: u16,    // Offset bits 0-15
    selector: u16,      // Code segment selector (0x08 for kernel code)
    ist: u8,            // Interrupt Stack Table (0 = don't use)
    type_attr: u8,      // Type and attributes (0x8E for interrupt gate)
    offset_mid: u16,    // Offset bits 16-31
    offset_high: u32,   // Offset bits 32-63
    zero: u32,          // Reserved (must be 0)
};

// IDT Pointer (IDTR)
const IDTPtr = packed struct {
    limit: u16,         // Size of IDT - 1
    base: u64,          // Base address of IDT
};

// IDT with 256 entries
var idt: [256]IDTEntry align(16) = undefined;
var idtr: IDTPtr = undefined;

// Exception names (for debugging)
const exception_names = [_][]const u8{
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved",
};

// Exception counter
pub var exception_count: u32 = 0;

// Timer interrupt counters (per-CPU)
const MAX_CPUS = 16;
pub var timer_ticks: [MAX_CPUS]u64 = [_]u64{0} ** MAX_CPUS;
pub var global_timer_calls: u64 = 0;

// Set an IDT entry
fn idt_set_gate(num: u8, handler: u64, selector: u16, flags: u8) void {
    idt[num].offset_low = @truncate(handler & 0xFFFF);
    idt[num].offset_mid = @truncate((handler >> 16) & 0xFFFF);
    idt[num].offset_high = @truncate((handler >> 32) & 0xFFFFFFFF);
    idt[num].selector = selector;
    idt[num].ist = 0;  // No IST for now
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

// Load IDT
fn idt_load() void {
    asm volatile ("lidt %[idtr]"
        :
        : [idtr] "*p" (&idtr),
    );
}

// Load IDT on AP (public for SMP)
pub fn idt_load_ap() void {
    idt_load();
}

// Generic exception handler (called from assembly stubs in idt.S)
export fn exception_handler(vector: u64, error_code: u64, rip: u64) callconv(.C) void {
    _ = @atomicRmw(u32, &exception_count, .Add, 1, .monotonic);

    serial.write_string("\n[EXCEPTION] ");
    if (vector < 32) {
        serial.write_string(exception_names[vector]);
    } else if (vector == 255) {
        serial.write_string("Spurious/Unhandled Interrupt");
    } else {
        serial.write_string("Unknown Exception");
    }
    serial.write_string(" (Vector ");
    serial.write_dec_u32(@truncate(vector));
    serial.write_string(")\n");

    serial.write_string("  Error Code: ");
    serial.write_hex_u64(error_code);
    serial.write_string("\n");

    serial.write_string("  RIP: ");
    serial.write_hex_u64(rip);
    serial.write_string("\n");

    // Allow breakpoint (vector 3) and spurious interrupts (vector 255) to continue
    if (vector == 3 or vector == 255) {
        serial.write_string("[INFO] Continuing execution...\n");
        return;  // Return to exception_common, which will iretq
    }

    // For other exceptions, halt
    serial.write_string("[HALT] System halted due to exception\n");
    while (true) {
        asm volatile ("hlt");
    }
}

// Timer interrupt handler (called from assembly stub in idt.S)
export fn timer_interrupt_handler() callconv(.C) void {
    // Increment global counter (debug)
    _ = @atomicRmw(u64, &global_timer_calls, .Add, 1, .monotonic);

    // Get current CPU APIC ID
    const apic_id = apic.get_apic_id();

    // Use APIC ID directly as index (works for sequential APIC IDs like in QEMU)
    if (apic_id < MAX_CPUS) {
        _ = @atomicRmw(u64, &timer_ticks[apic_id], .Add, 1, .monotonic);
    }

    // Send EOI to acknowledge interrupt
    apic.send_eoi();
}

// Assembly stubs - declared in idt.S
extern fn exception_common() callconv(.Naked) void;
extern fn timer_irq_stub() callconv(.Naked) void;
extern fn pure_iretq_handler() callconv(.Naked) void;

// Exception stubs (0-31) - declared in idt.S
extern fn exception_stub_0() callconv(.Naked) void;
extern fn exception_stub_1() callconv(.Naked) void;
extern fn exception_stub_2() callconv(.Naked) void;
extern fn exception_stub_3() callconv(.Naked) void;
extern fn exception_stub_4() callconv(.Naked) void;
extern fn exception_stub_5() callconv(.Naked) void;
extern fn exception_stub_6() callconv(.Naked) void;
extern fn exception_stub_7() callconv(.Naked) void;
extern fn exception_stub_8() callconv(.Naked) void;
extern fn exception_stub_9() callconv(.Naked) void;
extern fn exception_stub_10() callconv(.Naked) void;
extern fn exception_stub_11() callconv(.Naked) void;
extern fn exception_stub_12() callconv(.Naked) void;
extern fn exception_stub_13() callconv(.Naked) void;
extern fn exception_stub_14() callconv(.Naked) void;
extern fn exception_stub_15() callconv(.Naked) void;
extern fn exception_stub_16() callconv(.Naked) void;
extern fn exception_stub_17() callconv(.Naked) void;
extern fn exception_stub_18() callconv(.Naked) void;
extern fn exception_stub_19() callconv(.Naked) void;
extern fn exception_stub_20() callconv(.Naked) void;
extern fn exception_stub_21() callconv(.Naked) void;
extern fn exception_stub_22() callconv(.Naked) void;
extern fn exception_stub_23() callconv(.Naked) void;
extern fn exception_stub_24() callconv(.Naked) void;
extern fn exception_stub_25() callconv(.Naked) void;
extern fn exception_stub_26() callconv(.Naked) void;
extern fn exception_stub_27() callconv(.Naked) void;
extern fn exception_stub_28() callconv(.Naked) void;
extern fn exception_stub_29() callconv(.Naked) void;
extern fn exception_stub_30() callconv(.Naked) void;
extern fn exception_stub_31() callconv(.Naked) void;

// Initialize IDT
pub fn init() void {
    serial.write_string("[IDT] Initializing Interrupt Descriptor Table...\n");

    // Set ALL 256 entries to pure assembly handler (just iretq) as default
    var i: usize = 0;
    while (i < 256) : (i += 1) {
        idt_set_gate(@truncate(i), @intFromPtr(&pure_iretq_handler), 0x08, 0x8E);
    }

    // Exception handlers (0-31) - using function pointer array
    const exception_stubs = [_]*const fn () callconv(.Naked) void{
        exception_stub_0,  exception_stub_1,  exception_stub_2,  exception_stub_3,
        exception_stub_4,  exception_stub_5,  exception_stub_6,  exception_stub_7,
        exception_stub_8,  exception_stub_9,  exception_stub_10, exception_stub_11,
        exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15,
        exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19,
        exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23,
        exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27,
        exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31,
    };

    i = 0;
    while (i < 32) : (i += 1) {
        idt_set_gate(@truncate(i), @intFromPtr(exception_stubs[i]), 0x08, 0x8E);
    }

    // Timer IRQ handler (vector 32)
    idt_set_gate(32, @intFromPtr(&timer_irq_stub), 0x08, 0x8E);

    // Set up IDTR
    idtr.limit = @sizeOf(@TypeOf(idt)) - 1;
    idtr.base = @intFromPtr(&idt);

    // Load IDT
    idt_load();

    serial.write_string("[IDT] IDT initialized with 32 exception handlers\n");
    serial.write_string("[IDT] IDT loaded successfully!\n");
}
