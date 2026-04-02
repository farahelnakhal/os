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

int parse_pipe_input(char *input, char *args[]);

//removes leading and trailing whitespace from a string
void trim_whitespace(char *str) {
    int start = 0;
    //skip leading spaces
    while (isspace((unsigned char)str[start])) start++;

    int end = strlen(str) - 1;
    //skip trailing spaces
    while (end >= start && isspace((unsigned char)str[end])) end--;

    int i = 0;
    //shift trimmed content to the beginning
    while (start <= end) {
        str[i++] = str[start++];
    }
    str[i] = '\0';
}

//executes a single command inside a pipeline (or standalone)
void execute_single_pipe_cmd(char *cmd_str, int is_pipe_sequence, int is_first_cmd, int is_last_cmd) {
    char *args[MAX_ARGS];
    char *clean_args[MAX_ARGS];
    int arg_count = 0;
    int clean_count = 0;

    //file redirection pointers
    char *input_file = NULL;
    char *output_file = NULL;
    char *err_file = NULL;

    // parse command into tokens while keeping quoted strings intact
    arg_count = parse_pipe_input(cmd_str, args);

    for (int i = 0; i < arg_count; i++) {
        //input redirection
        if (strcmp(args[i], "<") == 0) {
            //first command in the pipline can have input redirection
            if (!is_first_cmd) {
                fprintf(stderr, "Input redirection only allowed for first command in pipeline.\n");
                exit(1);
            }

            if (i + 1 >= arg_count) {
                fprintf(stderr, "Input file not specified.\n");
                exit(1);
            }

            input_file = args[i + 1];
            i++;
        }
        //output redirection
        else if (strcmp(args[i], ">") == 0) {
            //last command in the pipeline can have output redirection
            if (!is_last_cmd) {
                fprintf(stderr, "Output redirection only allowed for last command in pipeline.\n");
                exit(1);
            }

            if (i + 1 >= arg_count) {
                fprintf(stderr, "Output file not specified.\n");
                exit(1);
            }

            output_file = args[i + 1];
            i++;
        }
        //error redirection
        else if (strcmp(args[i], "2>") == 0) {
            if (i + 1 >= arg_count) {
                fprintf(stderr, "Error output file not specified.\n");
                exit(1);
            }
            err_file = args[i + 1];
            i++;
        }
        //normal argument
        else {
            clean_args[clean_count++] = args[i];
        }
    }

    clean_args[clean_count] = NULL;

    //no actual command found
    if (clean_count == 0) {
        fprintf(stderr, "Empty command between pipes.\n");
        exit(1);
    }

    //handle input redirection
    if (input_file) {
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "File not found.\n");
            exit(1);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    //handle output redirection
    if (output_file) {
        int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            fprintf(stderr, "Output file not specified.\n");
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    //handle error redirection
    if (err_file) {
        int fd_err = open(err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0) {
            fprintf(stderr, "Error output file not specified.\n");
            exit(1);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);
    }

    //execute command
    execvp(clean_args[0], clean_args);

    //only runs if exec fails
    if (is_pipe_sequence)
        fprintf(stderr, "Command not found in pipe sequence.\n");
    else
        fprintf(stderr, "Command not found.\n");

    exit(1);
}

// parse a command string into arg tokens while preserving quoted strings (copied from simple.c)
int parse_pipe_input(char *input, char *args[]) {
    int i = 0;
    char *p = input;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p;
            p++;
            args[i++] = p;

            while (*p && *p != quote) {
                p++;
            }

            if (*p == quote) {
                *p = '\0';
                p++;
            }
        } else {
            args[i++] = p;

            while (*p && !isspace((unsigned char)*p)) {
                p++;
            }

            if (*p) {
                *p = '\0';
                p++;
            }
        }

        if (i >= MAX_ARGS - 1) {
            break;
        }
    }

    args[i] = NULL;
    return i;
}

//parses and executes a full pipeline command
void execute_pipeline(char *input) {
    char *commands[MAX_CMDS];
    pid_t pids[MAX_CMDS];
    int num_cmds = 0;

    //check for trailing pipe
    int len = strlen(input);
    while (len > 0 && isspace(input[len - 1])) len--;

    if (len > 0 && input[len - 1] == '|') {
        fprintf(stderr, "Command missing after pipe.\n");
        return;
    }

    //split input by pipe symbol
    char *token = strtok(input, "|");
    while (token != NULL) {
        commands[num_cmds] = strdup(token);
        trim_whitespace(commands[num_cmds]);

        //error if empty command between pipes
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

    //loop through each command in pipeline
    for (int i = 0; i < num_cmds; i++) {

        //create pipe for all but last command
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

        //child process
        if (pids[i] == 0) {
            //connect previous pipe to stdin
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }

            //connect current pipe to stdout
            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            execute_single_pipe_cmd(commands[i], num_cmds > 1, i == 0, i == num_cmds - 1);
        }
        //parent process
        else {
            if (prev_fd != -1)
                close(prev_fd);

            if (i < num_cmds - 1) {
                close(fd[1]);
                prev_fd = fd[0];
            }
        }
    }
    
    //wait for all children
    for (int i = 0; i < num_cmds; i++)
        waitpid(pids[i], NULL, 0);

    //free allocated memory
    for (int i = 0; i < num_cmds; i++)
        free(commands[i]);
}
