# Trampoline code for booting Application Processors (APs)
# Loaded at 0x8000, runs in 16-bit real mode initially
# Transitions: 16-bit → 32-bit → 64-bit

.code16
.section .trampoline, "awx"
.global trampoline_start
.global trampoline_end
.global trampoline_cr3
.global trampoline_stack
.global trampoline_entry

trampoline_start:
    cli
    cld

    # Load GDT pointer (relative to 0x8000)
    lgdtl (trampoline_gdt_ptr - trampoline_start + 0x8000)

    # Enable protected mode
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0

    # Jump to 32-bit protected mode
    ljmp $0x08, $(trampoline_32 - trampoline_start + 0x8000)

.code32
trampoline_32:
    # Set up segments for 32-bit
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Load CR3 (page tables from BSP)
    mov (trampoline_cr3 - trampoline_start + 0x8000), %eax
    mov %eax, %cr3

    # Enable PAE
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    # Enable long mode (EFER.LME)
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    # Enable paging
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    # Load 64-bit GDT
    lgdtl (trampoline_gdt64_ptr - trampoline_start + 0x8000)

    # Jump to 64-bit long mode
    ljmp $0x08, $(trampoline_64 - trampoline_start + 0x8000)

.code64
trampoline_64:
    # Set up segments for 64-bit
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Load stack pointer
    mov (trampoline_stack - trampoline_start + 0x8000), %rsp

    # Jump to AP entry point
    mov (trampoline_entry - trampoline_start + 0x8000), %rax
    jmp *%rax

# GDT for 32-bit protected mode
.align 16
trampoline_gdt:
    .quad 0x0000000000000000    # Null descriptor
    .quad 0x00CF9A000000FFFF    # Code segment (32-bit)
    .quad 0x00CF92000000FFFF    # Data segment (32-bit)
trampoline_gdt_end:

trampoline_gdt_ptr:
    .word trampoline_gdt_end - trampoline_gdt - 1
    .long (trampoline_gdt - trampoline_start + 0x8000)

# GDT for 64-bit long mode
.align 16
trampoline_gdt64:
    .quad 0x0000000000000000    # Null descriptor
    .quad 0x00AF9A000000FFFF    # Code segment (64-bit)
    .quad 0x00AF92000000FFFF    # Data segment (64-bit)
trampoline_gdt64_end:

trampoline_gdt64_ptr:
    .word trampoline_gdt64_end - trampoline_gdt64 - 1
    .long (trampoline_gdt64 - trampoline_start + 0x8000)

# Variables set by BSP before sending SIPI
.align 8
trampoline_cr3:
    .long 0

.align 8
trampoline_stack:
    .quad 0

.align 8
trampoline_entry:
    .quad 0

trampoline_end:
