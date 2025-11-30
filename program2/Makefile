CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -pthread

all: talker2

talker2: talker2.c
	$(CC) $(CFLAGS) talker2.c -o talker2 -lrt

clean:
	rm -f talker2
