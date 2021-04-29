
GCC := gcc
MY_CFLAGS := $(CFLAGS) -Wall -Werror -pthread
SCRATCHDIR := "test-scratch/"

app: app.c app_scrub.c app_iotest.c bitbuf.c fakewire.c
	$(GCC) $(MY_CFLAGS) $^ -o $@

clean:
	rm -f app fakewire_test

fakewire_test: fakewire_test.c fakewire.c bitbuf.c
	$(GCC) $(MY_CFLAGS) $^ -o $@

test: fakewire_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_test $(SCRATCHDIR)
