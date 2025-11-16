# Hybrid C + Zig Kernel

Architecture hybride où **C fait le bootstrap complet** et **Zig fait toute la computation**.

## 🎯 Philosophie

### Phase 1: C Bootstrap (Critique, Bas Niveau)
```
boot.S → init.c → handoff to Zig
```

**Responsabilités de C :**
- ✅ Multiboot2 entry point (boot.S)
- ✅ Memory detection (PMM, VMM)
- ✅ ACPI parsing & CPU detection
- ✅ SMP initialization (boot all APs)
- ✅ APIC configuration (x2APIC/xAPIC)
- ✅ **IDT setup (32 exception handlers + timer IRQ)**
- ✅ Enable interrupts
- ✅ Prepare BootInfo structure
- ✅ Call `zig_kernel_main()`

### Phase 2: Zig Kernel (Logique, Computation)
```
zig_kernel_main(boot_info) → run tests/tasks
```

**Responsabilités de Zig :**
- ✅ Receive ready-to-use environment from C
- ✅ Parallel computation tests
- ✅ Task scheduling
- ✅ Process management
- ✅ System calls
- ✅ User space
- ✅ All high-level logic

## 📁 Structure

```
hybrid/
├── boot/               # C Bootstrap (Phase 1)
│   ├── boot.S         # Multiboot2 entry (ASM)
│   ├── trampoline.S   # SMP trampoline (ASM)
│   ├── init.c         # Main C bootstrap
│   ├── acpi.c         # ACPI parsing
│   ├── apic.c         # APIC setup
│   ├── idt.c          # IDT initialization
│   ├── smp.c          # SMP boot
│   ├── pmm.c          # Physical memory
│   ├── vmm.c          # Virtual memory
│   └── services.c     # C services for Zig callbacks
│
├── kernel/            # Zig Kernel (Phase 2)
│   ├── main.zig       # Zig entry (zig_kernel_main)
│   ├── boot_info.zig  # BootInfo definition
│   ├── tests.zig      # Parallel tests
│   └── ...
│
├── shared/            # C ↔ Zig Interface
│   └── boot_info.h    # Shared BootInfo structure
│
├── Makefile           # Build C bootstrap
├── build.zig          # Build Zig kernel + link everything
└── linker.ld          # Linker script
```

## 🔄 Handoff Protocol

### 1. C prepares environment:
```c
BootInfo boot_info = {
    .cpu_count = 4,
    .use_x2apic = true,
    .idt_loaded = true,
    .serial_initialized = true,
    // ... fill all fields
};
```

### 2. C calls Zig:
```c
puts("[C] Bootstrap complete. Calling Zig...\n");
zig_kernel_main(&boot_info);  // Never returns
```

### 3. Zig receives control:
```zig
export fn zig_kernel_main(boot_info: *const BootInfo) callconv(.C) noreturn {
    c_write_serial("Hello from Zig!\n");

    // All hardware is ready, interrupts enabled, SMP running
    run_parallel_tests(boot_info);

    while (true) { asm volatile ("hlt"); }
}
```

## 🛠️ Building

```bash
# Build C bootstrap + Zig kernel + ISO
zig build iso

# Run in QEMU (KVM)
zig build run

# Debug mode
zig build debug
```

## ✅ What C Provides to Zig

| Feature | Status | Description |
|---------|--------|-------------|
| Physical Memory | ✅ Ready | PMM bitmap initialized |
| Virtual Memory | ✅ Ready | Page tables loaded |
| All CPUs | ✅ Running | 4 CPUs booted, stacks ready |
| APIC | ✅ Configured | x2APIC or xAPIC mode |
| IDT | ✅ Loaded | 32 exceptions + timer IRQ |
| Interrupts | ✅ Enabled | Timer ticking on all CPUs |
| Serial | ✅ Working | COM1 ready for debug output |

## 🎯 Benefits

✅ **C handles the hard stuff** - ACPI, SMP, IDT (battle-tested)
✅ **Zig gets clean environment** - No need to reimplement bootstrap
✅ **Type safety where it matters** - Zig for complex logic
✅ **Best of both worlds** - C's low-level power + Zig's safety
✅ **Easy debugging** - C bootstrap can be tested independently
✅ **Gradual migration** - Can move features from C to Zig over time
