# Makefile for myshell project
CC = gcc
CFLAGS = -Wall -Wextra -g

TARGET = myshell
SERVER_TARGET = server
CLIENT_TARGET = client

OBJ = main.o pipeline.o simple.o
SERVER_OBJ = server.o pipeline.o simple.o
CLIENT_OBJ = client.o

all: $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

$(SERVER_TARGET): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJ)

$(CLIENT_TARGET): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_OBJ)

main.o: main.c pipeline.h simple.h
	$(CC) $(CFLAGS) -c main.c

server.o: server.c pipeline.h simple.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

pipeline.o: pipeline.c pipeline.h
	$(CC) $(CFLAGS) -c pipeline.c

simple.o: simple.c simple.h
	$(CC) $(CFLAGS) -c simple.c

clean:
	rm -f $(OBJ) $(SERVER_OBJ) $(CLIENT_OBJ) $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)
