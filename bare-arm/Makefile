
all: kernel

KLIBC_ROOT=../../klibc
KLIBC_SRC=$(KLIBC_ROOT)/usr/klibc
KLIBC_INC=$(KLIBC_ROOT)/usr/include
KLIBC_ARCH_INC=$(KLIBC_INC)/arch/arm/
KLIBC_BITS_INC=$(KLIBC_INC)/bits32
KLIBC_VERSION_FILE=$(KLIBC_ROOT)/usr/klibc/version

LIBGCC_LOAD_PATH=/usr/lib/gcc/arm-none-eabi/7.3.1

ASFLAGS=-ggdb
CFLAGS=-ggdb -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding -std=gnu99 \
       -I$(KLIBC_INC) -I$(KLIBC_ARCH_INC) -I$(KLIBC_BITS_INC) -Iinclude \
       -D__KLIBC__=$(shell cut -d. -f1 $(KLIBC_VERSION_FILE)) \
       -D_BITSIZE=32

FUNCS=fputs lseek printf puts vfprintf vsnprintf \
	  stdio/fdopen stdio/fflush stdio/fseek stdio/fwrite \
	  __static_init exit \
      memcpy memrchr memset strlen zalloc

klibc-build/%.o: $(KLIBC_SRC)/%.c
	mkdir -p $$(dirname $@)
	arm-none-eabi-gcc $(CFLAGS) -o $@ -c $<

klibc-build/malloc.o: CFLAGS+=-Wno-unused-parameter

%.o: %.s
	arm-none-eabi-as $(ASFLAGS) -o $@ $<

%.o: %.c
	arm-none-eabi-gcc $(CFLAGS) -o $@ -c $<

kernel: start.o main.o stubs.o $(foreach func,$(FUNCS),klibc-build/$(func).o)
	arm-none-eabi-ld -T link.ld -L$(LIBGCC_LOAD_PATH) -o $@ $^ -lgcc

clean:
	rm -f kernel *.o
