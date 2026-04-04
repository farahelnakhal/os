CC = gcc
CFLAGS = -Wall -Wextra -g

#targets
SHELL_TARGET = myshell
SERVER_TARGET = server

#object files
SHELL_OBJ = main.o pipeline.o simple.o
SERVER_OBJ = server.o pipeline.o simple.o

#build all targets
all: $(SHELL_TARGET) $(SERVER_TARGET)

#build myshell (Phase 1)
$(SHELL_TARGET): $(SHELL_OBJ)
	$(CC) $(CFLAGS) -o $(SHELL_TARGET) $(SHELL_OBJ)

#build server (Phase 2)
$(SERVER_TARGET): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJ)

#compile main.o for myshell
main.o: main.c pipeline.h simple.h
	$(CC) $(CFLAGS) -c main.c

#compile server.o for server
server.o: server.c pipeline.h simple.h
	$(CC) $(CFLAGS) -c server.c

#compile pipeline.o (shared between myshell and server)
pipeline.o: pipeline.c pipeline.h
	$(CC) $(CFLAGS) -c pipeline.c

#compile simple.o (shared between myshell and server)
simple.o: simple.c simple.h
	$(CC) $(CFLAGS) -c simple.c

#clean all compiled files
clean:
	rm -f $(SHELL_OBJ) $(SERVER_OBJ) $(SHELL_TARGET) $(SERVER_TARGET)

#clean only server files
clean-server:
	rm -f server.o $(SERVER_TARGET)

#clean only shell files
clean-shell:
	rm -f $(SHELL_OBJ) $(SHELL_TARGET)

.PHONY: all clean clean-server clean-shell
