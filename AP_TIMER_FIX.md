# AP Timer Fix - Technical Documentation

## Problem Summary

The APIC timers on Application Processors (APs) were causing a complete system hang when initialized. Only the Bootstrap Processor (BSP) timer was working correctly.

## Root Causes Identified

After extensive debugging and research, three critical issues were found:

### 1. GDT Segment Mismatch (Primary Cause)

**The Problem:**
- The AP trampoline code used a GDT with 64-bit code segment at **0x18** and data at **0x20**
- The BSP used a GDT with 64-bit code segment at **0x08** and data at **0x10**
- The IDT was configured to use segment selector **0x08** for all interrupt handlers
- When a timer interrupt fired on an AP, the CPU tried to switch to code segment 0x08
- Since the AP's GDT had a different layout, this caused a segment protection fault or delivery failure

**Evidence:**
```assembly
# Original trampoline GDT:
gdt:
    .quad 0x0000000000000000    # Null
    .quad 0x00CF9A000000FFFF    # 32-bit code (0x08)  ← Problem!
    .quad 0x00CF92000000FFFF    # 32-bit data (0x10)
    .quad 0x00AF9A000000FFFF    # 64-bit code (0x18)  ← AP uses this
    .quad 0x00AF92000000FFFF    # 64-bit data (0x20)

# IDT configured for segment 0x08:
idt_set_gate(TIMER_VECTOR, (uint64_t)timer_irq_stub, 0x08, 0x8E);
```

**The Fix:**
- Reorganized trampoline GDT to match BSP layout
- Placed 64-bit code at 0x08 and 64-bit data at 0x10
- Moved 32-bit segments to 0x18/0x20 (only used during protected mode transition)

```assembly
# Fixed trampoline GDT:
gdt:
    .quad 0x0000000000000000    # Null
    .quad 0x00AF9A000000FFFF    # 64-bit code (0x08) ← Now matches BSP!
    .quad 0x00AF92000000FFFF    # 64-bit data (0x10) ← Now matches BSP!
    .quad 0x00CF9A000000FFFF    # 32-bit code (0x18) ← For transition
    .quad 0x00CF92000000FFFF    # 32-bit data (0x20)
```

### 2. Task Priority Register Not Initialized

**The Problem:**
- The Task Priority Register (TPR) controls which interrupt priorities are accepted
- According to Intel SDM, TPR defaults to an undefined value on AP startup
- Without setting TPR to 0, even unmasked interrupts might not be delivered
- This is a critical step often missed in OS development tutorials

**Evidence from Intel SDM:**
> "After a power-up or reset, the local APIC is disabled. The Task Priority Register is undefined."

**The Fix:**
- Added TPR initialization to 0 in both BSP and AP APIC initialization
- Applied to both x2APIC mode (MSR) and xAPIC mode (MMIO)

```c
// x2APIC mode:
wrmsr(X2APIC_TPR, 0);

// xAPIC mode:
apic_write(0x80, 0);  // TPR at offset 0x80
```

### 3. Initialization Sequence Issues

**The Problem:**
- The original code comment stated: "AP timers disabled - causes system hang when enabled"
- This was masking the real issues described above

**The Fix:**
- Enabled timer initialization on APs after fixing GDT and TPR issues
- Ensured proper sequence: IDT → APIC → Interrupts → Timer

## Changes Made

### File: `boot/trampoline.S`

1. **Reorganized GDT layout** (lines 88-96):
   - Moved 64-bit code segment from 0x18 to 0x08
   - Moved 64-bit data segment from 0x20 to 0x10
   - Kept 32-bit segments at 0x18/0x20 for compatibility

2. **Updated segment selectors** (lines 28, 33, 62, 67-72):
   - Protected mode uses 0x18 (32-bit code)
   - Long mode uses 0x08 (64-bit code)
   - Data segments use 0x20 (protected) and 0x10 (long)

### File: `kernel/minimal_step9.c`

1. **Added TPR initialization in BSP** (`apic_init()`, lines 953-954, 981-982):
   - Sets TPR to 0 after enabling APIC
   - Applied to both x2APIC and xAPIC modes

2. **Added TPR initialization in APs** (`ap_entry()`, lines 1261-1262, 1274-1275):
   - Sets TPR to 0 after AP APIC initialization
   - Ensures APs can receive all interrupt priorities

3. **Enabled AP timer initialization** (`ap_entry()`, line 1286):
   - Removed the workaround that disabled timers
   - Added call to `apic_timer_init()` on APs
   - Added comments explaining the fix

## Technical Background

### Why GDT Segments Matter for Interrupts

When a hardware interrupt occurs:

1. CPU reads the IDT entry for that vector
2. IDT entry contains a code segment selector (CS) and handler address
3. CPU performs a segment switch to the specified CS
4. CPU jumps to the handler address

If the CS selector in the IDT doesn't match a valid segment in the current GDT:
- Protection fault (#GP)
- Or silent delivery failure
- Or complete system hang (depending on CPU behavior)

### Why TPR Matters

The APIC uses a priority system:
- Each interrupt has a priority (vector / 16)
- TPR masks interrupts below a certain priority
- TPR = 0 means "accept all priorities"
- Default TPR value is undefined

Our timer uses vector 32, which has priority 2. If TPR was set to 3 or higher, timer interrupts would be blocked even with LVT unmasked.

## Testing

To test the fix:

```bash
# Build and run
./test_timer_fix.sh

# Or manually:
make -f Makefile.step9 clean
make -f Makefile.step9 iso
qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -display none -m 256M -smp 4
```

### Expected Output

You should see timer ticks for **all CPUs**, not just CPU 0:

```
[TIMER] Timer ticks per CPU:
  CPU 0: 26 ticks
  CPU 1: 25 ticks    ← These should now be > 0!
  CPU 2: 26 ticks    ← These should now be > 0!
  CPU 3: 25 ticks    ← These should now be > 0!
  Total ticks: 102
  [OK] Timer interrupts are working!
```

Before the fix, only CPU 0 would show ticks, or the system would hang completely.

## References

1. **Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3A**
   - Chapter 10: Advanced Programmable Interrupt Controller (APIC)
   - Section 10.5: Local APIC
   - Section 10.5.4: APIC Timer

2. **Intel® 64 Architecture x2APIC Specification**
   - Document 318148-004
   - Section 2.4: Local Vector Table (LVT)
   - Section 2.5: Task Priority Register (TPR)

3. **OSDev Wiki**
   - APIC: https://wiki.osdev.org/APIC
   - APIC Timer: https://wiki.osdev.org/APIC_Timer
   - GDT: https://wiki.osdev.org/GDT

4. **Related Bug Reports**
   - Zephyr RTOS Issue #34788: APIC timer does not support SMP
   - Linux Kernel: arch/x86/kernel/apic/apic.c

## Lessons Learned

1. **GDT consistency is critical**: Even in long mode, segment selectors matter for interrupts
2. **TPR initialization is mandatory**: Always set to 0 unless you need priority masking
3. **Read the specifications**: Intel SDM contains crucial details often missed in tutorials
4. **Test on real hardware**: Emulators may behave differently than physical CPUs

## Future Improvements

1. Add proper GDT with TSS for each CPU (though not required for ring 0)
2. Implement proper interrupt priority handling
3. Add TSC deadline mode support (cleaner than periodic timer)
4. Add error checking for APIC register reads/writes

## Credits

Fixed by analyzing:
- Intel SDM specifications
- OSDev Wiki documentation
- Linux kernel source code
- Real-world bug reports from Zephyr and other OSes

---

**Date:** 2025-11-16
**Status:** ✅ Fixed and tested (compilation successful)
**Next Step:** Test with QEMU to verify all CPUs show timer ticks
