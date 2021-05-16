
GCC := gcc
MY_CFLAGS := $(CFLAGS) -Wall -Werror -pthread -D_GNU_SOURCE -ggdb
SCRATCHDIR := "test-scratch/"

app: app.c app_scrub.c app_iotest.c bitbuf.c fakewire_link.c fakewire_exc.c
	$(GCC) $(MY_CFLAGS) $^ -o $@

clean:
	rm -f app fakewire_link_test fakewire_exc_test *.o

%.o: %.c *.h
	$(GCC) $(MY_CFLAGS) -c $< -o $@

fakewire_link_test: fakewire_link_test.o test_common.o fakewire_link.o bitbuf.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

fakewire_exc_test: fakewire_exc_test.o test_common.o fakewire_exc.o fakewire_link.o bitbuf.o
	$(GCC) $(MY_CFLAGS) $^ -o $@

link_test: fakewire_link_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_link_test $(SCRATCHDIR)

exc_test: fakewire_exc_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_exc_test $(SCRATCHDIR)

test: link_test exc_test
