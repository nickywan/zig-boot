# Trampoline for AP boot - Based on working linux-minimal version
# Loaded at 0x8000, transitions 16→32→64 bit

.section .trampoline, "ax"
.code16

.globl trampoline_start
.globl trampoline_end
.globl trampoline_cr3
.globl trampoline_stack
.globl trampoline_entry

trampoline_start:
    cli

    # CRITICAL: WBINVD first!
    wbinvd

    # Load GDT
    lgdt (gdt_ptr - trampoline_start + 0x8000)

    # Enable protected mode
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    # Far jump to 32-bit
    ljmpl $0x08, $(pm_start - trampoline_start + 0x8000)

.code32
pm_start:
    # Setup data segments
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    # Enable PAE
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    # Load CR3
    movl $(trampoline_cr3 - trampoline_start + 0x8000), %edi
    movl (%edi), %eax
    movl %eax, %cr3

    # Enable Long Mode
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    # Enable paging
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    # Far jump to 64-bit
    ljmpl $0x18, $(lm_start - trampoline_start + 0x8000)

.code64
lm_start:
    # Setup data segments
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    # Load stack
    movq $(trampoline_stack - trampoline_start + 0x8000), %rdi
    movq (%rdi), %rsp

    # Call entry point
    movq $(trampoline_entry - trampoline_start + 0x8000), %rax
    movq (%rax), %rax
    call *%rax

    # Halt if returns
    cli
    hlt
    jmp lm_start

# GDT
.align 16
gdt:
    .quad 0x0000000000000000    # Null
    .quad 0x00CF9A000000FFFF    # 32-bit code (0x08)
    .quad 0x00CF92000000FFFF    # 32-bit data (0x10)
    .quad 0x00AF9A000000FFFF    # 64-bit code (0x18)
    .quad 0x00AF92000000FFFF    # 64-bit data (0x20)
gdt_end:

gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt - trampoline_start + 0x8000

# Variables (patched by BSP)
.align 8
trampoline_cr3:
    .quad 0

.align 8
trampoline_stack:
    .quad 0

.align 8
trampoline_entry:
    .quad 0

trampoline_end:
