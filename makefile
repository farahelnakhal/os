
# makefile for myshell project

CC = gcc
CFLAGS = -Wall -Wextra -std=c11

# Output executable name
TARGET = myshell

# Source files
SRCS = main.c simple.c

# Compile all
all:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Clean compiled files
clean:
	rm -f $(TARGET)