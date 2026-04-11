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
#include <pthread.h>

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

// structure for thread data 
typedef struct {
    int socket;
    int client_id;
    struct sockaddr_in address;
} client_info;

// client counter for multiple clients
int client_counter = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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

// thread function to handle 1 client
void* handle_client(void *arg) {
    client_info *info = (client_info*)arg;

    int client_socket = info->socket;
    int client_id = info->client_id;
    char *ip = inet_ntoa(info->address.sin_addr);
    int port = ntohs(info->address.sin_port);

    // adding thread id 
    unsigned long thread_id = (unsigned long)pthread_self();

    // free allocated memory
    free(arg);

    char buffer[MAX_INPUT_SIZE];
    ssize_t bytes_received;

    printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%lu.\n", client_id, ip, port, thread_id);

    while (1) {
        // clear buffer for next command
        memset(buffer, 0, MAX_INPUT_SIZE);

        // receive command from the client
        bytes_received = recv(client_socket, buffer, MAX_INPUT_SIZE - 1, 0);

        // handle client disconnect or error
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("[INFO] Client disconnected.\n");
            } else {
                perror("recv error");
            }
            break;
        }

        // remove newline character from buffer
        buffer[strcspn(buffer, "\n")] = 0;

        // handle exit command
        if (strcmp(buffer, "exit") == 0) {
            printf("%s[RECEIVED]%s Received command: \"exit\" from client.\n", COLOR_RECEIVED, COLOR_RESET);
            fflush(stdout);
            printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. Closing connection.\n", client_id, ip, port);            
            fflush(stdout);
            
            send(client_socket, "exit_ack", 8, 0);
            break;
        }

        // skip empty inputs
        if (strlen(buffer) == 0) continue;

        // log the received command
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n", client_id, ip, port, buffer);

        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n", client_id, ip, port, buffer);

        // execute command and capture output
        char *output = execute_and_capture(buffer);

        // log and display output
        if (strstr(output, "not found") != NULL) {
            printf("[ERROR] [Client #%d - %s:%d] Command not found: \"%s\"\n", client_id, ip, port, buffer);
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n%s\n", client_id, ip, port, output);
        } else {
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n", client_id, ip, port, output);
        }

        // send output back to client
        if (send(client_socket, output, strlen(output), 0) < 0) {
            perror("send error");
        }

        // free allocated output memory
        free(output);
    }
    printf("[INFO] Client #%d disconnected.\n", client_id);

    // close the client connection
    close(client_socket);
    free(info);
    return NULL;
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
    
    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);
    
    //accept loop
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            perror("accept");
            continue;
        }
        
        pthread_t tid;

        // allocate memory for socket
        client_info *info = malloc(sizeof(client_info));
        info->socket = client_socket;
        info->address = client_addr;

        // increment client number
        pthread_mutex_lock(&lock);
        client_counter++;
        info->client_id = client_counter;
        pthread_mutex_unlock(&lock);

        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create failed");
            close(client_socket);
            free(info);
            continue;
        }

        // detach thread so it cleans itself
        pthread_detach(tid);
    }
    
    close(server_socket);
    return 0;
}