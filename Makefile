CC=gcc
CFLAGS=-O2 -Wall

PROGS=\
lz4s\
lzss\
split\

all: $(PROGS:%=bin/%)

bin/%: src/%.c | bin
	$(CC) -o $@ $(CFLAGS) $<

bin:
	mkdir -p bin

clean:
	rm -f $(PROGS:%=bin/%)
	rmdir bin

