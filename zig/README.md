# Zig Kernel - x86-64 Bare-Metal

This is a Zig 0.14 implementation of the C kernel, demonstrating the same OS concepts in Zig.

## Prerequisites

### Install Zig 0.14

```bash
# Download Zig 0.14.0
wget https://ziglang.org/download/0.14.0/zig-linux-x86_64-0.14.0.tar.xz
tar xf zig-linux-x86_64-0.14.0.tar.xz
sudo mv zig-linux-x86_64-0.14.0 /opt/zig
sudo ln -sf /opt/zig/zig /usr/local/bin/zig

# Verify installation
zig version
```

### QEMU

```bash
sudo apt install qemu-system-x86
```

## Project Structure

```
zig/
├── src/
│   ├── main.zig        # Kernel entry point
│   ├── boot.S          # x86-64 bootloader (assembly)
│   ├── serial.zig      # COM1 serial driver
│   ├── multiboot.zig   # Multiboot2 header/parsing
│   ├── pmm.zig         # Physical Memory Manager
│   ├── vmm.zig         # Virtual Memory Manager
│   ├── acpi.zig        # ACPI parsing (CPU detection)
│   ├── apic.zig        # APIC/timer management
│   ├── smp.zig         # SMP boot
│   ├── tests.zig       # Parallel computation tests
│   └── panic.zig       # Panic handler
├── build.zig           # Build configuration
├── linker.ld           # Linker script
└── README.md           # This file
```

## Building

### Compile the Kernel

```bash
zig build
```

This produces `zig-out/bin/kernel`

### Run in QEMU

```bash
zig build run
```

### Run in TCG Mode (software emulation)

```bash
zig build run-tcg
```

### Debug Mode

```bash
zig build debug
```

## Features

**Implemented:**
- ✅ Multiboot2 boot protocol
- ✅ Serial output (COM1)
- ✅ Physical Memory Manager (bitmap)
- ✅ ACPI CPU detection
- ✅ APIC initialization

**To be completed:**
- 🔄 Virtual Memory Manager (full implementation)
- 🔄 SMP boot (INIT-SIPI-SIPI)
- 🔄 APIC timer
- 🔄 Parallel tests
- 🔄 Interrupt handling (IDT)

## Development Notes

This Zig implementation follows the same architecture as the C version but takes advantage of Zig's features:

- **Comptime**: Multiboot header generated at compile time
- **Error handling**: Proper error propagation with `!` and `catch`
- **Type safety**: No implicit casts, explicit integer widths
- **Inline assembly**: x86 I/O operations (inb/outb, rdmsr/wrmsr)
- **No undefined behavior**: All UB is caught at compile time

## Comparison with C Version

| Feature | C Version | Zig Version |
|---------|-----------|-------------|
| Multiboot2 | ✅ | ✅ |
| Serial I/O | ✅ | ✅ |
| PMM | ✅ | ✅ |
| VMM | ✅ | 🔄 Stub |
| ACPI | ✅ | ✅ |
| APIC | ✅ | ✅ |
| SMP | ✅ 4 CPUs | 🔄 Stub |
| Timer | ✅ BSP only | 🔄 Stub |
| Tests | ✅ All pass | 🔄 Stub |

## Current Status

The Zig kernel successfully:
- Boots in QEMU
- Initializes serial output
- Parses Multiboot2 info
- Detects CPUs via ACPI
- Initializes Local APIC

**Next steps:**
1. Complete SMP implementation (trampoline + AP boot)
2. Implement APIC timer
3. Add interrupt handling (IDT)
4. Port parallel computation tests
5. Add VMM with recursive paging

## Building Without Zig (using C bootloader)

Since the Zig kernel uses the same assembly bootloader as the C version, you can also compile it by:

1. Building Zig code: `zig build-obj src/main.zig -target x86_64-freestanding`
2. Linking with C bootloader: `ld -T linker.ld ...`

This is already handled by `build.zig`.

## License

Educational project - same as C version.

---

**Status**: Basic kernel boots, modules implemented, SMP/tests pending
