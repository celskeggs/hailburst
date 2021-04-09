#!/bin/bash
set -e -u

arm-none-eabi-as -o start.o start.s
arm-none-eabi-gcc -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding -std=gnu99 -c main.c
arm-none-eabi-ld -T link.ld -o kernel start.o main.o
qemu-system-arm -M integratorcp -m 128 -kernel kernel -serial mon:stdio
