# Ultra-simple trampoline just to test if AP starts
# Should just halt immediately without any mode transitions

.code16
.section .trampoline, "awx"
.globl trampoline_start
.globl trampoline_end
.globl trampoline_cr3
.globl trampoline_stack
.globl trampoline_entry

trampoline_start:
    # Just halt immediately - no mode transitions
    cli
    hlt
    jmp trampoline_start

# Dummy data to make size consistent
.org 0x1C0
trampoline_cr3:
    .long   0

.org 0x1C8
trampoline_stack:
    .quad   0

.org 0x1D0
trampoline_entry:
    .quad   0

.org 0x1E0
trampoline_end:
