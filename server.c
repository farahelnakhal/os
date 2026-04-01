#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "simple.h"
#include "pipeline.h"

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_INPUT_SIZE 1024

#define COLOR_INFO "\033[0;36m"
#define COLOR_RECEIVED "\033[0;33m"
#define COLOR_EXECUTING "\033[0;35m"
#define COLOR_OUTPUT "\033[0;32m"
#define COLOR_ERROR "\033[0;31m"
#define COLOR_RESET "\033[0m"

void preprocess_input(char *input) {
    char buffer[4096];
    int i = 0, j = 0;

    while (input[i] != '\0') {
        if (input[i] == '2' && input[i+1] == '>' && (i == 0 || isspace((unsigned char)input[i-1]))) {
            buffer[j++] = ' ';
            buffer[j++] = '2';
            buffer[j++] = '>';
            buffer[j++] = ' ';
            i += 2;
        }
        else if (input[i] == '<' || input[i] == '>' || input[i] == '|') {
            buffer[j++] = ' ';
            buffer[j++] = input[i];
            buffer[j++] = ' ';
            i++;
        }
        else {
            buffer[j++] = input[i++];
        }
    }

    buffer[j] = '\0';
    strcpy(input, buffer);
}

char* execute_and_capture(char *command) {
    int stdout_pipe[2];
    int stderr_pipe[2];
    pid_t pid;
    char *output = NULL;
    int total_size = 0;
    
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        perror("pipe");
        return strdup("Error: Failed to create pipes\n");
    }

    pid = fork();
    
    if (pid < 0) {
        perror("fork");
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return strdup("Error: Failed to fork process\n");
    }
    
    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        preprocess_input(command);
        
        if (strchr(command, '|') != NULL) {
            execute_pipeline(command);
        } else {
            execute_simple_command(command);
        }
        
        exit(0);
    }
    
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
    waitpid(pid, NULL, 0);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    output = malloc(1);
    output[0] = '\0';
    
    while ((bytes_read = read(stdout_pipe[0], buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        char *new_output = realloc(output, total_size + bytes_read + 1);
        if (new_output == NULL) {
            free(output);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return strdup("Error: Memory allocation failed\n");
        }
        output = new_output;
        strcpy(output + total_size, buffer);
        total_size += bytes_read;
    }
    
    while ((bytes_read = read(stderr_pipe[0], buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        char *new_output = realloc(output, total_size + bytes_read + 1);
        if (new_output == NULL) {
            free(output);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return strdup("Error: Memory allocation failed\n");
        }
        output = new_output;
        strcpy(output + total_size, buffer);
        total_size += bytes_read;
    }
    
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    if (total_size == 0) {
        free(output);
        output = strdup("");
    }
    
    return output;
}

void handle_client(int client_socket) {
    char buffer[MAX_INPUT_SIZE];
    ssize_t bytes_received;
    
    printf("%s[INFO]%s Client connected.\n", COLOR_INFO, COLOR_RESET);
    fflush(stdout);
    
    while (1) {
        memset(buffer, 0, MAX_INPUT_SIZE);
        
        bytes_received = recv(client_socket, buffer, MAX_INPUT_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("%s[INFO]%s Client disconnected.\n", COLOR_INFO, COLOR_RESET);
            } else {
                perror("recv");
            }
            break;
        }
        
        buffer[strcspn(buffer, "\n")] = 0;
        
        if (strcmp(buffer, "exit") == 0) {
            printf("%s[RECEIVED]%s Received command: \"exit\" from client.\n", COLOR_RECEIVED, COLOR_RESET);
            printf("%s[INFO]%s Client requested to exit. Closing connection.\n", COLOR_INFO, COLOR_RESET);
            
            send(client_socket, "exit_ack", 8, 0);
            break;
        }
        
        if (strlen(buffer) == 0) {
            continue;
        }
        
        printf("%s[RECEIVED]%s Received command: \"%s\" from client.\n", COLOR_RECEIVED, COLOR_RESET, buffer);
        fflush(stdout);
        
        printf("%s[EXECUTING]%s Executing command: \"%s\"\n", COLOR_EXECUTING, COLOR_RESET, buffer);
        fflush(stdout);
        char *output = execute_and_capture(buffer);
        
        printf("%s[OUTPUT]%s Sending output to client:\n", COLOR_OUTPUT, COLOR_RESET);
        
        if (strlen(output) > 0) {
            printf("%s", output);
            if (output[strlen(output) - 1] != '\n') {
                printf("\n");
            }
        } else {
            printf("(no output)\n");
        }
        fflush(stdout);
        send(client_socket, output, strlen(output), 0);
        free(output);
    }
    
    close(client_socket);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_socket, 5) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("%s[INFO]%s Server started, waiting for client connections...\n", COLOR_INFO, COLOR_RESET);
    fflush(stdout);
    
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        handle_client(client_socket);
    }
    
    close(server_socket);
    return 0;
}