#!/bin/bash
# set -e (Removed to allow ISO failure)

# Paths
OUT_DIR=..
ISO_DIR="$OUT_DIR/isodir"
BOOT_DIR="$ISO_DIR/boot"
GRUB_DIR="$BOOT_DIR/grub"

# Clean old files
rm -f $OUT_DIR/kernel.bin $OUT_DIR/kernel.iso
mkdir -p $GRUB_DIR

# Compile kernel
nasm -f elf32 start.asm -o start.o
gcc -m32 -ffreestanding -fno-stack-protector -c kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-stack-protector -c paging.c -o paging.o
gcc -m32 -ffreestanding -fno-stack-protector -c pmm.c -o pmm.o
gcc -m32 -ffreestanding -fno-stack-protector -c swap.c -o swap.o
ld -m elf_i386 -T link.ld -o $OUT_DIR/kernel.bin start.o kernel.o paging.o pmm.o swap.o

# Copy to ISO structure
cp $OUT_DIR/kernel.bin $BOOT_DIR/kernel.bin

# Create ISO
# Create ISO (Optional)
if command -v grub-mkrescue &> /dev/null && command -v mformat &> /dev/null; then
    grub-mkrescue -o $OUT_DIR/kernel.iso $ISO_DIR
    echo "✅ ISO created: $OUT_DIR/kernel.iso"
else
    echo "⚠️  Skipping ISO creation (missing grub-mkrescue or mtools)."
    echo "✅ Kernel binary created: $OUT_DIR/kernel.bin"
fi
