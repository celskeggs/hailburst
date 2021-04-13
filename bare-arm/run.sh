#!/bin/bash
set -e -u

arm-none-eabi-as -ggdb -o start.o start.s
arm-none-eabi-gcc -ggdb -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding -std=gnu99 -c main.c
arm-none-eabi-ld -T link.ld -o kernel start.o main.o
# qemu-system-arm -s -M integratorcp -m 128 -kernel kernel -monitor stdio -icount shift=10
#../../qemu/build/qemu-system-arm -S -s -M integratorcp -m 128 -kernel kernel -monitor stdio -parallel none -icount shift=10,sleep=off -vga none
../../qemu/build/qemu-system-arm -S -s -M integratorcp -m 1 -kernel kernel -monitor stdio -parallel none -icount shift=10,sleep=off -vga none
