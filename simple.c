//parsing and execution of a simple command with no pipes
//handles input, output, and error redirection

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "simple.h"

#define MAX_ARGS 100 // defining a maximum number of arguments for a command

int parse_input(char *input, char *args[]);

//function to parse the input command and execute it
void execute_simple_command(char *input){
    char *args[MAX_ARGS]; // array to store the command and its arguments
    char *clean_args[MAX_ARGS]; // array to store arguments after removing redirection tokens
    int arg_count = 0; // counter for the number of arguments
    int clean_count = 0; // counter for the number of executable arguments

    char *input_file = NULL; // variable to store the input file for redirection
    char *output_file = NULL; // variable to store the output file for redirection
    char *error_file = NULL; // variable to store the error file for redirection

    //parse the input command into tokens
    arg_count = parse_input(input, args); // handle quoted strings and fill the args array

    //making a clean arguments array that excludes redirection operators and filenames to fix the error
    for (int i = 0; i < arg_count; i++) {
        // input redirection
        if (strcmp(args[i], "<") == 0) {
            if (i + 1 >= arg_count) {
                fprintf(stderr, "Error: Input file not specified\n");
                return;
            }
            input_file = args[i + 1];
            i++; // skip filename
        }

        // output redirection
        else if (strcmp(args[i], ">") == 0) {
            if (i + 1 >= arg_count) {
                fprintf(stderr, "Error: Output file not specified\n");
                return;
            }
            output_file = args[i + 1];
            i++; // skip filename
        }

        // error redirection
        else if (strcmp(args[i], "2>") == 0) {
            if (i + 1 >= arg_count) {
                fprintf(stderr, "Error: Error output file not specified\n");
                return;
            }
            error_file = args[i + 1];
            i++; // skip filename
        }

        // normal argument
        else {
            clean_args[clean_count++] = args[i];
        }
    }

    // null terminate the cleaned argument list
    clean_args[clean_count] = NULL;

    // if only redirection was provided then there is no command to run
    if (clean_count == 0) {
        if (input_file || output_file || error_file) {
            fprintf(stderr, "Error: No command specified\n");
        }
        return;
    }
    
    //fork a child process to execute the command
    pid_t pid = fork(); // create a new child process

    if(pid < 0){ // check for fork error
        perror("Fork failed");
        return;
    } 
    else if(pid == 0){ // child process
        if(input_file != NULL){
            //open the file in read only mode
            int fd = open(input_file, O_RDONLY);

            //error if the fike cannot be opened
            if(fd < 0){
                fprintf(stderr, "File not found.\n");
                exit(EXIT_FAILURE);
            }
            //redirect standard input to the file
            dup2(fd, STDIN_FILENO);
            // close the file descriptor after redirection
            close(fd); 
        }

        //handle output redirection 
        if(output_file != NULL){
            //open the file in write only mode, create it if it doesn't exist, and truncate it if it does exist
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            //error if the file cannot be opened
            if(fd < 0){
                perror("Error opening output file");
                exit(EXIT_FAILURE);
            }

            //redirect standard output to the file
            dup2(fd, STDOUT_FILENO);
            // close the file descriptor after redirection
            close(fd); 
        }

        //handle error redirection
        if(error_file != NULL){
            //open the file in write only mode, create it if it doesn't exist, and truncate it if it does exist
            int fd = open(error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            //error if the file cannot be opened
            if(fd < 0){
                perror("Error opening error file");
                exit(EXIT_FAILURE);
            }
            //redirect standard error to the file
            dup2(fd, STDERR_FILENO);
            // close the file descriptor after redirection
            close(fd); 
        }

        //handle echo as a built-in command
        if (strcmp(clean_args[0], "echo") == 0) {

            int interpret_escape = 0;
            int start = 1;

            // check for -e flag
            if (clean_args[1] && strcmp(clean_args[1], "-e") == 0) {
                interpret_escape = 1;
                start = 2;
            }

            for (int i = start; clean_args[i] != NULL; i++) {
                char *str = clean_args[i];

                for (int j = 0; str[j] != '\0'; j++) {
                    if (interpret_escape && str[j] == '\\' && str[j+1] == 'n') {
                        printf("\n");
                        j++;
                    } else {
                        printf("%c", str[j]);
                    }
                }

                if (clean_args[i+1]) printf(" ");
            }

            printf("\n");
            exit(EXIT_SUCCESS); // exit after handling echo
        }

        // execute the command with the cleaned argument list.
        execvp(clean_args[0], clean_args);

        fprintf(stderr, "Command not found.\n"); // if exec returns, it means it failed
        exit(EXIT_FAILURE); // exit with failure status
    } 
    
    else { // parent process
        wait(NULL); // wait for the child process to finish
    }
}

//function for a tokenizer that handles quoted strings together 
int parse_input(char *input, char *args[]){
    int i = 0; // index for args array
    char *p = input; // pointer to traverse the input string
    while(*p){
        //skip spaces
        while(*p && *p == ' '){
            p++;
        }
        if(*p == '\0'){
            break; // end of input
        }

        //handle quoted strings
        if(*p == '"' || *p == '\''){
            char quote = *p; // store the type of quote
            p++; // skip the opening quote
            args[i++] = p; // start of the argument
            while(*p && *p != quote){
                p++; // move to the closing quote
            }
            if(*p == quote){
                *p = '\0'; // null-terminate the argument
                p++; // skip the closing quote
            }
        } 
        else {
            args[i++] = p; // start of the argument
            while(*p && *p != ' '){
                p++; // move to the end of the argument
            }
            if(*p){
                *p = '\0'; // null-terminate the argument
                p++; // skip the space
            }
        }
    }
    args[i] = NULL; // nullterminate the args array
    return i; // return the number of arguments parsed
}