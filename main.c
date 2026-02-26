// main file for myshell
// were going to check the inputs and see if it will use simple execustion or the pipline

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "simple.h"
#include "pipeline.h"
#include <ctype.h>

#define MAX_INPUT_SIZE 1024 // defining a maximum lenght of the user input

void preprocess_input(char *input) {
    char buffer[4096];
    int i = 0, j = 0;

    while (input[i] != '\0') {
        //handle 2>
        if (input[i] == '2' && input[i+1] == '>' && (i == 0 || isspace((unsigned char)input[i-1]))) {
            buffer[j++] = ' ';
            buffer[j++] = '2';
            buffer[j++] = '>';
            buffer[j++] = ' ';
            i += 2;
        }
        //handle single symbols
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

int main(){
    char input[MAX_INPUT_SIZE]; // storing the user input

    while(1){ // loop to keep the shell running
        //print the shell prompt:
        printf("$ ");
        fflush(stdout); //fflush to ensure immediate print


        //read the user input:
        if(fgets(input, sizeof(input), stdin) == NULL){ // check if the input is valid
            //input error handling
            printf("\n");
            break;
        }
        preprocess_input(input);
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
        if(strchr(input, '|') != NULL) {
            execute_pipeline(input);
        } 
        //otherwise, we will handle it as a simple command
        else {
            execute_simple_command(input);
        }
    }
    return 0;
}
