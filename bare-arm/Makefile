
all: kernel

GCC=arm-none-eabi-gcc
AS=arm-none-eabi-as
LD=arm-none-eabi-ld

# embedded artistry's libc
EALIBC_ROOT=../../ealibc
EALIBC_INC=$(EALIBC_ROOT)/include

ZLIB_ROOT=../../zlib-1.2.11

FREERTOS_ROOT=../../FreeRTOS/FreeRTOS/Source
FREERTOS_INC=$(FREERTOS_ROOT)/include
FREERTOS_ARCH_INC=$(FREERTOS_ROOT)/portable/GCC/ARM_CA9

APP_ROOT=../ext/package/apps/src

LIBGCC_LIBRARY_PATH=$(shell $(GCC) -print-libgcc-file-name)

COMMON_FLAGS=-ggdb -mcpu=cortex-a15 -mfpu=vfpv4 -mfloat-abi=softfp

ASFLAGS=$(COMMON_FLAGS) --fatal-warnings
CFLAGS=$(COMMON_FLAGS) -Wall -Wextra -Werror -nostdlib -nostdinc -nostartfiles -ffreestanding -std=gnu99 \
       -Wno-unknown-pragmas \
       -I$(FREERTOS_INC) -I$(FREERTOS_ARCH_INC) \
       -I$(EALIBC_INC) -I$(EALIBC_ROOT)/printf -I$(EALIBC_ROOT)/arch/arm/include -I$(ZLIB_ROOT) \
       -Iinclude \
       -D_BITSIZE=32 \
       -D__FREERTOS__

LOCAL_FILES=start stubs timer gic thread_freertos virtio
EALIBC_FILES=printf/printf src/assert/assert src/string/memset src/string/memcpy src/string/memmove src/crt/exit src/stdlib/rand
ZLIB_FILES=crc32
FREERTOS_FILES=tasks queue list portable/GCC/ARM_CA9/port portable/GCC/ARM_CA9/portASM portable/MemMang/heap_4
APP_FILES=spacecraft clock_freertos ringbuf cmd fakewire_codec fakewire_link_freertos fakewire_exc rmap tlm comm magnetometer heartbeat radio

freertos-build/%.o: $(FREERTOS_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

freertos-build/%.o: $(FREERTOS_ROOT)/%.S
	mkdir -p $$(dirname $@)
	$(AS) $(ASFLAGS) -o $@ -c $<

ealibc-build/%.o: $(EALIBC_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

zlib-build/%.o: $(ZLIB_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

app-build/%.o: $(APP_ROOT)/%.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

local-build/%.o: %.s
	mkdir -p $$(dirname $@)
	$(AS) $(ASFLAGS) -o $@ $<

local-build/%.o: %.c
	mkdir -p $$(dirname $@)
	$(GCC) $(CFLAGS) -o $@ -c $<

ALL_FILES:=$(foreach f,$(LOCAL_FILES),local-build/$(f).o)
ALL_FILES+=$(foreach f,$(FREERTOS_FILES),freertos-build/$(f).o)
ALL_FILES+=$(foreach f,$(EALIBC_FILES),ealibc-build/$(f).o)
ALL_FILES+=$(foreach f,$(ZLIB_FILES),zlib-build/$(f).o)
ALL_FILES+=$(foreach f,$(APP_FILES),app-build/$(f).o)

kernel: $(ALL_FILES)
	$(LD) -T link.ld -o $@ $^ $(LIBGCC_LIBRARY_PATH)

clean:
	rm -rf kernel local-build/ freertos-build/ ealibc-build/ zlib-build/ app-build/
