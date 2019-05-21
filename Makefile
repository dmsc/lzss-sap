CC=gcc
CFLAGS=-O2 -Wall

PROGS=\
lzss\
rle\
split\

all: $(PROGS:%=bin/%)

bin/%: src/%.c | bin
	$(CC) -o $@ $(CFLAGS) $<

bin:
	mkdir -p bin

clean:
	rm -f $(PROGS:%=bin/%)
	rmdir bin

