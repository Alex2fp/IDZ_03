CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -pthread

all: talker4 observer4

talker4: talker4.c
	$(CC) $(CFLAGS) talker4.c -o talker4 -lrt

observer4: observer4.c
	$(CC) $(CFLAGS) observer4.c -o observer4 -lrt

clean:
	rm -f talker4 observer4
