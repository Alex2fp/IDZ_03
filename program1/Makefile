CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -pthread

all: program1

program1: main.c
	$(CC) $(CFLAGS) main.c -o program1 -lrt

clean:
	rm -f program1
