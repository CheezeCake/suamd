CC=gcc

all: suamd.c
	$(CC) -std=c89 -W -Wall -pedantic suamd.c -o suamd -ludev
