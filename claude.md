# Linux Ultra-Minimal x86-64 Kernel

A bare-metal x86-64 kernel built from scratch, demonstrating fundamental OS concepts including multiprocessor support, memory management, and hardware interrupts.

## Project Status (2025-11-16)

**Working Features:**
- ✅ Multiboot2 boot protocol
- ✅ 64-bit long mode (x86-64)
- ✅ Physical Memory Manager (PMM) with bitmap allocator
- ✅ Virtual Memory Manager (VMM) with recursive page tables
- ✅ Kernel heap allocator (16 MB)
- ✅ SMP support - boots all 4 CPUs successfully
- ✅ ACPI RSDP/MADT parsing
- ✅ Local APIC initialization (xAPIC mode)
- ✅ APIC timer on BSP (26 interrupts/2 seconds)
- ✅ Parallel computation across all CPUs
- ✅ Barrier synchronization primitives
- ✅ **TCG Mode**: Works perfectly in QEMU without KVM
- ✅ **Clean Codebase**: TSS/GDT dead code removed

**Known Limitations:**
- ⚠️ AP (Application Processor) timers disabled - causes system hang when enabled
- ⚠️ No TSS configured (not needed for ring 0 APIC timers - proven!)
- ⚠️ Timer only functional on BSP (CPU 0)

## Architecture

### Step-by-Step Evolution

This project is organized in progressive steps (Step 1-9), each building on the previous:

1. **Step 1**: Basic boot, serial output
2. **Step 2**: ACPI parsing
3. **Step 3**: APIC initialization
4. **Step 4**: SMP trampoline
5. **Step 5**: CPU synchronization
6. **Step 6**: Parallel computation tests
7. **Step 7**: Interrupt handling
8. **Step 8**: Timer implementation
9. **Step 9**: Memory management (current)

### Memory Layout

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

### Key Components

**Boot Process:**
- `boot/boot_minimal.S` - Multiboot2 header, initial setup, transitions to long mode
- `boot/trampoline.S` - AP (Application Processor) startup code

**Kernel Core:**
- `kernel/minimal_step9.c` - Main kernel with PMM, VMM, APIC, SMP support
- `kernel/interrupt_stub.o` - Pre-compiled interrupt handlers

**Build System:**
- `Makefile.step9` - Build configuration for Step 9
- `linker_minimal.ld` - Linker script for memory layout

## Building

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install build-essential grub-pc-bin xorriso qemu-system-x86

# Arch
sudo pacman -S base-devel grub xorriso qemu
```

### Compile and Run

```bash
# Build Step 9 (current)
make -f Makefile.step9

# Create bootable ISO
make -f Makefile.step9 iso

# Run in QEMU with 4 CPUs
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none -m 256M -smp 4
```

### Build Other Steps

```bash
# Build any specific step (1-9)
make -f Makefile.stepN
make -f Makefile.stepN iso
```

## Technical Details

### SMP Boot Sequence

1. BSP (Bootstrap Processor) starts in real mode
2. Multiboot2 loader loads kernel
3. BSP transitions: Real → Protected → Long mode
4. BSP sets up page tables, IDT, APIC
5. BSP sends INIT-SIPI-SIPI to wake APs
6. APs execute trampoline code at 0x8000
7. APs transition to long mode using BSP's page tables
8. APs load IDT and enable APIC
9. All CPUs synchronize and run parallel tests

### Memory Management

**PMM (Physical Memory Manager):**
- Bitmap-based allocator
- 4 KB page granularity
- Parses Multiboot2 memory map
- Tracks 16384 pages (64 MB)

**VMM (Virtual Memory Manager):**
- 4-level paging (PML4 → PDPT → PD → PT)
- Recursive mapping at index 511
- Identity mapping for first 2 MB
- Huge pages for APIC MMIO (with PCD flag)

**Heap:**
- Simple bump allocator
- 16 MB heap region
- `kmalloc()` / `kfree()` interface (currently unused)

### APIC Timer Implementation

**BSP Timer (Working):**
```c
apic_timer_init();  // Initialize timer
// Sets LVT Timer to vector 0x20, periodic mode
// Divider: 16, Initial count: 10000000
// Results in ~26 interrupts per 2 seconds
```

**AP Timers (Disabled):**
- Causes system hang when enabled
- Issue occurs during `apic_timer_init()` on APs
- Root cause: Unknown (not TSS-related, not interrupt ordering)
- Workaround: Only BSP timer enabled

### Parallel Tests

Three synchronization tests run on all CPUs:

1. **Parallel Counters** - Each CPU counts to 1M independently
2. **Distributed Sum** - Compute sum(1..10M) split across CPUs
3. **Barrier Sync** - All CPUs must reach barrier before proceeding

All tests currently pass with 4 CPUs active.

## Debug Information

### Serial Output

All kernel output goes to COM1 (serial port), captured by QEMU's `-serial stdio`.

### Debug Arrays

`ap_timer_debug[CPU][index]` - Per-CPU debug markers:
- `[0]` - AP started
- `[1]` - IDT loaded
- `[3]` - Before timer init
- `[4-5]` - Timer init markers
- `[6]` - SVR value
- `[7]` - LVT Timer value
- `[8]` - GDT/TSS markers

### QEMU Debug Options

```bash
# With CPU reset debugging
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none \
  -m 256M -smp 4 -no-reboot -d cpu_reset,guest_errors

# With GDB debugging
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none \
  -m 256M -smp 4 -s -S
# Then in another terminal: gdb kernel_step9.elf -ex "target remote :1234"
```

## Performance Notes

**Cache Behavior:**
- APIC MMIO mapped with PCD (Page Cache Disable) flag
- Prevents caching issues with MMIO registers
- Critical for stable APIC operation

**Overhead Optimizations:**
- Shared GDT across all CPUs (attempted but reverted due to regressions)
- Per-CPU interrupt stacks (4 KB each, pre-allocated but unused without TSS)
- Atomic operations for SMP-safe counters

## Lessons Learned

### TSS Not Required for Ring 0 Timers

Contrary to OSDev forum posts, TSS is **not** required for APIC timer interrupts in ring 0. The BSP timer works perfectly without any TSS configuration.

### GDT Reloading on APs

When APs transition from trampoline GDT to kernel GDT, CS must be reloaded with a far return:

```asm
pushq $0x08           # New CS selector
leaq 1f(%rip), %rax
pushq %rax
lretq                 # Far return reloads CS
1:
```

### APIC MMIO Requires PCD

The APIC MMIO region **must** be mapped with the PCD (Page Cache Disable) bit set, otherwise register reads/writes may be cached and cause unpredictable behavior.

### Wbinvd in Trampoline

The `wbinvd` instruction in the AP trampoline is critical - it flushes caches before the AP starts, ensuring it sees the latest memory state written by the BSP.

## Future Work

- [ ] Debug AP timer hang issue
- [ ] Implement proper TSS for ring transitions
- [ ] Add keyboard/interrupt remapping
- [ ] Implement proper scheduler
- [ ] Add user-space support (ring 3)
- [ ] Implement system calls

## References

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel SDM Volume 3](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/)

## License

This is an educational project. Feel free to learn from and build upon it.

---

**Last Updated:** 2025-11-16
**Status:** Step 9 - Memory Management (Stable with BSP timer)
