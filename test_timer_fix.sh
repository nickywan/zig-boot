#!/bin/bash

# Test script for AP timer fixes
# This script builds the kernel and runs it in QEMU to verify timer functionality

set -e

echo "=========================================="
echo "  AP Timer Fix - Test Script"
echo "=========================================="
echo

# Clean and rebuild
echo "[1/4] Cleaning build artifacts..."
make -f Makefile.step9 clean

echo "[2/4] Building kernel..."
make -f Makefile.step9

echo "[3/4] Creating bootable ISO..."
make -f Makefile.step9 iso

echo "[4/4] Running kernel in QEMU..."
echo
echo "Expected output:"
echo "  - All 4 CPUs should boot successfully"
echo "  - Timer ticks should be shown for ALL CPUs (not just CPU 0)"
echo "  - System should NOT hang when APs initialize timers"
echo
echo "Press Ctrl+C to stop QEMU"
echo
echo "=========================================="
echo

# Run with serial output, 4 CPUs, 256MB RAM
qemu-system-x86_64 \
    -cdrom boot_step9.iso \
    -serial stdio \
    -display none \
    -m 256M \
    -smp 4 \
    -no-reboot \
    -no-shutdown

echo
echo "Test completed!"
