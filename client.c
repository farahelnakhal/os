#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define PORT 8080 
#define BUFFER_SIZE 4096 

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char input[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    // create a tcp socket so this client can talk to the shell server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // fill in the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // convert localhost text into binary form expected by connect
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // open the connection to the remote shell process
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    while (1) {
        // print a shell-style prompt before reading each command
        printf("$ ");
        fflush(stdout);

        // read one command line from stdin
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        // strip trailing newline so the server receives only the command text
        input[strcspn(input, "\n")] = 0;

        // send the typed command to the shell server
        send(sock, input, strlen(input), 0);

        // stop locally after telling the server we want to exit
        if (strcmp(input, "exit") == 0) {
            break;
        }

        // clear the receive buffer, then wait for command output
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0) {
            // server closed connection or an error happened, so end
            printf("Disconnected from server.\n");
            break;
        }

        // print exactly what the server returned for this command
        printf("%s", buffer);

        // add a newline only if server output did not already include one
        if (bytes_received > 0 && buffer[bytes_received - 1] != '\n') {
            printf("\n");
        }
    }

    // release socket resources before exiting the client
    close(sock);
    return 0;
}