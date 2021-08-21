
GCC := gcc
MY_CFLAGS := $(CFLAGS) -Wall -Werror -pthread -lz -D_GNU_SOURCE -ggdb
SCRATCHDIR := "test-scratch/"

all: app

%.o: %.c *.h
	$(GCC) $(MY_CFLAGS) -c $< -o $@

app: spacecraft.o fakewire_codec.o fakewire_link.o fakewire_exc.o rmap.o ringbuf.o radio.o comm.o cmd.o tlm.o clock_posix.o magnetometer.o heartbeat.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

fakewire_exc_test: fakewire_exc_test.o test_common.o fakewire_exc.o fakewire_link.o fakewire_codec.o clock_stub.o ringbuf.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

clean:
	rm -f app fakewire_link_test fakewire_exc_test *.o
	rm -rf test_scratch/ testdir/

exc_test: fakewire_exc_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_exc_test $(SCRATCHDIR)

test: exc_test
