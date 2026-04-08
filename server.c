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

    //normalize spacing around operators
    while (input[i] != '\0') {
        //handle 2> redirection specifically
        if (input[i] == '2' && input[i+1] == '>' && (i == 0 || isspace((unsigned char)input[i-1]))) {
            buffer[j++] = ' ';
            buffer[j++] = '2';
            buffer[j++] = '>';
            buffer[j++] = ' ';
            i += 2;
        }
        //handle other operators
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
    strcpy(input, buffer);//copy processed string back
}

char* execute_and_capture(char *command) {
    int stdout_pipe[2];
    int stderr_pipe[2];
    pid_t pid;
    char *output = NULL;
    int total_size = 0;
    
    //create pipes for stdout and stderr
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        perror("pipe");
        return strdup("Error: Failed to create pipes\n");
    }

    pid = fork();
    
    //handle fork failure
    if (pid < 0) {
        perror("fork");
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return strdup("Error: Failed to fork process\n");
    }
    
    //child process executes command
    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);//redirect stdout
        dup2(stderr_pipe[1], STDERR_FILENO);//redirect stderr
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        char command_copy[MAX_INPUT_SIZE];
        strcpy(command_copy, command);
        preprocess_input(command_copy);//prepare command
        
        //decide between pipeline or simple execution
        if (strchr(command_copy, '|') != NULL) {
            execute_pipeline(command_copy);
        } else {
            execute_simple_command(command_copy);
        }
        
        exit(0);//terminate child
    }
    
    //parent process reads output
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    waitpid(pid, NULL, 0);//wait for child
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    output = malloc(1);//initialize empty output
    if (output == NULL) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        return strdup("Error: Memory allocation failed\n");
    }
    output[0] = '\0';
    
    //read stdout and filter out null bytes
    while ((bytes_read = read(stdout_pipe[0], buffer, BUFFER_SIZE - 1)) > 0) {
        char clean_buffer[BUFFER_SIZE];
        int clean_len = 0;
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] != '\0') {
                clean_buffer[clean_len++] = buffer[i];
            }
        }
        clean_buffer[clean_len] = '\0';
        
        //append cleaned data to output
        if (clean_len > 0) {
            char *new_output = realloc(output, total_size + clean_len + 1);
            if (new_output == NULL) {
                free(output);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                return strdup("Error: Memory allocation failed\n");
            }
            output = new_output;
            strcpy(output + total_size, clean_buffer);
            total_size += clean_len;
        }
    }
    
    //read stderr and filter out null bytes
    while ((bytes_read = read(stderr_pipe[0], buffer, BUFFER_SIZE - 1)) > 0) {
        char clean_buffer[BUFFER_SIZE];
        int clean_len = 0;
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] != '\0') {
                clean_buffer[clean_len++] = buffer[i];
            }
        }
        clean_buffer[clean_len] = '\0';
        
        //append stderr to output
        if (clean_len > 0) {
            char *new_output = realloc(output, total_size + clean_len + 1);
            if (new_output == NULL) {
                free(output);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                return strdup("Error: Memory allocation failed\n");
            }
            output = new_output;
            strcpy(output + total_size, clean_buffer);
            total_size += clean_len;
        }
    }
    
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    //handle empty output
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
        memset(buffer, 0, MAX_INPUT_SIZE);//clear buffer
        
        bytes_received = recv(client_socket, buffer, MAX_INPUT_SIZE - 1, 0);
        
        //handle disconnect or error
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("%s[INFO]%s Client disconnected.\n", COLOR_INFO, COLOR_RESET);
                fflush(stdout);
            } else {
                perror("recv");
            }
            break;
        }
        
        //null terminate and strip newline
        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        
        //skip empty input
        if (strlen(buffer) == 0) {
            continue;
        }
        
        //handle exit command
        if (strcmp(buffer, "exit") == 0) {
            printf("%s[RECEIVED]%s Received command: \"exit\" from client.\n", COLOR_RECEIVED, COLOR_RESET);
            fflush(stdout);
            printf("%s[INFO]%s Client requested to exit. Closing connection.\n", COLOR_INFO, COLOR_RESET);
            fflush(stdout);
            
            send(client_socket, "exit_ack", 8, 0);
            break;
        }
        
        //log received command
        printf("%s[RECEIVED]%s Received command: \"%s\" from client.\n", COLOR_RECEIVED, COLOR_RESET, buffer);
        fflush(stdout);
        
        //log execution start
        printf("%s[EXECUTING]%s Executing command: \"%s\"\n", COLOR_EXECUTING, COLOR_RESET, buffer);
        fflush(stdout);
        
        //execute command
        char *output = execute_and_capture(buffer);
        
        //log output
        printf("%s[OUTPUT]%s Sending output to client:\n", COLOR_OUTPUT, COLOR_RESET);
        fflush(stdout);
        
        //print output locally
        if (strlen(output) > 0) {
            printf("%s", output);
            if (output[strlen(output) - 1] != '\n') {
                printf("\n");
            }
        } else {
            printf("(no output)\n");
        }
        fflush(stdout);
        
        //send output back to client
        send(client_socket, output, strlen(output), 0);
        free(output);
    }
    
    shutdown(client_socket, SHUT_RDWR);//close connection cleanly   
    close(client_socket);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    //create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    //allow address reuse
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    //bind socket to port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    //start listening
    if (listen(server_socket, 5) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("%s[INFO]%s Server started, waiting for client connections\n", COLOR_INFO, COLOR_RESET);
    fflush(stdout);
    
    //accept loop
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        handle_client(client_socket);//handle one client
    }
    
    close(server_socket);
    return 0;
}