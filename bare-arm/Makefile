
all: kernel

GCC=arm-none-eabi-gcc
AS=arm-none-eabi-as
LD=arm-none-eabi-ld

KLIBC_ROOT=../../klibc
KLIBC_SRC=$(KLIBC_ROOT)/usr/klibc
KLIBC_INC=$(KLIBC_ROOT)/usr/include
KLIBC_ARCH_INC=$(KLIBC_INC)/arch/arm/
KLIBC_BITS_INC=$(KLIBC_INC)/bits32
KLIBC_VERSION_FILE=$(KLIBC_ROOT)/usr/klibc/version

FREERTOS_ROOT=../../FreeRTOS/FreeRTOS/Source
FREERTOS_INC=$(FREERTOS_ROOT)/include
FREERTOS_ARCH_INC=$(FREERTOS_ROOT)/portable/GCC/ARM_CA9

LIBGCC_LIBRARY_PATH=$(shell $(GCC) -print-libgcc-file-name)

COMMON_FLAGS=-ggdb -mcpu=cortex-a15 -mfpu=vfpv4 -mfloat-abi=softfp

ASFLAGS=$(COMMON_FLAGS) --fatal-warnings
CFLAGS=$(COMMON_FLAGS) -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding -std=gnu99 \
       -I$(KLIBC_INC) -I$(KLIBC_ARCH_INC) -I$(KLIBC_BITS_INC) \
       -I$(FREERTOS_INC) -I$(FREERTOS_ARCH_INC) \
       -Iinclude \
       -D__KLIBC__=$(shell cut -d. -f1 $(KLIBC_VERSION_FILE)) \
       -D_BITSIZE=32

KLIBC_FILES=fputs lseek printf puts vfprintf vsnprintf \
	  stdio/fdopen stdio/fflush stdio/fseek stdio/fwrite \
	  __static_init assert exit \
      memcpy memrchr memset strlen zalloc

FREERTOS_FILES=tasks queue list portable/GCC/ARM_CA9/port portable/GCC/ARM_CA9/portASM portable/MemMang/heap_1

klibc-build/%.o: $(KLIBC_SRC)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

klibc-build/malloc.o: CFLAGS+=-Wno-unused-parameter

freertos-build/%.o: $(FREERTOS_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

freertos-build/%.o: $(FREERTOS_ROOT)/%.S
	mkdir -p $$(dirname $@)
	$(AS) $(ASFLAGS) -o $@ -c $<

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(GCC) $(CFLAGS) -o $@ -c $<

kernel: start.o main.o stubs.o timer.o gic.o $(foreach f,$(KLIBC_FILES),klibc-build/$(f).o) $(foreach f,$(FREERTOS_FILES),freertos-build/$(f).o)
	$(LD) -T link.ld -o $@ $^ $(LIBGCC_LIBRARY_PATH)

clean:
	rm -f kernel *.o
	rm -rf klibc-build/ freertos-build/

