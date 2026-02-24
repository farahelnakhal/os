CC = gcc
CFLAGS = -Wall -Wextra -g

TARGET = myshell

OBJ = main.o pipeline.o simple.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

main.o: main.c pipeline.h simple.h
	$(CC) $(CFLAGS) -c main.c

pipeline.o: pipeline.c pipeline.h
	$(CC) $(CFLAGS) -c pipeline.c

simple.o: simple.c simple.h
	$(CC) $(CFLAGS) -c simple.c

clean:
	rm -f $(OBJ) $(TARGET)
