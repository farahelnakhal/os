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

//helper to remove leading and trailing spaces from a string
void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    //shift string to beginning
    memmove(str - (str - str), str, strlen(str) + 1); 
}

//handle execution and redirection for one cmd
void execute_single_pipe_cmd(char *cmd_str) {
    char *args[MAX_ARGS];
    int arg_count = 0;
    
    char *input_file = NULL;
    char *output_file = NULL;
    char *err_file = NULL;

    //split command by spaces to separate args and redirection symbols
    char *token = strtok(cmd_str, " ");
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            input_file = strtok(NULL, " ");
            if (!input_file) { fprintf(stderr, "Input file not specified.\n"); exit(1); }
        } else if (strcmp(token, ">") == 0) {
            output_file = strtok(NULL, " ");
            if (!output_file) { fprintf(stderr, "Output file not specified.\n"); exit(1); }
        } else if (strcmp(token, "2>") == 0) {
            err_file = strtok(NULL, " ");
            if (!err_file) { fprintf(stderr, "Error output file not specified.\n"); exit(1); }
        } else {
            args[arg_count++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL; //null terminate 

    //input redirection
    if (input_file) {
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "File not found.\n");
            exit(1);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    //output redirection
    if (output_file) {
        int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    //error redirection
    if (err_file) {
        int fd_err = open(err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0) {
            perror("Failed to open error file");
            exit(1);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);
    }

    //no command->exit
    if (arg_count == 0) exit(0);

    //execute cmd
    execvp(args[0], args);
    
    //execvp failed, error
    fprintf(stderr, "Command not found.\n");
    exit(1);
}

void execute_pipeline(char *input) {
    char *commands[MAX_CMDS];
    pid_t pids[MAX_CMDS]; //track pids to wait correctly
    int num_cmds = 0;

    //split input str by pipe
    char *token = strtok(input, "|");
    while (token != NULL) {
        commands[num_cmds] = strdup(token); //duplicate to modify safely
        trim_whitespace(commands[num_cmds]);
        
        //handle empty cmd b/w pipe error
        if (strlen(commands[num_cmds]) == 0) {
            fprintf(stderr, "Empty command between pipes.\n");
            return;
        }
        num_cmds++;
        token = strtok(NULL, "|");
    }

    //cmd missing after pipe err
    if (input[strlen(input) - 1] == '|') {
         fprintf(stderr, "Command missing after pipe.\n");
         return;
    }

    int fd[2];
    int prev_fd = -1;

    //loop through all cmds to create processes and link pipes
    for (int i = 0; i < num_cmds; i++) {
        //create pipe for all-last cmd
        if (i < num_cmds - 1) {
            if (pipe(fd) < 0) {
                perror("pipe creation failed");
                return;
            }
        }

        pids[i] = fork();
        
        if (pids[i] < 0) {
            perror("fork failed");
            return;
        }

        if (pids[i] == 0) {
            //child process
            
            //link previous pipe to stdin
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            
            //link current pipe to stdout
            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]); //child doesn't read from its pipe
                close(fd[1]);
            }

            //execute the command with any specific redirections
            execute_single_pipe_cmd(commands[i]);
            
            exit(1);
        } else {
            //parent process
            //close read end of previous pipe
            if (prev_fd != -1) {
                close(prev_fd);
            }
            
            //cave read end of current pipe for next
            if (i < num_cmds - 1) {
                close(fd[1]);
                prev_fd = fd[0];
            }
        }
    }

    //wait for all pids
    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], NULL, 0);
    }
}
