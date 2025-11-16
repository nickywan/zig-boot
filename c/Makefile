# Bare-Metal x86-64 Kernel - Main Makefile
# Wraps Makefile.step9 for simplified usage

.PHONY: all clean iso run run-tcg debug help

# Default target
all:
	@$(MAKE) -f Makefile.step9

# Clean build artifacts
clean:
	@$(MAKE) -f Makefile.step9 clean

# Build bootable ISO
iso:
	@$(MAKE) -f Makefile.step9 iso

# Run in QEMU (with KVM if available) - VGA output enabled
run: iso
	@echo "Running kernel in QEMU (VGA enabled)..."
	qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -m 256M -smp 4

# Run in pure TCG mode (software emulation) - VGA output enabled
run-tcg: iso
	@echo "Running kernel in TCG mode (VGA enabled)..."
	qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -m 256M -smp 4 -accel tcg

# Run with debug output - VGA enabled
debug: iso
	@echo "Running kernel in debug mode (VGA enabled)..."
	qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio \
		-m 256M -smp 4 -d cpu_reset,guest_errors -no-reboot

# Quick test (3 seconds) - VGA enabled
test: iso
	@echo "Quick test (3 seconds, VGA enabled)..."
	@timeout 3 qemu-system-x86_64 -cdrom boot_step9.iso -serial stdio -m 256M -smp 4 -accel tcg || true

# Help target
help:
	@echo "Bare-Metal x86-64 Kernel - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all       - Compile kernel (default)"
	@echo "  iso       - Create bootable ISO"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Run in QEMU (with KVM if available)"
	@echo "  run-tcg   - Run in pure software emulation"
	@echo "  debug     - Run with debug output"
	@echo "  test      - Quick 3-second test"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make          # Compile kernel"
	@echo "  make iso      # Build bootable ISO"
	@echo "  make run      # Compile, build ISO, and run"
	@echo "  make run-tcg  # Run in TCG mode"
