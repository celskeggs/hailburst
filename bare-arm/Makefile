
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

APP_ROOT=../ext/package/apps/src

LIBGCC_LIBRARY_PATH=$(shell $(GCC) -print-libgcc-file-name)

COMMON_FLAGS=-ggdb -mcpu=cortex-a15 -mfpu=vfpv4 -mfloat-abi=softfp

ASFLAGS=$(COMMON_FLAGS) --fatal-warnings
CFLAGS=$(COMMON_FLAGS) -Wall -Wextra -Werror -nostdlib -nostartfiles -ffreestanding -std=gnu99 \
       -I$(KLIBC_INC) -I$(KLIBC_ARCH_INC) -I$(KLIBC_BITS_INC) \
       -I$(FREERTOS_INC) -I$(FREERTOS_ARCH_INC) \
       -Iinclude \
       -D__KLIBC__=$(shell cut -d. -f1 $(KLIBC_VERSION_FILE)) \
       -D_BITSIZE=32 \
       -D__FREERTOS__

LOCAL_FILES=start stubs timer gic thread_freertos main virtio
KLIBC_FILES=vsnprintf memcpy memrchr memset memmove strlen zlib/crc32 lrand48 jrand48 zalloc
FREERTOS_FILES=tasks queue list portable/GCC/ARM_CA9/port portable/GCC/ARM_CA9/portASM portable/MemMang/heap_1
# APP_FILES=spacecraft clock_freertos ringbuf cmd fakewire_codec fakewire_link fakewire_exc rmap tlm comm magnetometer heartbeat radio

klibc-build/%.o: $(KLIBC_SRC)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

freertos-build/%.o: $(FREERTOS_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

freertos-build/%.o: $(FREERTOS_ROOT)/%.S
	mkdir -p $$(dirname $@)
	$(AS) $(ASFLAGS) -o $@ -c $<

app-build/%.o: $(APP_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

local-build/%.o: %.s
	mkdir -p $$(dirname $@)
	$(AS) $(ASFLAGS) -o $@ $<

local-build/%.o: %.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

kernel: $(foreach f,$(LOCAL_FILES),local-build/$(f).o) $(foreach f,$(KLIBC_FILES),klibc-build/$(f).o) $(foreach f,$(FREERTOS_FILES),freertos-build/$(f).o) $(foreach f,$(APP_FILES),app-build/$(f).o)
	$(LD) -T link.ld -o $@ $^ $(LIBGCC_LIBRARY_PATH)

clean:
	rm -rf kernel local-build/ klibc-build/ freertos-build/ app-build/
