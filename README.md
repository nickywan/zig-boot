# Bare-Metal x86-64 Kernel with SMP Support

A minimal bare-metal x86-64 kernel demonstrating fundamental OS concepts: multiprocessor boot, memory management, hardware interrupts, and parallel computation.

## Features

- ✅ **Multiboot2 Boot**: GRUB-compatible bootloader protocol
- ✅ **64-bit Long Mode**: Full x86-64 support with 4-level paging
- ✅ **SMP (Symmetric Multiprocessing)**: Boots all CPUs via INIT-SIPI-SIPI
- ✅ **Memory Management**: Physical (PMM), Virtual (VMM), and Heap allocators
- ✅ **APIC Support**: xAPIC mode with timer interrupts on BSP
- ✅ **ACPI Parsing**: RSDP/MADT for hardware discovery
- ✅ **Parallel Computation**: Synchronization primitives and multi-CPU tests
- ✅ **TCG Compatible**: Works in QEMU with and without KVM acceleration

## Project Structure

```
.
├── boot/
│   ├── boot_minimal.S      # Bootloader: real → protected → long mode
│   └── trampoline.S        # AP (Application Processor) startup code
├── kernel/
│   ├── minimal_step9.c     # Main kernel (all-in-one)
│   └── interrupt_stub.S    # Pre-compiled interrupt handlers
├── archive/                # Old development iterations
├── linker_minimal.ld       # Linker script
├── Makefile.step9          # Build configuration
├── claude.md               # Development documentation
└── README.md               # This file
```

## Prerequisites

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential grub-pc-bin xorriso qemu-system-x86
```

### Fedora/RHEL

```bash
sudo dnf install gcc make grub2-tools xorriso qemu-system-x86
```

### Arch Linux

```bash
sudo pacman -S base-devel grub xorriso qemu
```

### macOS (Homebrew)

```bash
brew install x86_64-elf-gcc grub xorriso qemu
# Note: May require additional configuration for cross-compilation
```

## Building

### Compile the Kernel

```bash
make -f Makefile.step9
```

This produces:
- `boot/boot_minimal.o` - Bootloader object
- `boot/trampoline.o` - AP startup object
- `kernel/minimal_step9.o` - Kernel object
- `kernel_step9.elf` - Final kernel binary

### Create Bootable ISO

```bash
make -f Makefile.step9 iso
```

This creates `boot_step9.iso` ready for QEMU or real hardware.

### Clean Build Artifacts

```bash
make -f Makefile.step9 clean
```

## Running

### Standard Run (with KVM if available)

```bash
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none -m 256M -smp 4
```

### Force TCG Mode (software emulation)

```bash
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none -m 256M -smp 4 -accel tcg
```

### With Graphics (VGA output)

```bash
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -m 256M -smp 4
```

### Debug Mode

```bash
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none \
  -m 256M -smp 4 -d cpu_reset,guest_errors -no-reboot
```

### GDB Debugging

```bash
# Terminal 1: Start QEMU with GDB server
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none \
  -m 256M -smp 4 -s -S

# Terminal 2: Connect with GDB
gdb kernel_step9.elf -ex "target remote :1234"
```

## Expected Output

```
===========================================
  Step 9: Memory Management
===========================================

[INFO] Multiboot2 info at: 0x000000000012F778

[MMAP] Parsing Multiboot2 memory map...
[PMM] Free pages: 16238 / 16384 (64952 KB free)
[VMM] Virtual Memory Manager initialized!
[HEAP] Kernel heap initialized!

[ACPI] Detected 4 CPU(s)
[APIC] Local APIC initialized successfully!

[SMP] Application Processors booted
[SMP] CPUs online: 4 / 4

[SUCCESS] All CPUs booted successfully!

[TIMER] BSP timer started successfully!

===========================================
  Running Parallel Computation Tests
===========================================

TEST 1: Parallel Counters
  CPU 0: 1000000 [OK]
  CPU 1: 1000000 [OK]
  CPU 2: 1000000 [OK]
  CPU 3: 1000000 [OK]

TEST 2: Distributed Sum (1 to 10,000,000)
  Total sum: 50000005000000
  Expected:  50000005000000
  [OK] Sum is correct!

TEST 3: Barrier Synchronization
  [OK] Barrier synchronization worked!

[SUCCESS] All parallel tests passed!

[TIMER] BSP collected 26 timer interrupts in 2 seconds
```

## Memory Layout

```
0x0000000000000000 - Low memory (reserved)
0x0000000000008000 - AP trampoline code
0x0000000000100000 - Kernel code/data
0x0000000000105000 - PML4 (Page Map Level 4)
0x0000000000130000 - PMM bitmap
0x0000000000131000 - Kernel heap (16 MB)
0xFEE00000         - Local APIC MMIO
0xFFFFFFFF80000000 - Recursive mapping base
```

## Architecture Overview

### Boot Sequence

1. **BIOS/GRUB** loads Multiboot2 kernel at 1MB
2. **Bootloader** (`boot_minimal.S`):
   - Sets up GDT, enables PAE
   - Configures 4-level paging with huge pages
   - Maps APIC MMIO with PCD flag
   - Transitions to 64-bit long mode
3. **Kernel** (`minimal_step9.c`):
   - Initializes PMM, VMM, Heap
   - Parses ACPI tables
   - Sets up IDT and APIC
   - Boots Application Processors
   - Runs parallel tests

### SMP Boot (INIT-SIPI-SIPI)

1. BSP sends INIT IPI to AP → AP enters wait-for-SIPI state
2. BSP sends SIPI with trampoline address (0x8000)
3. AP starts in real mode at trampoline
4. Trampoline transitions AP to long mode
5. AP loads IDT, enables APIC, runs tests
6. All CPUs synchronize via barriers

### Memory Management

**PMM (Physical Memory Manager)**:
- Bitmap-based allocator
- 4 KB page granularity
- Tracks 64 MB (16384 pages)

**VMM (Virtual Memory Manager)**:
- 4-level paging (PML4 → PDPT → PD → PT)
- Recursive mapping at PML4[511]
- Identity map for first 2 MB
- APIC MMIO at 0xFEE00000 with PCD

**Heap**:
- Simple bump allocator
- 16 MB region
- `kmalloc()` / `kfree()` interface

## Known Limitations

- ⚠️ **AP Timers Disabled**: APIC timer init causes system hang on APs (BSP timer works perfectly)
- ⚠️ **No TSS**: Not configured (not needed for ring 0 interrupts)
- ⚠️ **No User Mode**: Kernel runs entirely in ring 0
- ⚠️ **Serial Only**: No VGA text mode output

## Key Learnings

1. **TSS Not Required for Ring 0 Timers**: Contrary to some sources, TSS is not mandatory for APIC timer interrupts when running in ring 0.

2. **APIC MMIO Requires PCD**: The Page Cache Disable flag must be set when mapping APIC MMIO, otherwise register access may be cached.

3. **CS Reload on GDT Switch**: When APs switch from trampoline GDT to kernel GDT, CS must be reloaded via far return (`lretq`).

4. **Cache Coherency**: The `wbinvd` instruction in the AP trampoline ensures APs see BSP's memory writes.

## Troubleshooting

### Build Errors

**Problem**: `grub-mkrescue: command not found`
```bash
# Ubuntu/Debian
sudo apt install grub-pc-bin xorriso

# Arch
sudo pacman -S grub xorriso
```

**Problem**: Linker warnings about executable stack
```
# These are harmless for bare-metal kernels
# Can be ignored or suppressed
```

### Runtime Issues

**Problem**: System reboots immediately
- Check QEMU version (≥ 4.0 recommended)
- Try with `-accel tcg` to force software emulation
- Enable debug: `-d cpu_reset,guest_errors -no-reboot`

**Problem**: Only 1 CPU boots
- Check `-smp` parameter: `-smp 4`
- Some hypervisors may not support SMP
- Try different QEMU versions

**Problem**: No serial output
- Ensure `-serial stdio` is present
- Check COM1 initialization in kernel
- Try redirecting to file: `-serial file:output.log`

## Development

### Adding New Features

1. Edit `kernel/minimal_step9.c`
2. Rebuild: `make -f Makefile.step9 clean && make -f Makefile.step9`
3. Test: `make -f Makefile.step9 iso && qemu-system-x86_64 ...`

### Debug Tips

- Use `puts()` for serial output
- Check `exception_count` for unhandled exceptions
- GDB with QEMU: set breakpoints, examine registers

### Code Organization

All code is currently in `kernel/minimal_step9.c` for simplicity. For larger projects, consider modularizing:
- `boot/` - Boot and low-level assembly
- `kernel/acpi.c` - ACPI parsing
- `kernel/apic.c` - APIC/timer management
- `kernel/smp.c` - SMP initialization
- `kernel/mm.c` - Memory management
- `kernel/sync.c` - Synchronization primitives

## References

- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development resource
- [Intel SDM Volume 3](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - System programming guide
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/) - Boot protocol
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/search/documentation/hub.html) - x86-64 reference

## License

This is an educational project. Feel free to learn from, modify, and build upon it.

## Contributing

This is a learning project, but feedback and suggestions are welcome via GitHub issues.

---

**Last Updated**: 2025-11-16
**Status**: Stable - 4 CPUs boot successfully, BSP timer functional, AP timers pending debug
