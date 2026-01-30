#!/bin/bash
# run.sh - Helper script to launch the kernel in QEMU

# Define paths (relative to src directory)
ISO_PATH="../kernel.iso"
BIN_PATH="../kernel.bin"

echo "Looking for kernel at: $BIN_PATH"
ls -l $BIN_PATH 2>/dev/null || echo "File not found by ls"

# Check if QEMU is installed
if ! command -v qemu-system-i386 &> /dev/null; then
    echo "Error: qemu-system-i386 could not be found."
    echo "Please install it using: sudo apt install qemu-system-x86"
    exit 1
fi

# Prefer booting the ISO if it exists (Test full bootloader flow)
if [ -f "$ISO_PATH" ]; then
    echo "🚀 Booting $ISO_PATH..."
    qemu-system-i386 -cdrom "$ISO_PATH" -m 128M

elif [ -f "$BIN_PATH" ]; then
    # Fallback to direct kernel boot (Faster, skips GRUB, good for quick tests)
    echo "⚠️  ISO not found. Booting direct kernel binary $BIN_PATH..."
    qemu-system-i386 -kernel "$BIN_PATH" -m 128M -no-reboot -d int,guest_errors

else
    echo "❌ No kernel found! Please run ./build.sh first."
    exit 1
fi
