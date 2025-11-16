# Ultra-minimal bootloader - Multiboot2 → 64-bit → Hello World

.section .multiboot
.align 8
multiboot2_header:
    .long 0xE85250D6                    # Magic
    .long 0                             # Architecture (i386)
    .long multiboot2_header_end - multiboot2_header
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))

    # End tag
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .data
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096
boot_pd_high:
    .skip 4096

.section .rodata
.align 16
gdt64:
    .quad 0x0000000000000000    # Null
    .quad 0x00AF9A000000FFFF    # Code 64
    .quad 0x00AF92000000FFFF    # Data 64
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .text
.global _start
.code32

_start:
    cli

    # Save Multiboot2 info pointer (EBX contains it)
    mov %ebx, %esi      # Save EBX to ESI (will preserve through mode switch)

    # Setup stack
    mov $stack_top, %esp

    # Clear page tables
    mov $boot_pml4, %edi
    mov $0, %eax
    mov $3072, %ecx
    rep stosl

    # PML4[0] → PDPT
    mov $boot_pdpt, %eax
    or $3, %eax
    mov $boot_pml4, %edi
    mov %eax, (%edi)

    # PDPT[0] → PD
    mov $boot_pd, %eax
    or $3, %eax
    mov $boot_pdpt, %edi
    mov %eax, (%edi)

    # PD: map first 1GB (512 * 2MB huge pages)
    mov $boot_pd, %edi
    mov $0x83, %eax         # Present + RW + Huge (2MB)
    mov $512, %ecx          # 512 entries = 1GB
1:
    mov %eax, (%edi)
    add $0x200000, %eax     # Next 2MB
    add $8, %edi
    loop 1b

    # PDPT[3] -> PD_HIGH for APIC region (0xC0000000-0xFFFFFFFF)
    mov $boot_pd_high, %eax
    or $3, %eax
    mov $boot_pdpt, %edi
    add $24, %edi           # PDPT[3]
    mov %eax, (%edi)

    # Map APIC at 0xFEE00000 in PD_HIGH
    # Offset in 4th GB: 0xFEE00000 - 0xC0000000 = 0x3EE00000
    # Entry in PD_HIGH: 0x3EE00000 / 0x200000 = 503
    mov $boot_pd_high, %edi
    add $(503 * 8), %edi
    mov $0xFEE00093, %eax   # APIC | Present + RW + PCD + Huge (cache-disable for MMIO!)
    mov %eax, (%edi)

    # Load PML4
    mov $boot_pml4, %eax
    mov %eax, %cr3

    # Enable PAE
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    # Enable long mode
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    # Enable paging
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    # Load GDT
    lgdt gdt64_ptr

    # Jump to 64-bit
    ljmp $8, $start64

.code64
start64:
    # Setup segments
    mov $16, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Pass Multiboot2 info address as first argument (RDI)
    mov %rsi, %rdi

    # Call kernel
    call kernel_main

    # Halt
1:
    hlt
    jmp 1b
