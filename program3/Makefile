CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -pthread

all: talker3 observer3

talker3: talker3.c
	$(CC) $(CFLAGS) talker3.c -o talker3 -lrt

observer3: observer3.c
	$(CC) $(CFLAGS) observer3.c -o observer3 -lrt

clean:
	rm -f talker3 observer3
