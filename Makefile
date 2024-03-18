CC = gcc
CFLAGS = -std=c11 -pedantic -pthread

all: crawler

crawler: crawler.c
	$(CC) $(CFLAGS) crawler.c -o crawler

clean:
	rm -f crawler

run: crawler
	./crawler