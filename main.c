// main file for myshell
// were going to check the inputs and see if it will use simple execustion or the pipline

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "simple.h"

#define MAX_INPUT_SIZE 1024 // defining a maximum lenght of the user input

int main(){
    char input[MAX_INPUT_SIZE]; // storing the user input

    while(1){ // loop to keep the shell running
        //print the shell prompt:
        printf("$ ");

        //read the user input:
        if(fgets(input, sizeof(input), stdin) == NULL){ // check if the input is valid
            //input error handling
            printf("\n");
            break;
        }

        //remove any newline character from the input
        input[strcspn(input, "\n")] = 0;

        //if the user types exit then we will terminate the shell
        if(strcmp(input, "exit") == 0){
            break;
        }

        // if the input is empty, we will just continue to the next iteration of the loop
        if(strlen(input) == 0){
            continue;
        }

        //if the input contains a pipe, we will handle it as a pipeline
        //!!!! come back to this after you have pipiline code from farah!!!

        //otherwise, we will handle it as a simple command
        execute_simple_command(input);
    }
    return 0;
}