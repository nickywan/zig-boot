.section .multiboot
.align 8

# Multiboot2 header
multiboot_header_start:
    .long 0xE85250D6                  # Magic
    .long 0                           # Architecture (0 = i386)
    .long multiboot_header_end - multiboot_header_start
    .long -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header_start))

    # End tag
    .word 0
    .word 0
    .long 8
multiboot_header_end:

.section .data
.align 4096
boot_page_table:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pml4:
    .skip 4096

# GDT for 64-bit mode
.section .rodata
.align 16
gdt64:
    .quad 0x0000000000000000    # Null descriptor
    .quad 0x00AF9A000000FFFF    # Code segment (64-bit)
    .quad 0x00AF92000000FFFF    # Data segment (64-bit)
gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .bss
.align 16
stack_bottom:
    .skip 16384  # 16 KB stack
stack_top:

.section .text
.global _start
.code32

_start:
    # Set up stack
    mov $stack_top, %esp
    mov %esp, %ebp

    # Save multiboot info pointer
    push %ebx

    # Set up paging for 64-bit mode
    # Identity map first 2MB

    # Clear tables
    mov $boot_pml4, %edi
    mov $0, %eax
    mov $3072, %ecx  # 3 pages * 1024 dwords
    rep stosl

    # PML4[0] -> PDPT
    mov $boot_pdpt, %eax
    or $0x03, %eax   # Present + RW
    mov $boot_pml4, %edi
    mov %eax, (%edi)

    # PDPT[0] -> Page Table
    mov $boot_page_table, %eax
    or $0x03, %eax
    mov $boot_pdpt, %edi
    mov %eax, (%edi)

    # Page Table: identity map first 2MB
    mov $boot_page_table, %edi
    mov $0x03, %eax  # Start at 0x0, Present + RW
    mov $512, %ecx
1:
    mov %eax, (%edi)
    add $0x1000, %eax
    add $8, %edi
    loop 1b

    # Load PML4 into CR3
    mov $boot_pml4, %eax
    mov %eax, %cr3

    # Enable PAE
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    # Enable long mode (set EFER.LME)
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    # Enable paging
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    # Load GDT
    lgdt gdt64_pointer

    # Far jump to 64-bit code
    pop %ebx  # Restore multiboot pointer
    push $8   # Code segment
    push $long_mode_start
    retf

.code64
long_mode_start:
    # Set up segment registers
    mov $16, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Call kernel main
    mov %rbx, %rdi  # Pass multiboot info as first argument
    call kernel_main

    # Hang
1:
    hlt
    jmp 1b
