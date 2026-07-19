#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//simple demo program that prints progress 0->n
int main(int argc, char *argv[]) {
    //check for correct number of args
    if (argc != 2) {
        fprintf(stderr, "Usage: %s N\n", argv[0]);
        return EXIT_FAILURE;
    }

    //convert input to integer
    char *end;
    long n = strtol(argv[1], &end, 10);

    //validate input (must be positive integer)
    if (*end != '\0' || n <= 0) {
        fprintf(stderr, "Error: N must be a positive integer, got '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    //loop 0->n, printing progress each second
    for (long i = 0; i <= n; i++) {
        printf("Demo %ld/%ld\n", i, n);
        fflush(stdout); //force output immediately

        //sleep 1 second between prints (except last iteration)
        if (i < n) {
            sleep(1);
        }
    }

    return EXIT_SUCCESS;
}