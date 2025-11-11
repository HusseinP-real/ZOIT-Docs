# TODO: make sure the rules for server client and markdown filled!
CC := gcc
CFLAGS := -fsanitize=address -g -Wall -Wextra -std=c11 -Ilibs -Ipthread

all: server client

server: server.o markdown.o
	$(CC) $(CFLAGS) -o server server.o markdown.o

server.o: source/server.c libs/markdown.h
	$(CC) $(CFLAGS) -c source/server.c -o server.o

client: client.o markdown.o
	$(CC) $(CFLAGS) -o client client.o markdown.o

client.o: source/client.c libs/markdown.h
	$(CC) $(CFLAGS) -c source/client.c -o client.o

markdown.o: source/markdown.c libs/markdown.h
	$(CC) $(CFLAGS) -c source/markdown.c -o markdown.o

clean:
	rm -f *.o server client
