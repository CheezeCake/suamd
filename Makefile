CC=gcc
CFLAGS=-std=c89 -W -Wall -pedantic
LDFLAGS=-ludev
PREFIX=/usr/local

all: suamd


suamd: suamd.o
	$(CC) suamd.o -o suamd $(LDFLAGS)

suamd.o: suamd.c
	$(CC) $(CFLAGS) -c suamd.c -o suamd.o

clean:
	rm -f suamd.o

mrproper: clean
	rm -f suamd

install: suamd
	install -m 755 suamd $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/suamd
