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
#include <time.h>

#include "simple.h"
#include "pipeline.h"
#include "scheduler.h"


#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_INPUT_SIZE 1024

#define COLOR_INFO "\033[0;36m"
#define COLOR_RECEIVED "\033[0;33m"
#define COLOR_EXECUTING "\033[0;35m"
#define COLOR_OUTPUT "\033[0;32m"
#define COLOR_ERROR "\033[0;31m"
#define COLOR_RESET "\033[0m"

static int client_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER; //mutex to protect client_counter in multithreaded access

//struct with client socket descriptor, client uid  and client network address info
typedef struct {
    int socket;
    int client_id;
    struct sockaddr_in addr;
} client_info_t;

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

// static int is_command_not_found(const char *output) {
//     //checks shell output for "command not found" error (case variations)
//     return (strstr(output, "Command not found") != NULL || strstr(output, "command not found") != NULL);}

// static void extract_command_name(const char *command, char *dest, size_t dest_size) {
//     const char *start = command;
//     while (*start && isspace((unsigned char)*start)) start++; //skip leading whitespace
//     size_t i = 0;

//     // copy first token (command name) into dest
//     while (*start && !isspace((unsigned char)*start) && i < dest_size - 1)
//         dest[i++] = *start++;
//     dest[i] = '\0';
// }

void *handle_client(void *arg) {
    client_info_t *info = (client_info_t *)arg;

    //extract socket + id early since we free struct later
    int sock = info->socket;
    int client_id = info->client_id;

    char client_ip[INET_ADDRSTRLEN];
    int client_port = ntohs(info->addr.sin_port);

    //convert ip to human readable
    inet_ntop(AF_INET, &info->addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    //build a readable label for logging
    char label[64];
    snprintf(label, sizeof(label), "Client #%d - %s:%d",
             client_id, client_ip, client_port);

    free(info); //no longer needed after extracting fields

    printf("[%d]<<< client connected\n", client_id);
    fflush(stdout);

    char buffer[MAX_INPUT_SIZE];
    char *output = NULL;

    while (1) {
        memset(buffer, 0, MAX_INPUT_SIZE);

        //receive command from client
        ssize_t bytes_received = recv(sock, buffer, MAX_INPUT_SIZE - 1, 0);

        //handle disconnect or socket error
        if (bytes_received <= 0) {
            if (bytes_received == 0)
                printf("%s[INFO]%s %s disconnected.\n", COLOR_INFO, COLOR_RESET, label);
            else
                printf("%s[ERROR]%s %s recv error: %s\n", COLOR_ERROR, COLOR_RESET, label, strerror(errno));

            fflush(stdout);
            break;
        }

        //null terminate and strip newline characters
        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        //ignore empty input
        if (strlen(buffer) == 0) continue;

        //handle client exit request
        if (strcmp(buffer, "exit") == 0) {
            // printf("%s[RECEIVED]%s [%s] Received command: \"exit\"\n", COLOR_RECEIVED, COLOR_RESET, label);
            // printf("%s[INFO]%s [%s] Client requested disconnect. Closing connection.\n", COLOR_INFO, COLOR_RESET, label);
            printf("[%d] >>> %s\n", client_id, buffer);
            fflush(stdout);

            send(sock, "exit_ack", 8, 0);
            break;
        }

        //log received command
        // printf("%s[RECEIVED]%s [%s] Received command: \"%s\"\n", COLOR_RECEIVED, COLOR_RESET, label, buffer);

        //log execution step
        // printf("%s[EXECUTING]%s [%s] Executing command: \"%s\"\n", COLOR_EXECUTING, COLOR_RESET, label, buffer);
        printf("[%d] >>> %s\n", client_id, buffer);
        
        fflush(stdout);

        // check if command is demo program: "./demo N"
        if (strncmp(buffer, "./demo", 6) == 0) {

            int n;
            if (sscanf(buffer, "./demo %d", &n) != 1 || n <= 0) {
                char *err = "Invalid demo command. Usage: ./demo N\n";
                send(sock, err, strlen(err), 0);
                continue;
            }

            // create new task
            task_t *task = malloc(sizeof(task_t));
            if (!task) {
                perror("malloc task");
                continue;
            }

            task->task_id = rand(); // or global counter if you prefer
            task->client_id = client_id;
            strncpy(task->command, buffer, sizeof(task->command));
            task->burst_time = n;
            task->remaining_time = n;
            task->arrival_time = time(NULL);
            task->round = 0;
            task->sock = sock;
            task->pid = -1;

            // printf("%s[INFO]%s [%s] Created demo task with burst %d\n",COLOR_INFO, COLOR_RESET, label, n);

            // printf("(%d) --- created (%d)\n", client_id, n);
            printf("\033[0;34m(%d) --- created (%d)\033[0m\n", client_id, n);

            fflush(stdout);

            // send to scheduler queue
            scheduler_enqueue(task);
            continue;

        } else {

            // normal shell command → execute immediately
            printf("\033[0;34m(%d) --- created (-1)\033[0m\n", client_id);
            printf("\033[0;32m(%d) --- started (-1)\033[0m\n", client_id);
            fflush(stdout);

            output = execute_and_capture(buffer);

            size_t bytes = strlen(output);
            send(sock, output, bytes, 0);

            printf("[%d] <<< %zu bytes sent\n", client_id, bytes);
            //print ended in red
            printf("\033[0;31m(%d) --- ended (-1)\033[0m\n", client_id);
            // printf("(%d) --- ended (-1)\n", client_id);
            fflush(stdout);

            free(output);
            output = NULL;
        }
    }
    // remove all tasks belonging to this client
    scheduler_remove_client(client_id);
    
    //cleanup socket on exit
    shutdown(sock, SHUT_RDWR);
    close(sock);
    printf("%s[INFO]%s %s disconnected.\n", COLOR_INFO, COLOR_RESET, label);
    fflush(stdout);
    return NULL;
}

int main() {
    int server_socket;
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
    
    // printf("%s[INFO]%s Server started, waiting for client connections...\n", COLOR_INFO, COLOR_RESET);
    printf("-------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("-------------------------\n");
    fflush(stdout);

    // initialize scheduler system
    scheduler_init();

    // create scheduler thread
    pthread_t sched_tid;
    if (pthread_create(&sched_tid, NULL, scheduler_thread, NULL) != 0) {
        perror("pthread_create scheduler");
        exit(EXIT_FAILURE);
    }

    while (1) {
        //accept new client connection
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) { perror("accept"); continue; }

        //threadsafe client id generation
        pthread_mutex_lock(&counter_mutex);
        int new_id = ++client_counter;
        pthread_mutex_unlock(&counter_mutex);

        //allocate and populate client info struct
        client_info_t *info = malloc(sizeof(client_info_t));
        if (!info) { perror("malloc"); close(client_socket); continue; }

        info->socket = client_socket;
        info->client_id = new_id;
        info->addr = client_addr;

        pthread_t tid;

        //spawn detached thread for client handling
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create");
            free(info);
            close(client_socket);
            continue;
        }
        pthread_detach(tid); //auto cleanup thread on exit

    }

    close(server_socket);
    return 0;
}   