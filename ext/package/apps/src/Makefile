
GCC := gcc
CFLAGS := -Wall -Werror -pthread
SCRATCHDIR := "test-scratch/"

app: app.c bitbuf.c fakewire.c
	$(GCC) $(CFLAGS) $^ -o $@

clean:
	rm -f app fakewire_test

fakewire_test: fakewire_test.c fakewire.c bitbuf.c
	$(GCC) $(CFLAGS) $^ -o $@

test: fakewire_test
	rm -rf $(SCRATCHDIR)
	mkdir $(SCRATCHDIR)
	./fakewire_test $(SCRATCHDIR)
