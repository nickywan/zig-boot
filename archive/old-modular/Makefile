# Compiler and tools
CC := gcc
LD := ld
AS := as

# Directories
BOOT_DIR := boot
KERNEL_DIR := kernel
INCLUDE_DIR := include

# Compiler flags
CFLAGS := -m64 -ffreestanding -nostdlib -nostdinc \
          -mno-red-zone -fno-exceptions -fno-asynchronous-unwind-tables \
          -Wall -Wextra -I$(INCLUDE_DIR) \
          -mcmodel=large -mno-sse -mno-sse2

ASFLAGS := --64

LDFLAGS := -n -T linker.ld -nostdlib

# Source files
BOOT_ASM := $(BOOT_DIR)/boot.S $(BOOT_DIR)/trampoline.S
KERNEL_C := $(KERNEL_DIR)/main.c $(KERNEL_DIR)/serial.c $(KERNEL_DIR)/sync.c \
            $(KERNEL_DIR)/acpi.c $(KERNEL_DIR)/smp.c

# Object files
BOOT_OBJ := $(BOOT_ASM:.S=.o)
KERNEL_OBJ := $(KERNEL_C:.c=.o)
ALL_OBJ := $(BOOT_OBJ) $(KERNEL_OBJ)

# Output
TARGET := kernel.elf
BINARY := kernel.bin

# Default target
all: $(BINARY)

# Link kernel
$(TARGET): $(ALL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJ)

# Compile C files
$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM files
$(BOOT_DIR)/%.o: $(BOOT_DIR)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

# Create flat binary (optional)
$(BINARY): $(TARGET)
	objcopy -O binary $< $@

# Run in QEMU (TCG)
run-tcg: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -serial stdio -smp 4 \
	    -no-reboot -no-shutdown

# Run in QEMU (KVM)
run-kvm: $(TARGET)
	qemu-system-x86_64 -enable-kvm -kernel $(TARGET) -serial stdio -smp 4 \
	    -no-reboot -no-shutdown

# Run with debug
debug: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -serial stdio -smp 4 \
	    -d int,cpu_reset -D qemu.log -no-reboot -no-shutdown

# Clean
clean:
	rm -f $(ALL_OBJ) $(TARGET) $(BINARY) qemu.log

# Dependencies
$(ALL_OBJ): $(wildcard $(INCLUDE_DIR)/*.h)

.PHONY: all clean run-tcg run-kvm debug
