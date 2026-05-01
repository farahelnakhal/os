CC = gcc
CFLAGS = -Wall -Wextra -g
TARGETS = myshell server client

all: myshell server client demo

myshell: main.o simple.o pipeline.o
	$(CC) $(CFLAGS) -o myshell main.o simple.o pipeline.o

server: server.o scheduler.o simple.o pipeline.o
	$(CC) $(CFLAGS) -o server server.o scheduler.o simple.o pipeline.o -lpthread

client: client.o
	$(CC) $(CFLAGS) -o client client.o

main.o: main.c simple.h pipeline.h
	$(CC) $(CFLAGS) -c main.c

server.o: server.c simple.h pipeline.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

simple.o: simple.c simple.h
	$(CC) $(CFLAGS) -c simple.c

pipeline.o: pipeline.c pipeline.h
	$(CC) $(CFLAGS) -c pipeline.c
	
scheduler.o: scheduler.c scheduler.h
	$(CC) $(CFLAGS) -c scheduler.c

demo: demo.c
	$(CC) $(CFLAGS) -o demo demo.c
	
clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean