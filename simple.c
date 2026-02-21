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

//function to parse the input command and execute it
void execute_simple_command(char *input){
    char *args[MAX_ARGS]; // array to store the command and its arguments
    char *token; // variable to store each token while parsing
    int arg_count = 0; // counter for the number of arguments

    char *input_file = NULL; // variable to store the input file for redirection
    char *output_file = NULL; // variable to store the output file for redirection
    char *error_file = NULL; // variable to store the error file for redirection

    //parse the input command into tokens
    token = strtok(input, " "); // split the input by spaces by tokeninzing

    while(token != NULL && arg_count < MAX_ARGS - 1){ // loop until we have tokens or reach max args
        
        // check for input redirection:
        if(strcmp(token, "<") == 0){ 
            token = strtok(NULL, " ");
            //error if missing file after <
            if(token == NULL){
                fprintf(stderr, "Error: Input file not specified\n");
                return;
            }
            input_file = token; // store the input file
        } 
        
        // check for output redirection:
        else if(strcmp(token, ">") == 0){
            token = strtok(NULL, " ");

            //error if missing file after >
            if(token == NULL){
                fprintf(stderr, "Error: Output file not specified\n");
                return;
            }
            output_file = token; // store the output file
        } 
        
        // check for error redirection:
        else if(strcmp(token, "2>") == 0){
            token = strtok(NULL, " ");
            //error if missing file after 2>
            if(token == NULL){
                fprintf(stderr, "Error: Error output file not specified\n");
                return;
            }
            error_file = token;
        } 
        
        //otherwise, it's a regular argument
        else {
            args[arg_count++] = token; // store the token in the args array
        }

        token = strtok(NULL, " "); // get the next token
    }

    //making sure the args array is null-terminated for execvp
    args[arg_count] = NULL;

    //if no argument was provided then we will just return without doing anything
    if(arg_count == 0){
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
                perror("Error opening input file");
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

        // execute the command with its arguments
        execvp(args[0], args); 

        perror("exec failed"); // if exec returns, it means it failed
        exit(EXIT_FAILURE); // exit with failure status
    } 
    
    else { // parent process
        wait(NULL); // wait for the child process to finish
    }
}