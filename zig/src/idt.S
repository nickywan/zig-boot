# IDT Assembly Stubs - Pure assembly handlers for interrupts/exceptions
# Based on the C version

.section .text

# Pure iretq handler (for unhandled interrupts)
.global pure_iretq_handler
pure_iretq_handler:
    iretq

# Common exception handler
.global exception_common
exception_common:
    # Save all registers
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    # Get vector and error code from stack
    mov 15*8(%rsp), %rdi    # vector (arg 1)
    mov 16*8(%rsp), %rsi    # error_code (arg 2)
    mov 17*8(%rsp), %rdx    # RIP (arg 3)

    # Call C handler
    call exception_handler

    # Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    # Remove vector and error code
    add $16, %rsp

    # Return from interrupt
    iretq

# Timer IRQ stub
.global timer_irq_stub
timer_irq_stub:
    # Save all registers
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    # Call C handler
    call timer_interrupt_handler

    # Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    # Return from interrupt
    iretq

# Exception stubs - without error code (CPU doesn't push one)
.macro EXCEPTION_STUB_NOERR num
.global exception_stub_\num
exception_stub_\num:
    pushq $0            # Push dummy error code
    pushq $\num         # Push exception number
    jmp exception_common
.endm

# Exception stubs - with error code (CPU pushes it automatically)
.macro EXCEPTION_STUB_ERR num
.global exception_stub_\num
exception_stub_\num:
    pushq $\num         # Push exception number (error code already on stack)
    jmp exception_common
.endm

# Generate all 32 exception stubs
EXCEPTION_STUB_NOERR 0      # Division By Zero
EXCEPTION_STUB_NOERR 1      # Debug
EXCEPTION_STUB_NOERR 2      # NMI
EXCEPTION_STUB_NOERR 3      # Breakpoint
EXCEPTION_STUB_NOERR 4      # Overflow
EXCEPTION_STUB_NOERR 5      # Bound Range Exceeded
EXCEPTION_STUB_NOERR 6      # Invalid Opcode
EXCEPTION_STUB_NOERR 7      # Device Not Available
EXCEPTION_STUB_ERR 8        # Double Fault
EXCEPTION_STUB_NOERR 9      # Coprocessor Segment Overrun
EXCEPTION_STUB_ERR 10       # Invalid TSS
EXCEPTION_STUB_ERR 11       # Segment Not Present
EXCEPTION_STUB_ERR 12       # Stack-Segment Fault
EXCEPTION_STUB_ERR 13       # General Protection Fault
EXCEPTION_STUB_ERR 14       # Page Fault
EXCEPTION_STUB_NOERR 15     # Reserved
EXCEPTION_STUB_NOERR 16     # x87 FPU Error
EXCEPTION_STUB_ERR 17       # Alignment Check
EXCEPTION_STUB_NOERR 18     # Machine Check
EXCEPTION_STUB_NOERR 19     # SIMD Exception
EXCEPTION_STUB_NOERR 20     # Virtualization Exception
EXCEPTION_STUB_ERR 21       # Control Protection Exception
EXCEPTION_STUB_NOERR 22     # Reserved
EXCEPTION_STUB_NOERR 23     # Reserved
EXCEPTION_STUB_NOERR 24     # Reserved
EXCEPTION_STUB_NOERR 25     # Reserved
EXCEPTION_STUB_NOERR 26     # Reserved
EXCEPTION_STUB_NOERR 27     # Reserved
EXCEPTION_STUB_NOERR 28     # Hypervisor Injection Exception
EXCEPTION_STUB_NOERR 29     # VMM Communication Exception
EXCEPTION_STUB_ERR 30       # Security Exception
EXCEPTION_STUB_NOERR 31     # Reserved
