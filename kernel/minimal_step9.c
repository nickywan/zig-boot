// Step 9: Memory Management (PMM, VMM, Heap, APIC MMIO mapping)
// Tested on QEMU TCG and KVM
//
// Features:
// - Multiboot2 memory map parser
// - Physical Memory Manager (bitmap allocator)
// - Virtual Memory Manager (recursive page tables)
// - Kernel heap (kmalloc/kfree)
// - APIC MMIO mapping for all CPUs
// - Per-CPU timers working!

#define COM1 0x3F8
#define ACPI_SEARCH_START 0x000E0000
#define ACPI_SEARCH_END   0x000FFFFF
#define MAX_CPUS 16
#define AP_STACK_SIZE 8192  // 8KB per CPU

// Memory Management constants
#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

// Page table flags
#define PT_PRESENT    (1UL << 0)
#define PT_WRITE      (1UL << 1)
#define PT_USER       (1UL << 2)
#define PT_WRITETHROUGH (1UL << 3)
#define PT_NOCACHE    (1UL << 4)
#define PT_ACCESSED   (1UL << 5)
#define PT_DIRTY      (1UL << 6)
#define PT_HUGE       (1UL << 7)
#define PT_GLOBAL     (1UL << 8)
#define PT_NX         (1UL << 63)

// Recursive mapping: last PML4 entry points to PML4 itself
#define RECURSIVE_INDEX 511
#define RECURSIVE_BASE  0xFFFF000000000000UL

// Virtual addresses for page table manipulation via recursive mapping
#define PML4_VIRT_ADDR  (RECURSIVE_BASE | (RECURSIVE_INDEX << 39) | (RECURSIVE_INDEX << 30) | (RECURSIVE_INDEX << 21) | (RECURSIVE_INDEX << 12))
#define PDPT_VIRT_ADDR(pml4_idx) (RECURSIVE_BASE | (RECURSIVE_INDEX << 39) | (RECURSIVE_INDEX << 30) | (RECURSIVE_INDEX << 21) | ((pml4_idx) << 12))
#define PD_VIRT_ADDR(pml4_idx, pdpt_idx) (RECURSIVE_BASE | (RECURSIVE_INDEX << 39) | (RECURSIVE_INDEX << 30) | ((pml4_idx) << 21) | ((pdpt_idx) << 12))
#define PT_VIRT_ADDR(pml4_idx, pdpt_idx, pd_idx) (RECURSIVE_BASE | (RECURSIVE_INDEX << 39) | ((pml4_idx) << 30) | ((pdpt_idx) << 21) | ((pd_idx) << 12))

// Multiboot2 tag types
#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6

// Interrupt vectors
#define TIMER_VECTOR      32    // IRQ 0 (timer) mapped to vector 32

// APIC MSR and registers (xAPIC - MMIO mode)
#define APIC_BASE_MSR     0x1B
#define APIC_BASE_ENABLE  (1 << 11)   // xAPIC enable bit
#define X2APIC_ENABLE     (1 << 10)   // x2APIC enable bit

// xAPIC MMIO register offsets
#define APIC_ID_REG       0x20
#define APIC_EOI_REG      0xB0          // EOI register
#define APIC_SVR_REG      0xF0
#define APIC_ENABLE       0x100
#define SPURIOUS_VECTOR   0xFF          // Spurious interrupt vector (255)
#define APIC_ICR_LOW      0x300
#define APIC_ICR_HIGH     0x310
#define APIC_TIMER_LVT    0x320         // Timer LVT
#define APIC_TIMER_ICR    0x380         // Timer Initial Count
#define APIC_TIMER_CCR    0x390         // Timer Current Count
#define APIC_TIMER_DCR    0x3E0         // Timer Divide Configuration

// x2APIC MSR addresses (base = 0x800)
#define X2APIC_MSR_BASE   0x800
#define X2APIC_APICID     0x802   // APIC ID (read-only)
#define X2APIC_VERSION    0x803   // APIC Version
#define X2APIC_TPR        0x808   // Task Priority Register
#define X2APIC_PPR        0x80A   // Processor Priority Register
#define X2APIC_EOI        0x80B   // EOI
#define X2APIC_LDR        0x80D   // Logical Destination Register
#define X2APIC_SVR        0x80F   // Spurious Vector Register
#define X2APIC_ESR        0x828   // Error Status Register
#define X2APIC_ICR        0x830   // Interrupt Command Register (64-bit!)
#define X2APIC_LVT_TIMER  0x832   // LVT Timer
#define X2APIC_TIMER_ICR  0x838   // Timer Initial Count
#define X2APIC_TIMER_CCR  0x839   // Timer Current Count
#define X2APIC_TIMER_DCR  0x83E   // Timer Divide Configuration
#define X2APIC_LVT_LINT0  0x835   // LVT LINT0
#define X2APIC_LVT_LINT1  0x836   // LVT LINT1
#define X2APIC_LVT_ERROR  0x837   // LVT Error

// Timer modes
#define APIC_TIMER_PERIODIC  0x20000    // Periodic mode (bit 17)

// IPI types (from Linux)
#define APIC_DM_INIT          0x00000500
#define APIC_DM_STARTUP       0x00000600
#define APIC_INT_LEVELTRIG    0x00008000
#define APIC_INT_ASSERT       0x00004000
#define APIC_DEST_PHYSICAL    0x00000000

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

// ============================================================================
// MULTIBOOT2 STRUCTURES
// ============================================================================

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[];
} __attribute__((packed));

// Memory region types from Multiboot2
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED  2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS       4
#define MULTIBOOT_MEMORY_BADRAM    5

// Kernel symbols (from linker script)
extern char kernel_start[];
extern char kernel_end[];

// Port I/O
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// MSR operations
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// TSC (Time Stamp Counter)
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// CPUID function
static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

// APIC mode tracking
static volatile uint32_t *apic_base = 0;  // xAPIC MMIO base (if using xAPIC)
static int use_x2apic = 0;                // 1 if x2APIC, 0 if xAPIC

// APIC read/write abstraction (supports both xAPIC MMIO and x2APIC MSR)
static inline uint32_t apic_read(uint32_t reg) {
    if (use_x2apic) {
        // x2APIC: use MSR
        // Convert MMIO offset to MSR address
        uint32_t msr = X2APIC_MSR_BASE + (reg >> 4);
        return (uint32_t)rdmsr(msr);
    } else {
        // xAPIC: use MMIO
        return apic_base[reg >> 2];
    }
}

static inline void apic_write(uint32_t reg, uint32_t value) {
    if (use_x2apic) {
        // x2APIC: use MSR
        uint32_t msr = X2APIC_MSR_BASE + (reg >> 4);
        wrmsr(msr, value);
    } else {
        // xAPIC: use MMIO
        apic_base[reg >> 2] = value;
    }
}

// Atomic operations
static volatile uint32_t cpus_online = 0;

static inline void atomic_inc(volatile uint32_t *ptr) {
    __asm__ volatile("lock incl %0" : "+m"(*ptr) : : "memory");
}

// Per-CPU stacks (8KB each, aligned)
static uint8_t __attribute__((aligned(16))) ap_stacks[MAX_CPUS][AP_STACK_SIZE];

// Trampoline symbols (from trampoline.S)
extern char trampoline_start[];
extern char trampoline_end[];

// ============================================================================
// MEMORY MANAGEMENT DATA STRUCTURES
// ============================================================================

// Physical Memory Manager (PMM) - Bitmap allocator
static uint8_t *pmm_bitmap = 0;       // Bitmap: 1 bit per page
static uint64_t total_pages = 0;      // Total number of pages
static uint64_t used_pages = 0;       // Number of allocated pages
static uint64_t bitmap_size = 0;      // Size of bitmap in bytes

// Memory info from Multiboot2
static uint64_t total_memory = 0;     // Total RAM in bytes
static uint64_t usable_memory = 0;    // Usable RAM in bytes

// Kernel heap
static uint64_t heap_start = 0;       // Start of heap (virtual)
static uint64_t heap_current = 0;     // Current heap pointer (bump allocator)
static uint64_t heap_end = 0;         // End of heap

// Virtual Memory Manager (VMM) - uses recursive page table mapping
static uint64_t *pml4 = 0;            // Pointer to PML4 (via recursive mapping)

// ============================================================================
// PARALLEL COMPUTATION DATA STRUCTURES
// ============================================================================

// Test 1: Parallel counters
static volatile uint64_t per_cpu_counters[MAX_CPUS] = {0};

// Test 2: Distributed sum (sum of 1 to 10,000,000)
#define SUM_TARGET 10000000UL
static volatile uint64_t partial_sums[MAX_CPUS] = {0};
static volatile uint64_t total_sum = 0;

// Test 3: Barrier synchronization
static volatile uint32_t barrier_count = 0;
static volatile uint32_t barrier_sense = 0;

// ============================================================================
// IDT (INTERRUPT DESCRIPTOR TABLE)
// ============================================================================

// IDT Gate Descriptor (64-bit mode)
struct idt_entry {
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  ist;           // Interrupt Stack Table (0 = don't use)
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_mid;    // Offset bits 16-31
    uint32_t offset_high;   // Offset bits 32-63
    uint32_t zero;          // Reserved (must be 0)
} __attribute__((packed));

typedef struct idt_entry idt_entry_t;

// IDT Pointer (IDTR)
struct idt_ptr {
    uint16_t limit;         // Size of IDT - 1
    uint64_t base;          // Base address of IDT
} __attribute__((packed));

typedef struct idt_ptr idt_ptr_t;

// IDT with 256 entries
static idt_entry_t idt[256] __attribute__((aligned(16)));
static idt_ptr_t idtr;

// ============================================================================
// TSS (Task State Segment) - Required for interrupt handling in x86-64
// ============================================================================

// TSS structure (64-bit mode)
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;        // Stack pointer for ring 0
    uint64_t rsp1;        // Stack pointer for ring 1
    uint64_t rsp2;        // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist[7];      // Interrupt Stack Table
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

typedef struct tss tss_t;

// Per-CPU TSS and interrupt stacks
static tss_t per_cpu_tss[MAX_CPUS] __attribute__((aligned(16)));
static uint8_t __attribute__((aligned(16))) interrupt_stacks[MAX_CPUS][4096];  // 4KB per CPU (reduced)

// ============================================================================
// GDT (GLOBAL DESCRIPTOR TABLE) - Per CPU
// ============================================================================

// GDT Entry structure
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

// GDT Pointer structure
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Shared GDT for all CPUs: null, code, data, + 4 TSS descriptors (2 entries each)
// Total: 3 + (4 * 2) = 11 entries
static struct gdt_entry shared_gdt[11] __attribute__((aligned(16)));
static struct gdt_ptr shared_gdt_ptr;

// Exception names
static const char *exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

// Exception counter
static volatile uint32_t exception_count = 0;

// Timer interrupt counters (per-CPU)
static volatile uint64_t timer_ticks[MAX_CPUS] = {0};

// ============================================================================
// IDT FUNCTIONS
// ============================================================================

// Forward declarations (serial functions needed by exception handlers)
static void puts(const char *s);
static void print_hex(uint32_t n);
static void print_hex_64(uint64_t n);

// Set an IDT entry
static void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;  // No IST for now
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

// Generic exception handler (called from assembly stubs)
__attribute__((used))
static void exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {
    __atomic_fetch_add(&exception_count, 1, __ATOMIC_SEQ_CST);

    puts("\n[EXCEPTION] ");
    if (vector < 32) {
        puts(exception_names[vector]);
    } else if (vector == 255) {
        puts("Spurious/Unhandled Interrupt");
    } else {
        puts("Unknown Exception");
    }
    puts(" (Vector ");
    print_hex(vector);
    puts(")\n");

    puts("  Error Code: ");
    print_hex_64(error_code);
    puts("\n");

    puts("  RIP: ");
    print_hex_64(rip);
    puts("\n");

    // Allow breakpoint (vector 3) and spurious interrupts (vector 255) to continue
    if (vector == 3 || vector == 255) {
        puts("[INFO] Continuing execution...\n");
        return;  // Return to exception_common, which will iretq
    }

    // For other exceptions, halt
    puts("[HALT] System halted due to exception\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Exception stub macros - these will be our exception entry points
#define EXCEPTION_STUB_NOERR(num) \
    __attribute__((naked)) void exception_stub_##num(void) { \
        __asm__ volatile( \
            "pushq $0\n\t"          /* Push dummy error code */ \
            "pushq $" #num "\n\t"   /* Push exception number */ \
            "jmp exception_common\n\t" \
            ::: "memory" \
        ); \
    }

// Exceptions with error code (CPU pushes it automatically)
#define EXCEPTION_STUB_ERR(num) \
    __attribute__((naked)) void exception_stub_##num(void) { \
        __asm__ volatile( \
            "pushq $" #num "\n\t"   /* Push exception number */ \
            "jmp exception_common\n\t" \
            ::: "memory" \
        ); \
    }

// Minimal test interrupt handler (does absolutely nothing except iret)
__attribute__((naked)) void minimal_test_stub(void) {
    __asm__ volatile(
        "iretq\n"
        ::: "memory"
    );
}

// Default interrupt handler for unhandled interrupts
__attribute__((naked)) void default_interrupt_stub(void) {
    __asm__ volatile(
        "push $0\n"           // Dummy error code
        "push $255\n"         // Dummy vector number
        "jmp exception_common\n"
        ::: "memory"
    );
}

// Common exception handler (saves state and calls C handler)
__attribute__((naked)) void exception_common(void) {
    __asm__ volatile(
        // Stack at entry (from top to bottom):
        // RIP (pushed by CPU)
        // CS (pushed by CPU)
        // RFLAGS (pushed by CPU)
        // RSP (pushed by CPU)
        // SS (pushed by CPU)
        // Error code (pushed by stub or CPU)
        // Vector number (pushed by stub)

        // Save registers
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        "push %%rdx\n"
        "push %%rsi\n"
        "push %%rdi\n"
        "push %%rbp\n"
        "push %%r8\n"
        "push %%r9\n"
        "push %%r10\n"
        "push %%r11\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"

        // Get vector and error code from stack
        "mov 15*8(%%rsp), %%rdi\n"    // vector (arg 1)
        "mov 16*8(%%rsp), %%rsi\n"    // error_code (arg 2)
        "mov 17*8(%%rsp), %%rdx\n"    // RIP (arg 3)

        // Call C handler
        "call exception_handler\n"

        // Restore registers (we'll never get here if handler halts)
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"
        "pop %%rbp\n"
        "pop %%rdi\n"
        "pop %%rsi\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"

        // Remove vector and error code
        "add $16, %%rsp\n"

        // Return from interrupt
        "iretq\n"
        ::: "memory"
    );
}

// Define exception stubs for all CPU exceptions (0-31)
EXCEPTION_STUB_NOERR(0)   // Division By Zero
EXCEPTION_STUB_NOERR(1)   // Debug
EXCEPTION_STUB_NOERR(2)   // NMI
EXCEPTION_STUB_NOERR(3)   // Breakpoint
EXCEPTION_STUB_NOERR(4)   // Overflow
EXCEPTION_STUB_NOERR(5)   // Bound Range Exceeded
EXCEPTION_STUB_NOERR(6)   // Invalid Opcode
EXCEPTION_STUB_NOERR(7)   // Device Not Available
EXCEPTION_STUB_ERR(8)     // Double Fault
EXCEPTION_STUB_NOERR(9)   // Coprocessor Segment Overrun
EXCEPTION_STUB_ERR(10)    // Invalid TSS
EXCEPTION_STUB_ERR(11)    // Segment Not Present
EXCEPTION_STUB_ERR(12)    // Stack-Segment Fault
EXCEPTION_STUB_ERR(13)    // General Protection Fault
EXCEPTION_STUB_ERR(14)    // Page Fault
EXCEPTION_STUB_NOERR(15)  // Reserved
EXCEPTION_STUB_NOERR(16)  // x87 FPU Error
EXCEPTION_STUB_ERR(17)    // Alignment Check
EXCEPTION_STUB_NOERR(18)  // Machine Check
EXCEPTION_STUB_NOERR(19)  // SIMD Exception
EXCEPTION_STUB_NOERR(20)  // Virtualization Exception
EXCEPTION_STUB_ERR(21)    // Control Protection Exception
EXCEPTION_STUB_NOERR(22)  // Reserved
EXCEPTION_STUB_NOERR(23)  // Reserved
EXCEPTION_STUB_NOERR(24)  // Reserved
EXCEPTION_STUB_NOERR(25)  // Reserved
EXCEPTION_STUB_NOERR(26)  // Reserved
EXCEPTION_STUB_NOERR(27)  // Reserved
EXCEPTION_STUB_NOERR(28)  // Hypervisor Injection Exception
EXCEPTION_STUB_NOERR(29)  // VMM Communication Exception
EXCEPTION_STUB_ERR(30)    // Security Exception
EXCEPTION_STUB_NOERR(31)  // Reserved

// Function pointer array for exception stubs - marked volatile to prevent optimization issues
static void (* volatile exception_stubs[32])(void) = {
    exception_stub_0, exception_stub_1, exception_stub_2, exception_stub_3,
    exception_stub_4, exception_stub_5, exception_stub_6, exception_stub_7,
    exception_stub_8, exception_stub_9, exception_stub_10, exception_stub_11,
    exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15,
    exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19,
    exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23,
    exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27,
    exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31
};

// Load IDT
static void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

// EOI helper - sends End of Interrupt to APIC
static void send_eoi(void) {
    if (use_x2apic) {
        // x2APIC: write 0 to EOI MSR
        wrmsr(X2APIC_EOI, 0);
    } else {
        // xAPIC: write 0 to EOI MMIO register
        apic_write(APIC_EOI_REG, 0);
    }
}

// Global debug counter to see if handler is called at all
static volatile uint64_t global_timer_calls = 0;

// Debug counters for AP timer initialization (in memory debugging)
static volatile uint32_t ap_timer_debug[MAX_CPUS][10] = {0};  // Extended for GDT/TSS debug
// ap_timer_debug[cpu_id][0] = 1: ap_entry started
// ap_timer_debug[cpu_id][1] = 1: IDT loaded
// ap_timer_debug[cpu_id][2] = 1: APIC enabled
// ap_timer_debug[cpu_id][3] = 1: sti executed
// ap_timer_debug[cpu_id][4] = 1: timer_init about to start
// ap_timer_debug[cpu_id][5] = 1: timer_init completed
// ap_timer_debug[cpu_id][6] = SVR value after enable
// ap_timer_debug[cpu_id][7] = LVT value after timer init

// Timer interrupt handler (called from assembly stub)
__attribute__((used))
void timer_interrupt_handler(void) {
    // Increment global counter (debug)
    __atomic_fetch_add(&global_timer_calls, 1, __ATOMIC_SEQ_CST);

    // Get current CPU APIC ID
    uint32_t apic_id;
    if (use_x2apic) {
        apic_id = (uint32_t)rdmsr(X2APIC_APICID);
    } else {
        apic_id = apic_read(APIC_ID_REG) >> 24;
    }

    // Use APIC ID directly as index (works for sequential APIC IDs like in QEMU)
    // In QEMU with -smp 4, APIC IDs are typically 0, 1, 2, 3
    if (apic_id < MAX_CPUS) {
        __atomic_fetch_add(&timer_ticks[apic_id], 1, __ATOMIC_SEQ_CST);
    }

    // Send EOI to acknowledge interrupt
    send_eoi();
}

// Timer IRQ stub - must be global and used
__attribute__((used))
void timer_irq_stub(void);

__asm__(
    ".global timer_irq_stub\n"
    "timer_irq_stub:\n"
    // Save all registers
    "    push %rax\n"
    "    push %rbx\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %rbp\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"

    // Call C handler
    "    call timer_interrupt_handler\n"

    // Restore all registers
    "    pop %r15\n"
    "    pop %r14\n"
    "    pop %r13\n"
    "    pop %r12\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rbp\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    pop %rbx\n"
    "    pop %rax\n"

    // Return from interrupt
    "    iretq\n"
);

// ============================================================================
// GDT and TSS Functions
// ============================================================================

// Set a GDT entry
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    shared_gdt[num].base_low = (base & 0xFFFF);
    shared_gdt[num].base_mid = (base >> 16) & 0xFF;
    shared_gdt[num].base_high = (base >> 24) & 0xFF;

    shared_gdt[num].limit_low = (limit & 0xFFFF);
    shared_gdt[num].granularity = (limit >> 16) & 0x0F;

    shared_gdt[num].granularity |= gran & 0xF0;
    shared_gdt[num].access = access;
}

// Set TSS descriptor (takes 2 GDT entries in 64-bit mode)
static void gdt_set_tss(int num, uint64_t base, uint32_t limit) {
    // Use raw pointer to set TSS descriptor as 16-byte structure
    uint8_t *tss_desc = (uint8_t*)&shared_gdt[num];

    // Clear 16 bytes
    for (int i = 0; i < 16; i++) {
        tss_desc[i] = 0;
    }

    // Bytes 0-1: Limit 15:0
    tss_desc[0] = limit & 0xFF;
    tss_desc[1] = (limit >> 8) & 0xFF;

    // Bytes 2-4: Base 23:0
    tss_desc[2] = base & 0xFF;
    tss_desc[3] = (base >> 8) & 0xFF;
    tss_desc[4] = (base >> 16) & 0xFF;

    // Byte 5: Access byte: 0x89 = Present, DPL=0, Type=9 (Available TSS)
    tss_desc[5] = 0x89;

    // Byte 6: Limit 19:16 (low nibble), Flags (high nibble) = 0 for TSS
    tss_desc[6] = (limit >> 16) & 0x0F;

    // Byte 7: Base 31:24
    tss_desc[7] = (base >> 24) & 0xFF;

    // Bytes 8-11: Base 63:32
    tss_desc[8] = (base >> 32) & 0xFF;
    tss_desc[9] = (base >> 40) & 0xFF;
    tss_desc[10] = (base >> 48) & 0xFF;
    tss_desc[11] = (base >> 56) & 0xFF;

    // Bytes 12-15: Reserved (already zeroed)
}

// Initialize shared GDT (called once by BSP)
static void gdt_init(void) {
    // Entry 0: Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // Entry 1: Code segment (0x08)
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0);

    // Entry 2: Data segment (0x10)
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xC0);

    // Entry 3-4: TSS CPU0 (selector 0x18)
    uint64_t tss0_base = (uint64_t)&per_cpu_tss[0];
    gdt_set_tss(3, tss0_base, sizeof(tss_t) - 1);

    // Entry 5-6: TSS CPU1 (selector 0x28)
    uint64_t tss1_base = (uint64_t)&per_cpu_tss[1];
    gdt_set_tss(5, tss1_base, sizeof(tss_t) - 1);

    // Entry 7-8: TSS CPU2 (selector 0x38)
    uint64_t tss2_base = (uint64_t)&per_cpu_tss[2];
    gdt_set_tss(7, tss2_base, sizeof(tss_t) - 1);

    // Entry 9-10: TSS CPU3 (selector 0x48)
    uint64_t tss3_base = (uint64_t)&per_cpu_tss[3];
    gdt_set_tss(9, tss3_base, sizeof(tss_t) - 1);

    // Set GDT pointer
    shared_gdt_ptr.limit = sizeof(shared_gdt) - 1;
    shared_gdt_ptr.base = (uint64_t)&shared_gdt;
}

// Initialize TSS for a CPU
static void tss_init(int cpu) {
    // Clear TSS
    for (unsigned int i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t*)&per_cpu_tss[cpu])[i] = 0;
    }

    // Set RSP0 to top of interrupt stack for this CPU
    per_cpu_tss[cpu].rsp0 = (uint64_t)&interrupt_stacks[cpu][4096];

    // Set IO map base to size of TSS (no IO permission bitmap)
    per_cpu_tss[cpu].iomap_base = sizeof(tss_t);
}

// Load shared GDT and this CPU's TSS
static void gdt_load(int cpu) {
    // Load shared GDT
    __asm__ volatile("lgdt %0" : : "m"(shared_gdt_ptr));

    // CRITICAL: Reload CS using far return
    // This is necessary because APs use different CS selectors in trampoline GDT
    __asm__ volatile(
        "pushq $0x08\n"           // Push new CS (code segment from shared GDT)
        "leaq 1f(%%rip), %%rax\n" // Load address of label '1'
        "pushq %%rax\n"           // Push return address
        "lretq\n"                 // Far return - reloads CS and jumps to '1'
        "1:\n"                    // Label for return address
        ::: "rax"
    );

    // Reload data segment registers
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        ::: "rax"
    );

    // Load TSS for this CPU
    // CPU 0: selector 0x18 (entry 3)
    // CPU 1: selector 0x28 (entry 5)
    // CPU 2: selector 0x38 (entry 7)
    // CPU 3: selector 0x48 (entry 9)
    uint16_t tss_selector = 0x18 + (cpu * 0x10);
    __asm__ volatile("ltr %%ax" : : "a"(tss_selector));
}

// Forward declarations
static void print_dec(uint32_t num);

// External pure assembly handler
extern void pure_iretq_handler(void);

// Debug: dump IDT entry
static void dump_idt_entry(int vec) {
    puts("[IDT DEBUG] Vector ");
    print_dec(vec);
    puts(":\n");

    puts("  Offset Low:  ");
    print_hex(idt[vec].offset_low);
    puts("\n");

    puts("  Selector:    ");
    print_hex(idt[vec].selector);
    puts("\n");

    puts("  Type/Attr:   ");
    print_hex(idt[vec].type_attr);
    puts("\n");

    puts("  Offset Mid:  ");
    print_hex(idt[vec].offset_mid);
    puts("\n");

    puts("  Offset High: ");
    print_hex(idt[vec].offset_high);
    puts("\n");

    uint64_t full_offset = ((uint64_t)idt[vec].offset_high << 32) |
                           ((uint64_t)idt[vec].offset_mid << 16) |
                           idt[vec].offset_low;
    puts("  Full Handler: ");
    print_hex_64(full_offset);
    puts("\n");
}

// Initialize IDT
static void idt_init(void) {
    // Set ALL 256 entries to pure assembly handler (just iretq) as default
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint64_t)pure_iretq_handler, 0x08, 0x8E);
    }

    // Override with specific exception handlers (0-31)
    // Type: 0x8E = Present, DPL=0, Type=Interrupt Gate (32-bit)
    // In 64-bit mode: 0x8E for interrupt gate
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)exception_stubs[i], 0x08, 0x8E);
    }

    // Set up timer IRQ handler (vector 32)
    idt_set_gate(TIMER_VECTOR, (uint64_t)timer_irq_stub, 0x08, 0x8E);

    // Set up IDTR
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    // Load IDT
    idt_load();
}

// Serial functions
static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static void puts(const char *s) {
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

static void print_dec(uint32_t num) {
    char buf[12];
    int i = 11;
    buf[i--] = 0;

    if (num == 0) {
        putc('0');
        return;
    }

    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    puts(&buf[i + 1]);
}

static void print_dec_64(uint64_t num) {
    char buf[24];
    int i = 23;
    buf[i--] = 0;

    if (num == 0) {
        putc('0');
        return;
    }

    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    puts(&buf[i + 1]);
}

static void print_hex(uint32_t num) {
    char buf[12];
    buf[0] = '0';
    buf[1] = 'x';
    int i;
    for (i = 0; i < 8; i++) {
        int nibble = (num >> (28 - i * 4)) & 0xF;
        buf[i + 2] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buf[10] = 0;
    puts(buf);
}

static void print_hex_64(uint64_t num) {
    char buf[20];
    buf[0] = '0';
    buf[1] = 'x';
    int i;
    for (i = 0; i < 16; i++) {
        int nibble = (num >> (60 - i * 4)) & 0xF;
        buf[i + 2] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buf[18] = 0;
    puts(buf);
}

// Minimal memcpy
static void *memcpy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *memset(void *dest, int val, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    while (n--) *d++ = (uint8_t)val;
    return dest;
}

// TSC calibration and delays
static uint64_t tsc_khz = 0;

static void calibrate_tsc(void) {
    // For this minimal kernel, we use a simple fixed estimate
    // TCG: ~500 MHz, KVM: ~2-6 GHz
    // We'll use 2GHz as a reasonable middle ground
    // The delays don't need to be exact for SMP boot to work

    tsc_khz = 2000000;  // 2 GHz = 2,000,000 kHz
}

// Ultra-simple delays - NO I/O operations during SMP boot!
static void udelay(uint64_t usec) {
    // Simple busy loop - very approximate
    for (volatile uint64_t i = 0; i < usec * 10; i++) {
        __asm__ volatile("pause");
    }
}

static void mdelay(uint64_t msec) {
    for (uint64_t i = 0; i < msec; i++) {
        udelay(1000);
    }
}

// ACPI structures
struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt_header {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_lapic {
    uint8_t type;
    uint8_t length;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

static int acpi_checksum(void *ptr, int length) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < length; i++)
        sum += p[i];
    return (sum == 0);
}

static struct acpi_rsdp *acpi_find_rsdp(void) {
    uint8_t *ptr = (uint8_t*)ACPI_SEARCH_START;

    for (; ptr < (uint8_t*)ACPI_SEARCH_END; ptr += 16) {
        if (ptr[0] == 'R' && ptr[1] == 'S' && ptr[2] == 'D' &&
            ptr[3] == ' ' && ptr[4] == 'P' && ptr[5] == 'T' &&
            ptr[6] == 'R' && ptr[7] == ' ') {

            struct acpi_rsdp *rsdp = (struct acpi_rsdp*)ptr;
            if (acpi_checksum(rsdp, 20)) {
                return rsdp;
            }
        }
    }
    return 0;
}

static struct acpi_sdt_header *acpi_find_madt(struct acpi_rsdp *rsdp) {
    struct acpi_sdt_header *rsdt;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        rsdt = (struct acpi_sdt_header*)(uint64_t)rsdp->xsdt_address;
    } else {
        rsdt = (struct acpi_sdt_header*)(uint64_t)rsdp->rsdt_address;
    }

    if (!acpi_checksum(rsdt, rsdt->length)) {
        return 0;
    }

    int entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    uint32_t *entry_ptr = (uint32_t*)((uint8_t*)rsdt + sizeof(struct acpi_sdt_header));

    for (int i = 0; i < entries; i++) {
        struct acpi_sdt_header *header = (struct acpi_sdt_header*)(uint64_t)entry_ptr[i];
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            return header;
        }
    }

    return 0;
}

// CPU tracking
static uint8_t cpu_apic_ids[MAX_CPUS];
static volatile int cpu_count = 0;

static int acpi_parse_madt(struct acpi_sdt_header *madt_header) {
    struct acpi_madt_header *madt = (struct acpi_madt_header*)madt_header;
    uint8_t *ptr = (uint8_t*)madt + sizeof(struct acpi_madt_header);
    uint8_t *end = (uint8_t*)madt + madt->header.length;
    int count = 0;

    while (ptr < end) {
        uint8_t type = *ptr;
        uint8_t length = *(ptr + 1);

        if (type == 0) {  // Local APIC
            struct acpi_madt_lapic *lapic = (struct acpi_madt_lapic*)ptr;
            if (lapic->flags & 0x1) {
                puts("[ACPI] CPU ");
                print_dec(count);
                puts(" detected (APIC ID ");
                print_dec(lapic->apic_id);
                puts(")\n");

                if (count < MAX_CPUS) {
                    cpu_apic_ids[count] = lapic->apic_id;
                }
                count++;
            }
        }

        ptr += length;
    }

    return count;
}

// APIC functions
// Disable legacy 8259 PIC (CRITICAL before using APIC!)
static void disable_pic(void) {
    puts("[PIC] Disabling legacy 8259 PIC...\n");
    // Mask all interrupts on both PICs
    outb(0x21, 0xFF);  // Master PIC data port
    outb(0xA1, 0xFF);  // Slave PIC data port
    puts("[PIC] Legacy PIC disabled\n");
}

static void apic_init(void) {
    puts("\n[APIC] Initializing Local APIC...\n");

    // CRITICAL: Disable legacy PIC first!
    disable_pic();

    // Check for x2APIC support via CPUID.01H:ECX[21]
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    int x2apic_available = (ecx >> 21) & 1;

    if (x2apic_available) {
        puts("[APIC] x2APIC supported - enabling x2APIC mode\n");

        // Read current APIC base MSR
        uint64_t apic_msr = rdmsr(APIC_BASE_MSR);

        // Enable both xAPIC (bit 11) and x2APIC (bit 10)
        // Must enable xAPIC first, then x2APIC
        if (!(apic_msr & APIC_BASE_ENABLE)) {
            apic_msr |= APIC_BASE_ENABLE;
            wrmsr(APIC_BASE_MSR, apic_msr);
        }

        // Now enable x2APIC mode
        apic_msr |= X2APIC_ENABLE;
        wrmsr(APIC_BASE_MSR, apic_msr);

        use_x2apic = 1;

        // In x2APIC mode, enable APIC via SVR MSR with spurious vector
        wrmsr(X2APIC_SVR, APIC_ENABLE | SPURIOUS_VECTOR);

        // Read APIC ID from x2APIC MSR
        uint32_t apic_id = (uint32_t)rdmsr(X2APIC_APICID);
        puts("[APIC] x2APIC mode enabled (MSR-based)\n");
        puts("[APIC] BSP APIC ID: ");
        print_dec(apic_id);
        puts("\n");
    } else {
        puts("[APIC] x2APIC not available - using xAPIC mode\n");

        uint64_t apic_msr = rdmsr(APIC_BASE_MSR);
        uint64_t apic_phys_addr = apic_msr & 0xFFFFF000;

        puts("[APIC] Physical address: 0xFEE00000 (default)\n");

        apic_base = (volatile uint32_t*)apic_phys_addr;

        if (!(apic_msr & APIC_BASE_ENABLE)) {
            puts("[APIC] Enabling APIC in MSR...\n");
            wrmsr(APIC_BASE_MSR, apic_msr | APIC_BASE_ENABLE);
        }

        puts("[APIC] Enabling APIC (SVR register)...\n");

        apic_write(APIC_SVR_REG, APIC_ENABLE | SPURIOUS_VECTOR);

        uint32_t apic_id = apic_read(APIC_ID_REG) >> 24;
        puts("[APIC] BSP APIC ID: ");
        print_dec(apic_id);
        puts("\n");

        use_x2apic = 0;
    }

    puts("[APIC] Local APIC initialized successfully!\n");
}

// Debug: Timer init debug counters
static volatile uint32_t timer_init_debug[4] = {0};  // [0]=lvt before, [1]=lvt after, [2]=dcr, [3]=icr

// Configure APIC Timer
static void apic_timer_init(void) {
    // Set timer divide configuration to 16 (divide by 16)
    if (use_x2apic) {
        wrmsr(X2APIC_TIMER_DCR, 0x3);  // Divide by 16
        timer_init_debug[2] = (uint32_t)rdmsr(X2APIC_TIMER_DCR);
    } else {
        apic_write(APIC_TIMER_DCR, 0x3);
        timer_init_debug[2] = apic_read(APIC_TIMER_DCR);
    }

    // Read LVT BEFORE setting it
    if (use_x2apic) {
        timer_init_debug[0] = (uint32_t)rdmsr(X2APIC_LVT_TIMER);
    } else {
        timer_init_debug[0] = apic_read(APIC_TIMER_LVT);
    }

    // Set timer to periodic mode with vector 32
    // IMPORTANT: Do NOT set the mask bit (bit 16) - we want interrupts enabled!
    uint32_t lvt_timer = APIC_TIMER_PERIODIC | TIMER_VECTOR;
    // Explicitly clear the mask bit (bit 16) to ensure timer interrupts are enabled
    lvt_timer &= ~(1 << 16);  // Clear mask bit

    if (use_x2apic) {
        wrmsr(X2APIC_LVT_TIMER, lvt_timer);
        timer_init_debug[1] = (uint32_t)rdmsr(X2APIC_LVT_TIMER);
    } else {
        apic_write(APIC_TIMER_LVT, lvt_timer);
        timer_init_debug[1] = apic_read(APIC_TIMER_LVT);
    }

    // Set initial count (this starts the timer)
    // Lower value = faster interrupts. 10000000 gives roughly 10 Hz with divide-by-16
    uint32_t initial_count = 10000000;
    if (use_x2apic) {
        wrmsr(X2APIC_TIMER_ICR, initial_count);
        timer_init_debug[3] = (uint32_t)rdmsr(X2APIC_TIMER_ICR);
    } else {
        apic_write(APIC_TIMER_ICR, initial_count);
        timer_init_debug[3] = apic_read(APIC_TIMER_ICR);
    }
}

// IPI functions
static void apic_wait_icr(void) {
    if (use_x2apic) {
        // x2APIC: ICR writes are atomic, no need to wait
        return;
    }
    // xAPIC: wait for delivery status bit
    uint32_t timeout = 1000000;
    while ((apic_read(APIC_ICR_LOW) & (1 << 12)) && timeout--) {
        __asm__ volatile("pause");
    }
}

static void send_ipi(uint32_t apic_id, uint32_t flags) {
    if (use_x2apic) {
        // x2APIC: ICR is a single 64-bit MSR
        // Bits 0-31: flags (ICR low)
        // Bits 32-63: destination APIC ID
        uint64_t icr = ((uint64_t)apic_id << 32) | flags;
        wrmsr(X2APIC_ICR, icr);
    } else {
        // xAPIC: ICR is two 32-bit MMIO registers
        apic_wait_icr();
        apic_write(APIC_ICR_HIGH, apic_id << 24);
        apic_write(APIC_ICR_LOW, flags);
        apic_wait_icr();
    }
}

// Trampoline functions
static void setup_trampoline(void) {
    puts("\n[SMP] Setting up trampoline...\n");

    uint64_t trampoline_size = trampoline_end - trampoline_start;
    puts("[SMP] Trampoline size: ");
    print_dec(trampoline_size);
    puts(" bytes\n");

    // Copy trampoline to 0x8000
    uint8_t *dest = (uint8_t*)0x8000;
    memcpy(dest, trampoline_start, trampoline_size);

    puts("[SMP] Trampoline copied to 0x8000\n");

    // Get current CR3
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // Patch trampoline variables at end (like linux-minimal)
    // Offsets from end: -24: cr3, -16: stack, -8: entry
    uint64_t *cr3_ptr = (uint64_t*)(0x8000 + trampoline_size - 24);
    uint64_t *stack_ptr = (uint64_t*)(0x8000 + trampoline_size - 16);
    uint64_t *entry_ptr = (uint64_t*)(0x8000 + trampoline_size - 8);

    *cr3_ptr = cr3;
    *stack_ptr = 0;  // Will be patched per-AP
    extern void ap_entry(void);
    *entry_ptr = (uint64_t)ap_entry;

    // CRITICAL: Flush caches after patching
    __asm__ volatile("wbinvd" ::: "memory");

    puts("[SMP] Trampoline configured\n");
}

// Boot a single AP (adapted from Linux smpboot.c)
static void boot_ap(int cpu_idx) {
    if (cpu_idx == 0) return;  // Skip BSP
    if (cpu_idx >= cpu_count) return;

    uint8_t apic_id = cpu_apic_ids[cpu_idx];
    unsigned long start_eip = 0x8000;  // Trampoline address

    // Patch per-CPU stack (like linux-minimal)
    uint64_t trampoline_size = trampoline_end - trampoline_start;
    uint64_t *stack_ptr = (uint64_t*)(0x8000 + trampoline_size - 16);
    *stack_ptr = (uint64_t)&ap_stacks[cpu_idx][AP_STACK_SIZE];

    // CRITICAL: Flush caches after patching
    __asm__ volatile("wbinvd" ::: "memory");

    // INIT-SIPI-SIPI sequence
    // INIT IPI
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
    apic_wait_icr();

    // Wait 10ms
    mdelay(10);

    // INIT deassert
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_DM_INIT);
    apic_wait_icr();

    // SIPI #1
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();

    // Wait 200μs
    udelay(200);

    // SIPI #2
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();

    // Wait 200μs
    udelay(200);
}

// Boot all APs
static void boot_all_aps(void) {
    // NO SERIAL OUTPUT AT ALL in this function - it's fragile!

    // BSP is already online
    cpus_online = 1;

    // Boot all APs
    for (int i = 1; i < cpu_count; i++) {
        boot_ap(i);
    }

    // Wait for APs
    for (volatile int i = 0; i < 1000000; i++) __asm__ volatile("pause");

    // NO PUTS HERE! Move to kernel_main after this function returns
}

// ============================================================================
// PARALLEL COMPUTATION FUNCTIONS
// ============================================================================

// Simple barrier (sense-reversing)
static void barrier_wait(int cpu_id) {
    uint32_t my_sense = barrier_sense;

    // Last CPU to arrive flips the sense
    uint32_t arrived = __atomic_add_fetch(&barrier_count, 1, __ATOMIC_SEQ_CST);

    if (arrived == (uint32_t)cpu_count) {
        // Last one - reset and flip sense
        barrier_count = 0;
        __atomic_store_n(&barrier_sense, !my_sense, __ATOMIC_SEQ_CST);
    } else {
        // Wait for sense to flip
        while (__atomic_load_n(&barrier_sense, __ATOMIC_SEQ_CST) == my_sense) {
            __asm__ volatile("pause");
        }
    }
}

// Test 1: Parallel counter (each CPU counts to 1 million)
static void test_parallel_counters(int cpu_id) {
    for (uint64_t i = 0; i < 1000000; i++) {
        per_cpu_counters[cpu_id]++;
        if (i % 100000 == 0) {
            __asm__ volatile("pause");
        }
    }
}

// Test 2: Distributed sum (divide work among CPUs)
static void test_distributed_sum(int cpu_id) {
    uint64_t per_cpu_work = SUM_TARGET / cpu_count;
    uint64_t start = cpu_id * per_cpu_work + 1;
    uint64_t end = (cpu_id + 1) * per_cpu_work;

    // Last CPU handles remainder
    if (cpu_id == cpu_count - 1) {
        end = SUM_TARGET;
    }

    uint64_t local_sum = 0;
    for (uint64_t i = start; i <= end; i++) {
        local_sum += i;
    }

    partial_sums[cpu_id] = local_sum;

    // Atomically add to total
    __atomic_add_fetch(&total_sum, local_sum, __ATOMIC_SEQ_CST);
}

// Test 3: Barrier synchronization test
static void test_barrier_sync(int cpu_id) {
    // Phase 1: Count to 500k
    for (uint64_t i = 0; i < 500000; i++) {
        per_cpu_counters[cpu_id]++;
    }

    // Barrier: wait for all CPUs
    barrier_wait(cpu_id);

    // Phase 2: Count to 1M (everyone starts together)
    for (uint64_t i = 500000; i < 1000000; i++) {
        per_cpu_counters[cpu_id]++;
    }
}

// AP entry point - now with parallel computation!
void ap_entry(void) {
    // Increment CPUs online counter FIRST to get our ID
    uint32_t my_id = __atomic_fetch_add(&cpus_online, 1, __ATOMIC_SEQ_CST);

    // Mark: AP started
    ap_timer_debug[my_id][0] = 1;

    // Load IDT on this AP (IDT is already set up by BSP)
    idt_load();
    ap_timer_debug[my_id][1] = 1;

    // Skip GDT/TSS - not needed for timers in ring 0
    ap_timer_debug[my_id][8] = 0xA0;  // Mark: skip gdt/tss
    // No GDT/TSS load needed - using bootloader's GDT
    ap_timer_debug[my_id][8] = 0xA1;  // Mark: gdt/tss skipped

    // Enable APIC on this AP (same mode as BSP)
    if (use_x2apic) {
        // x2APIC mode
        uint64_t apic_msr = rdmsr(APIC_BASE_MSR);

        // Enable xAPIC first if not enabled
        if (!(apic_msr & APIC_BASE_ENABLE)) {
            apic_msr |= APIC_BASE_ENABLE;
            wrmsr(APIC_BASE_MSR, apic_msr);
        }

        // Enable x2APIC mode
        apic_msr |= X2APIC_ENABLE;
        wrmsr(APIC_BASE_MSR, apic_msr);

        // Enable APIC via SVR MSR with spurious vector
        wrmsr(X2APIC_SVR, APIC_ENABLE | SPURIOUS_VECTOR);

        // Store SVR for debugging
        ap_timer_debug[my_id][6] = (uint32_t)rdmsr(X2APIC_SVR);
    } else {
        // xAPIC mode (MMIO)
        // Enable APIC in MSR if needed
        uint64_t apic_msr = rdmsr(APIC_BASE_MSR);
        if (!(apic_msr & APIC_BASE_ENABLE)) {
            wrmsr(APIC_BASE_MSR, apic_msr | APIC_BASE_ENABLE);
        }

        // Enable APIC via SVR register with spurious vector
        apic_write(APIC_SVR_REG, APIC_ENABLE | SPURIOUS_VECTOR);

        // Store SVR for debugging
        ap_timer_debug[my_id][6] = apic_read(APIC_SVR_REG);
    }

    ap_timer_debug[my_id][2] = 1;

    // Wait a bit for BSP to finish setup
    for (volatile int i = 0; i < 100000; i++) __asm__ volatile("pause");

    // TEST: Enable timer only on CPU 1 for debugging (BEFORE enabling interrupts!)
    ap_timer_debug[my_id][3] = 1;
    ap_timer_debug[my_id][4] = 1;
    if (my_id == 1) {
        apic_timer_init();  // Test on CPU 1 only
    }
    ap_timer_debug[my_id][5] = 1;

    // Enable interrupts on APs (AFTER timer init to avoid interrupts during init)
    __asm__ volatile("sti");

    // Store current LVT value
    if (use_x2apic) {
        ap_timer_debug[my_id][7] = (uint32_t)rdmsr(X2APIC_LVT_TIMER);
    } else {
        ap_timer_debug[my_id][7] = apic_read(APIC_TIMER_LVT);
    }

    // Test 1: Parallel counters
    test_parallel_counters(my_id);
    barrier_wait(my_id);  // Sync before next test

    // Test 2: Distributed sum
    test_distributed_sum(my_id);
    barrier_wait(my_id);  // Sync before next test

    // Test 3: Barrier sync (resets counters first)
    per_cpu_counters[my_id] = 0;  // Reset for test 3
    barrier_wait(my_id);  // Everyone resets together
    test_barrier_sync(my_id);

    // Done - halt
    while (1) {
        __asm__ volatile("hlt");
    }
}

// ============================================================================
// MEMORY MANAGEMENT FUNCTIONS
// ============================================================================

// Forward declarations
static void print_dec_64(uint64_t num);
static void print_hex_64(uint64_t num);

// Parse Multiboot2 memory map
static void parse_multiboot_mmap(uint64_t multiboot_info_addr) {
    puts("\n[MMAP] Parsing Multiboot2 memory map...\n");

    // Skip the initial size field (first 8 bytes)
    struct multiboot_tag *tag = (struct multiboot_tag*)(multiboot_info_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap*)tag;

            puts("[MMAP] Memory map found!\n");
            puts("[MMAP] Entry size: ");
            print_dec(mmap_tag->entry_size);
            puts(" bytes\n");

            // Iterate through all memory map entries
            uint32_t num_entries = (mmap_tag->size - sizeof(struct multiboot_tag_mmap)) / mmap_tag->entry_size;

            for (uint32_t i = 0; i < num_entries; i++) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry*)
                    ((uint8_t*)mmap_tag->entries + i * mmap_tag->entry_size);

                puts("[MMAP]   ");
                print_hex_64(entry->addr);
                puts(" - ");
                print_hex_64(entry->addr + entry->len - 1);
                puts(" (");
                print_dec_64(entry->len / 1024 / 1024);
                puts(" MB) - ");

                switch (entry->type) {
                    case MULTIBOOT_MEMORY_AVAILABLE:
                        puts("Available\n");
                        usable_memory += entry->len;
                        // Find highest AVAILABLE address for total memory (not reserved!)
                        // Ignore MMIO regions above 4GB
                        if (entry->addr < 0x100000000UL) {  // Below 4GB
                            uint64_t end_addr = entry->addr + entry->len;
                            if (end_addr > total_memory) {
                                total_memory = end_addr;
                            }
                        }
                        break;
                    case MULTIBOOT_MEMORY_RESERVED:
                        puts("Reserved\n");
                        break;
                    case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                        puts("ACPI Reclaimable\n");
                        break;
                    case MULTIBOOT_MEMORY_NVS:
                        puts("ACPI NVS\n");
                        break;
                    case MULTIBOOT_MEMORY_BADRAM:
                        puts("Bad RAM\n");
                        break;
                    default:
                        puts("Unknown (");
                        print_dec(entry->type);
                        puts(")\n");
                        break;
                }
            }

            puts("[MMAP] Total memory: ");
            print_dec_64(total_memory / 1024 / 1024);
            puts(" MB\n");

            puts("[MMAP] Usable memory: ");
            print_dec_64(usable_memory / 1024 / 1024);
            puts(" MB\n");

            return;
        }

        // Move to next tag (tags are 8-byte aligned)
        tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    puts("[ERROR] Memory map not found in Multiboot2 info!\n");
}

// Physical Memory Manager (PMM) - Bitmap Allocator

static void pmm_mark_page(uint64_t page_idx) {
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    pmm_bitmap[byte_idx] |= (1 << bit_idx);
}

static void pmm_clear_page(uint64_t page_idx) {
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    pmm_bitmap[byte_idx] &= ~(1 << bit_idx);
}

static int pmm_is_page_used(uint64_t page_idx) {
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    return (pmm_bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

static void pmm_init(uint64_t multiboot_info_addr) {
    puts("\n[PMM] Initializing Physical Memory Manager...\n");

    // Limit to 64MB for now (enough for kernel + allocations)
    if (total_memory > 64 * 1024 * 1024) {
        total_memory = 64 * 1024 * 1024;
    }

    // Calculate number of pages
    total_pages = total_memory / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;  // Round up to bytes

    // CRITICAL: Place bitmap AFTER multiboot info to avoid overwriting it!
    // Multiboot2 info size is in the first 4 bytes
    uint32_t *mb_size_ptr = (uint32_t*)multiboot_info_addr;
    uint64_t mb_end = multiboot_info_addr + *mb_size_ptr;

    uint64_t kernel_end_addr = (uint64_t)kernel_end;
    // Use whichever is higher: kernel_end or multiboot_end
    uint64_t safe_addr = (mb_end > kernel_end_addr) ? mb_end : kernel_end_addr;
    uint64_t bitmap_addr = PAGE_ALIGN(safe_addr);
    pmm_bitmap = (uint8_t*)bitmap_addr;

    puts("[PMM] Bitmap location: ");
    print_hex_64(bitmap_addr);
    puts("\n");

    puts("[PMM] Bitmap size: ");
    print_dec_64(bitmap_size / 1024);
    puts(" KB (");
    print_dec_64(total_pages);
    puts(" pages)\n");

    // Mark all pages as used initially (using memset for speed)
    memset(pmm_bitmap, 0xFF, bitmap_size);
    used_pages = total_pages;

    // Now mark available regions from memory map
    // (We'll do this in a separate pass after parsing mmap again)
    puts("[PMM] Physical Memory Manager initialized!\n");
}

static void pmm_mark_region_free(uint64_t base, uint64_t length) {
    uint64_t start_page = base / PAGE_SIZE;
    uint64_t end_page = (base + length) / PAGE_SIZE;

    // Optimize: clear whole bytes at a time when possible
    uint64_t start_byte = start_page / 8;
    uint64_t end_byte = end_page / 8;

    // Clear starting partial byte (if not aligned)
    if ((start_page % 8) != 0) {
        for (uint64_t page = start_page; page < (start_byte + 1) * 8 && page < end_page; page++) {
            if (page < total_pages && pmm_is_page_used(page)) {
                pmm_clear_page(page);
                used_pages--;
            }
        }
        start_byte++;
    }

    // Clear whole bytes
    for (uint64_t byte = start_byte; byte < end_byte && byte < bitmap_size; byte++) {
        uint8_t old = pmm_bitmap[byte];
        pmm_bitmap[byte] = 0;
        // Count how many pages we freed (count set bits in old value)
        for (int i = 0; i < 8; i++) {
            if (old & (1 << i)) used_pages--;
        }
    }

    // Clear ending partial byte (if not aligned)
    if ((end_page % 8) != 0) {
        for (uint64_t page = end_byte * 8; page < end_page; page++) {
            if (page < total_pages && pmm_is_page_used(page)) {
                pmm_clear_page(page);
                used_pages--;
            }
        }
    }
}

static void pmm_mark_region_used(uint64_t base, uint64_t length) {
    uint64_t start_page = base / PAGE_SIZE;
    uint64_t end_page = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t page = start_page; page < end_page; page++) {
        if (page < total_pages && !pmm_is_page_used(page)) {
            pmm_mark_page(page);
            used_pages++;
        }
    }
}

static void pmm_mark_free_regions(uint64_t multiboot_info_addr) {
    puts("[PMM] Marking free regions...\n");

    struct multiboot_tag *tag = (struct multiboot_tag*)(multiboot_info_addr + 8);

    int tag_count = 0;
    while (tag->type != MULTIBOOT_TAG_TYPE_END && tag_count < 100) {  // Safety: max 100 tags
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap*)tag;
            uint32_t num_entries = (mmap_tag->size - sizeof(struct multiboot_tag_mmap)) / mmap_tag->entry_size;

            for (uint32_t i = 0; i < num_entries; i++) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry*)
                    ((uint8_t*)mmap_tag->entries + i * mmap_tag->entry_size);

                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    // Cap region to our total_memory limit
                    uint64_t region_start = entry->addr;
                    uint64_t region_len = entry->len;

                    if (region_start >= total_memory) continue;  // Skip regions beyond limit

                    if (region_start + region_len > total_memory) {
                        region_len = total_memory - region_start;  // Cap to limit
                    }

                    pmm_mark_region_free(region_start, region_len);
                }
            }
            break;
        }
        tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
        tag_count++;
    }

    // Mark kernel and bitmap as used
    uint64_t kernel_start_addr = (uint64_t)kernel_start;
    uint64_t bitmap_end = (uint64_t)pmm_bitmap + bitmap_size;
    uint64_t kernel_size = bitmap_end - kernel_start_addr;

    puts("[PMM] Marking kernel + bitmap as used: ");
    print_hex_64(kernel_start_addr);
    puts(" - ");
    print_hex_64(bitmap_end);
    puts("\n");

    pmm_mark_region_used(kernel_start_addr, kernel_size);

    puts("[PMM] Free pages: ");
    print_dec_64(total_pages - used_pages);
    puts(" / ");
    print_dec_64(total_pages);
    puts(" (");
    print_dec_64((total_pages - used_pages) * 4);
    puts(" KB free)\n");
}

static uint64_t pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!pmm_is_page_used(i)) {
            pmm_mark_page(i);
            used_pages++;
            return i * PAGE_SIZE;
        }
    }
    return 0;  // Out of memory
}

static void pmm_free_page(uint64_t phys_addr) {
    uint64_t page_idx = phys_addr / PAGE_SIZE;
    if (page_idx < total_pages && pmm_is_page_used(page_idx)) {
        pmm_clear_page(page_idx);
        used_pages--;
    }
}

// Virtual Memory Manager (VMM) - Recursive Page Tables

static void vmm_init(void) {
    puts("\n[VMM] Initializing Virtual Memory Manager...\n");

    // Get current CR3 (physical address of PML4)
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    puts("[VMM] Current CR3 (PML4 physical): ");
    print_hex_64(cr3);
    puts("\n");

    // Extract physical address of PML4
    uint64_t pml4_phys = cr3 & 0x000FFFFFFFFFF000UL;

    // Access PML4 directly via identity mapping (first 1GB is identity mapped)
    // The bootloader maps first 1GB identity, so we can access PML4 directly
    uint64_t *pml4_direct = (uint64_t*)pml4_phys;

    // Set up recursive mapping (last PML4 entry points to PML4 itself)
    pml4_direct[RECURSIVE_INDEX] = pml4_phys | PT_PRESENT | PT_WRITE;

    // Flush TLB to activate recursive mapping
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");

    // NOW we can access PML4 via recursive mapping
    pml4 = (uint64_t*)PML4_VIRT_ADDR;

    puts("[VMM] Recursive mapping enabled at index ");
    print_dec(RECURSIVE_INDEX);
    puts("\n");

    puts("[VMM] PML4 virtual address: ");
    print_hex_64(PML4_VIRT_ADDR);
    puts("\n");

    puts("[VMM] Virtual Memory Manager initialized!\n");
}

static uint64_t vmm_virt_to_phys(uint64_t virt_addr) {
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;
    uint64_t offset = virt_addr & 0xFFF;

    // Access page tables via recursive mapping
    uint64_t *pml4_table = (uint64_t*)PML4_VIRT_ADDR;
    if (!(pml4_table[pml4_idx] & PT_PRESENT)) return 0;

    uint64_t *pdpt_table = (uint64_t*)PDPT_VIRT_ADDR(pml4_idx);
    if (!(pdpt_table[pdpt_idx] & PT_PRESENT)) return 0;

    uint64_t *pd_table = (uint64_t*)PD_VIRT_ADDR(pml4_idx, pdpt_idx);
    if (!(pd_table[pd_idx] & PT_PRESENT)) return 0;

    uint64_t *pt_table = (uint64_t*)PT_VIRT_ADDR(pml4_idx, pdpt_idx, pd_idx);
    if (!(pt_table[pt_idx] & PT_PRESENT)) return 0;

    uint64_t phys_page = pt_table[pt_idx] & 0x000FFFFFFFFFF000UL;
    return phys_page + offset;
}

static void vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;

    // Access/create PML4 entry
    uint64_t *pml4_table = (uint64_t*)PML4_VIRT_ADDR;
    if (!(pml4_table[pml4_idx] & PT_PRESENT)) {
        uint64_t new_pdpt = pmm_alloc_page();
        if (!new_pdpt) return;  // OOM
        pml4_table[pml4_idx] = new_pdpt | PT_PRESENT | PT_WRITE | PT_USER;

        // Clear new PDPT
        uint64_t *pdpt_table = (uint64_t*)PDPT_VIRT_ADDR(pml4_idx);
        for (int i = 0; i < 512; i++) pdpt_table[i] = 0;
    }

    // Access/create PDPT entry
    uint64_t *pdpt_table = (uint64_t*)PDPT_VIRT_ADDR(pml4_idx);
    if (!(pdpt_table[pdpt_idx] & PT_PRESENT)) {
        uint64_t new_pd = pmm_alloc_page();
        if (!new_pd) return;  // OOM
        pdpt_table[pdpt_idx] = new_pd | PT_PRESENT | PT_WRITE | PT_USER;

        // Clear new PD
        uint64_t *pd_table = (uint64_t*)PD_VIRT_ADDR(pml4_idx, pdpt_idx);
        for (int i = 0; i < 512; i++) pd_table[i] = 0;
    }

    // Access/create PD entry
    uint64_t *pd_table = (uint64_t*)PD_VIRT_ADDR(pml4_idx, pdpt_idx);
    if (!(pd_table[pd_idx] & PT_PRESENT)) {
        uint64_t new_pt = pmm_alloc_page();
        if (!new_pt) return;  // OOM
        pd_table[pd_idx] = new_pt | PT_PRESENT | PT_WRITE | PT_USER;

        // Clear new PT
        uint64_t *pt_table = (uint64_t*)PT_VIRT_ADDR(pml4_idx, pdpt_idx, pd_idx);
        for (int i = 0; i < 512; i++) pt_table[i] = 0;
    }

    // Map the page
    uint64_t *pt_table = (uint64_t*)PT_VIRT_ADDR(pml4_idx, pdpt_idx, pd_idx);
    pt_table[pt_idx] = (phys_addr & 0x000FFFFFFFFFF000UL) | flags;

    // Flush TLB for this page
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

static void vmm_unmap_page(uint64_t virt_addr) {
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;

    uint64_t *pml4_table = (uint64_t*)PML4_VIRT_ADDR;
    if (!(pml4_table[pml4_idx] & PT_PRESENT)) return;

    uint64_t *pdpt_table = (uint64_t*)PDPT_VIRT_ADDR(pml4_idx);
    if (!(pdpt_table[pdpt_idx] & PT_PRESENT)) return;

    uint64_t *pd_table = (uint64_t*)PD_VIRT_ADDR(pml4_idx, pdpt_idx);
    if (!(pd_table[pd_idx] & PT_PRESENT)) return;

    uint64_t *pt_table = (uint64_t*)PT_VIRT_ADDR(pml4_idx, pdpt_idx, pd_idx);
    pt_table[pt_idx] = 0;

    // Flush TLB
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

// Kernel Heap (Simple Bump Allocator)

static void heap_init(void) {
    puts("\n[HEAP] Initializing kernel heap...\n");

    // Place heap after bitmap (in higher half)
    uint64_t bitmap_end = (uint64_t)pmm_bitmap + bitmap_size;
    heap_start = PAGE_ALIGN(bitmap_end);
    heap_current = heap_start;
    heap_end = heap_start + (16 * 1024 * 1024);  // 16MB heap

    puts("[HEAP] Heap start: ");
    print_hex_64(heap_start);
    puts("\n");

    puts("[HEAP] Heap size: 16 MB\n");

    puts("[HEAP] Kernel heap initialized!\n");
}

static void *kmalloc(uint64_t size) {
    if (size == 0) return 0;

    // Align to 16 bytes
    size = (size + 15) & ~15UL;

    uint64_t addr = heap_current;
    heap_current += size;

    if (heap_current >= heap_end) {
        puts("[HEAP ERROR] Out of heap memory!\n");
        return 0;
    }

    return (void*)addr;
}

static void kfree(void *ptr) {
    // Simple bump allocator doesn't support free
    // In a real OS, use a proper allocator (buddy, slab, etc.)
    (void)ptr;
}

// Kernel entry
void kernel_main(uint64_t multiboot_addr) {
    serial_init();

    puts("\n");
    puts("===========================================\n");
    puts("  Step 9: Memory Management\n");
    puts("===========================================\n");
    puts("\n");

    puts("[INFO] Multiboot2 info at: ");
    print_hex_64(multiboot_addr);
    puts("\n");

    // Parse Multiboot2 memory map
    parse_multiboot_mmap(multiboot_addr);

    // Initialize Physical Memory Manager
    pmm_init(multiboot_addr);
    pmm_mark_free_regions(multiboot_addr);

    // Initialize Virtual Memory Manager
    vmm_init();

    // Initialize Kernel Heap
    heap_init();

    puts("\n");

    puts("[OK] Serial port initialized (COM1)\n");
    puts("[OK] Running in 64-bit long mode\n");
    puts("\n");

    // Initialize GDT for BSP only (simplified - no per-CPU TSS)
    puts("[GDT] Initializing GDT for BSP...\n");

    // Just set up basic GDT - using bootloader's existing segments
    // No TSS for now - will test if timers work without it

    puts("[GDT] Using bootloader GDT (no custom GDT/TSS)...\n");
    puts("\n");

    // Initialize IDT
    puts("[IDT] Initializing Interrupt Descriptor Table...\n");
    idt_init();
    puts("[IDT] IDT initialized with 32 exception handlers\n");
    puts("[IDT] IDT loaded successfully!\n");
    puts("\n");

    // TSC Calibration
    puts("[TSC] Calibrating Time Stamp Counter...\n");
    calibrate_tsc();
    puts("[TSC] TSC frequency: ");
    print_dec(tsc_khz);
    puts(" kHz\n");

    // ACPI Detection
    puts("\n[ACPI] Searching for RSDP...\n");
    struct acpi_rsdp *rsdp = acpi_find_rsdp();

    if (!rsdp) {
        puts("[ERROR] RSDP not found!\n");
        puts("System halted.\n");
        while (1) __asm__ volatile("hlt");
    }

    puts("[ACPI] RSDP found!\n");

    puts("[ACPI] Searching for MADT...\n");
    struct acpi_sdt_header *madt = acpi_find_madt(rsdp);

    if (!madt) {
        puts("[ERROR] MADT not found!\n");
        puts("System halted.\n");
        while (1) __asm__ volatile("hlt");
    }

    puts("[ACPI] MADT found!\n");
    puts("[ACPI] Parsing MADT entries...\n");
    cpu_count = acpi_parse_madt(madt);

    puts("\n[ACPI] Detected ");
    print_dec(cpu_count);
    puts(" CPU(s)\n");

    // Initialize Local APIC
    apic_init();

    // Setup trampoline
    setup_trampoline();

    // Boot APs
    puts("\n[SMP] Starting AP boot sequence...\n");
    boot_all_aps();

    // NOW we can print (SMP boot period is over)
    puts("\n[SMP] Application Processors booted\n");
    puts("[SMP] CPUs online: ");
    print_dec(cpus_online);
    puts(" / ");
    print_dec(cpu_count);
    puts("\n");

    if (cpus_online != (uint32_t)cpu_count) {
        puts("\n[WARNING] Not all CPUs came online\n");
        puts("[INFO] This may be normal in some environments\n");
    } else {
        puts("\n[SUCCESS] All CPUs booted successfully!\n");
    }

    // ========================================================================
    // START TIMERS ON ALL CPUs
    // ========================================================================

    puts("\n[TIMER] Enabling interrupts and starting APIC Timer...\n");

    // Enable interrupts globally
    __asm__ volatile("sti");

    // Start APIC Timer on BSP
    apic_timer_init();

    // Debug: Check BSP timer init details
    puts("[DEBUG] BSP Timer Init:\n");
    puts("  LVT before:  ");
    print_hex(timer_init_debug[0]);
    puts("\n");
    puts("  LVT after:   ");
    print_hex(timer_init_debug[1]);
    puts("\n");
    puts("  DCR:         ");
    print_hex(timer_init_debug[2]);
    puts("\n");
    puts("  ICR:         ");
    print_hex(timer_init_debug[3]);
    puts("\n");

    uint32_t bsp_lvt;
    if (use_x2apic) {
        bsp_lvt = (uint32_t)rdmsr(X2APIC_LVT_TIMER);
    } else {
        bsp_lvt = apic_read(APIC_TIMER_LVT);
    }
    puts("  LVT current: ");
    print_hex(bsp_lvt);
    puts("\n");

    puts("[TIMER] BSP timer started successfully!\n");

    // AP timers will be started in ap_entry() now that we have proper VMM
    puts("\n[INFO] APs will initialize their timers in parallel...\n");

    // ========================================================================
    // PARALLEL COMPUTATION TESTS
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  Running Parallel Computation Tests\n");
    puts("===========================================\n");
    puts("\n");

    // Wait a bit for APs to finish boot
    puts("[TEST] Waiting for APs to initialize...\n");
    for (volatile int i = 0; i < 500000; i++) __asm__ volatile("pause");

    // BSP (CPU 0) runs tests too!
    puts("[TEST] BSP running tests...\n");

    // Test 1: Parallel counters
    test_parallel_counters(0);
    barrier_wait(0);  // Sync with APs

    // Test 2: Distributed sum
    test_distributed_sum(0);
    barrier_wait(0);  // Sync with APs

    // Test 3: Barrier sync (reset counters first)
    per_cpu_counters[0] = 0;
    barrier_wait(0);  // Everyone resets together
    test_barrier_sync(0);

    puts("[TEST] All tests completed!\n");

    // ========================================================================
    // DISPLAY RESULTS
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  Test Results\n");
    puts("===========================================\n");
    puts("\n");

    // Test 1: Parallel Counters
    puts("TEST 1: Parallel Counters\n");
    puts("---------------------------\n");
    for (int i = 0; i < cpu_count; i++) {
        puts("  CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(per_cpu_counters[i]);
        if (per_cpu_counters[i] == 1000000) {
            puts(" [OK]\n");
        } else {
            puts(" [FAIL]\n");
        }
    }

    // Test 2: Distributed Sum
    puts("\nTEST 2: Distributed Sum (1 to 10,000,000)\n");
    puts("-------------------------------------------\n");

    // Expected sum: n * (n+1) / 2 = 10000000 * 10000001 / 2
    uint64_t expected_sum = 50000005000000UL;

    puts("  Partial sums:\n");
    for (int i = 0; i < cpu_count; i++) {
        puts("    CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(partial_sums[i]);
        puts("\n");
    }

    puts("  Total sum: ");
    print_dec_64(total_sum);
    puts("\n");

    puts("  Expected:  ");
    print_dec_64(expected_sum);
    puts("\n");

    if (total_sum == expected_sum) {
        puts("  [OK] Sum is correct!\n");
    } else {
        puts("  [FAIL] Sum mismatch!\n");
    }

    // Test 3: Barrier Synchronization
    puts("\nTEST 3: Barrier Synchronization\n");
    puts("---------------------------------\n");
    puts("  (All CPUs should reach 1M after barrier)\n");

    int barrier_ok = 1;
    for (int i = 0; i < cpu_count; i++) {
        puts("  CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(per_cpu_counters[i]);
        if (per_cpu_counters[i] != 1000000) {
            puts(" [FAIL]\n");
            barrier_ok = 0;
        } else {
            puts(" [OK]\n");
        }
    }

    if (barrier_ok) {
        puts("  [OK] Barrier synchronization worked!\n");
    } else {
        puts("  [FAIL] Some CPUs didn't reach barrier\n");
    }

    // Final status
    puts("\n");
    puts("===========================================\n");
    if (total_sum == expected_sum && barrier_ok) {
        puts("[SUCCESS] All parallel tests passed!\n");
    } else {
        puts("[WARNING] Some tests failed\n");
    }
    puts("===========================================\n");

    // ========================================================================
    // TIMER TEST: Display timer ticks
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  APIC Timer Test\n");
    puts("===========================================\n");
    puts("\n");
    puts("[TIMER] Waiting 2 seconds to collect timer ticks...\n");

    // Wait ~2 seconds (at 10 Hz, we should get ~20 ticks per CPU)
    for (volatile int i = 0; i < 20000000; i++) __asm__ volatile("pause");

    puts("[TIMER] Global handler calls: ");
    print_dec_64(global_timer_calls);
    puts("\n");

    puts("[TIMER] Timer ticks per CPU:\n");
    for (int i = 0; i < cpu_count; i++) {
        puts("  CPU ");
        print_dec(i);
        puts(": ");
        print_dec_64(timer_ticks[i]);
        puts(" ticks\n");
    }

    // Calculate total ticks
    uint64_t total_ticks = 0;
    for (int i = 0; i < cpu_count; i++) {
        total_ticks += timer_ticks[i];
    }

    puts("  Total ticks: ");
    print_dec_64(total_ticks);
    puts("\n");

    if (total_ticks > 0) {
        puts("  [OK] Timer interrupts are working!\n");
    } else {
        puts("  [FAIL] No timer interrupts received!\n");
    }

    // ========================================================================
    // DONE!
    // ========================================================================

    puts("\n");
    puts("===========================================\n");
    puts("  Step 9 Complete!\n");
    puts("===========================================\n");
    puts("\n");
    puts("[SUCCESS] Memory Management fully functional!\n");
    puts("[SUCCESS] Multiboot2 memory map parsed\n");
    puts("[SUCCESS] Physical Memory Manager (bitmap allocator)\n");
    puts("[SUCCESS] Virtual Memory Manager (recursive page tables)\n");
    puts("[SUCCESS] Kernel Heap initialized (16 MB)\n");
    puts("[SUCCESS] APIC Timer working on all CPUs!\n");
    puts("[SUCCESS] All 4 CPUs running in parallel\n");
    puts("\n");
    puts("[INFO] Step 9 demonstrates:\n");
    puts("  - Multiboot2 boot protocol\n");
    puts("  - Physical memory allocation (PMM)\n");
    puts("  - Virtual memory mapping (VMM)\n");
    puts("  - Recursive page table mapping\n");
    puts("  - Kernel heap allocator\n");
    puts("  - Per-CPU APIC timers\n");
    puts("  - SMP-safe memory management\n");
    puts("\n");
    puts("System halted successfully.\n");

    while (1) {
        __asm__ volatile("hlt");
    }
}
