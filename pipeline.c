#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include "pipeline.h"

#define MAX_CMDS 20
#define MAX_ARGS 50

void trim_whitespace(char *str) {
    int start = 0;
    while (isspace((unsigned char)str[start])) start++;

    int end = strlen(str) - 1;
    while (end >= start && isspace((unsigned char)str[end])) end--;

    int i = 0;
    while (start <= end) {
        str[i++] = str[start++];
    }
    str[i] = '\0';
}

void execute_single_pipe_cmd(char *cmd_str, int is_pipe_sequence) {
    char *args[MAX_ARGS];
    int arg_count = 0;

    char *input_file = NULL;
    char *output_file = NULL;
    char *err_file = NULL;

    char *token = strtok(cmd_str, " ");

    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            input_file = strtok(NULL, " ");
            if (!input_file) {
                fprintf(stderr, "Input file not specified.\n");
                exit(1);
            }
        }
        else if (strcmp(token, ">") == 0) {
            output_file = strtok(NULL, " ");
            if (!output_file) {
                fprintf(stderr, "Output file not specified.\n");
                exit(1);
            }
        }
        else if (strcmp(token, "2>") == 0) {
            err_file = strtok(NULL, " ");
            if (!err_file) {
                fprintf(stderr, "Error output file not specified.\n");
                exit(1);
            }
        }
        else {
            args[arg_count++] = token;
        }

        token = strtok(NULL, " ");
    }

    args[arg_count] = NULL;

    if (arg_count == 0) {
        fprintf(stderr, "Empty command between pipes.\n");
        exit(1);
    }

    if (input_file) {
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "File not found.\n");
            exit(1);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    if (output_file) {
        int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            fprintf(stderr, "Output file not specified.\n");
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    if (err_file) {
        int fd_err = open(err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0) {
            fprintf(stderr, "Error output file not specified.\n");
            exit(1);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);
    }

    execvp(args[0], args);

    if (is_pipe_sequence)
        fprintf(stderr, "Command not found in pipe sequence.\n");
    else
        fprintf(stderr, "Command not found.\n");

    exit(1);
}

void execute_pipeline(char *input) {
    char *commands[MAX_CMDS];
    pid_t pids[MAX_CMDS];
    int num_cmds = 0;

    int len = strlen(input);
    while (len > 0 && isspace(input[len - 1])) len--;

    if (len > 0 && input[len - 1] == '|') {
        fprintf(stderr, "Command missing after pipe.\n");
        return;
    }

    char *token = strtok(input, "|");
    while (token != NULL) {
        commands[num_cmds] = strdup(token);
        trim_whitespace(commands[num_cmds]);

        if (strlen(commands[num_cmds]) == 0) {
            fprintf(stderr, "Empty command between pipes.\n");
            free(commands[num_cmds]);
            return;
        }

        num_cmds++;
        token = strtok(NULL, "|");
    }

    if (num_cmds == 0)
        return;

    int fd[2];
    int prev_fd = -1;

    for (int i = 0; i < num_cmds; i++) {

        if (i < num_cmds - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                return;
            }
        }

        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            return;
        }

        if (pids[i] == 0) {
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }

            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            execute_single_pipe_cmd(commands[i], num_cmds > 1);
        }
        else {
            if (prev_fd != -1)
                close(prev_fd);

            if (i < num_cmds - 1) {
                close(fd[1]);
                prev_fd = fd[0];
            }
        }
    }

    for (int i = 0; i < num_cmds; i++)
        waitpid(pids[i], NULL, 0);

    for (int i = 0; i < num_cmds; i++)
        free(commands[i]);
}
