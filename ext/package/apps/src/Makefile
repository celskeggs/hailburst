
GCC := gcc
MY_CFLAGS := $(CFLAGS) -Wall -Werror -pthread -lz -D_GNU_SOURCE -ggdb
SCRATCHDIR := "test-scratch/"

all: app

%.o: %.c *.h
	$(GCC) $(MY_CFLAGS) -c $< -o $@

app: app.o app_iotest.o app_rmap.o app_scrub.o bitbuf.o fakewire_link.o fakewire_exc.o rmap.o ringbuf.o radio.o comm.o cmd.o tlm.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

fakewire_link_test: fakewire_link_test.o test_common.o fakewire_link.o bitbuf.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

fakewire_exc_test: fakewire_exc_test.o test_common.o fakewire_exc.o fakewire_link.o bitbuf.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

clean:
	rm -f app fakewire_link_test fakewire_exc_test *.o

link_test: fakewire_link_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_link_test $(SCRATCHDIR)

exc_test: fakewire_exc_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_exc_test $(SCRATCHDIR)

test: link_test exc_test
