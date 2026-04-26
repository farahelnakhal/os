#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_QUEUE_SIZE 256 //max number of tasks in the queue
#define QUANTUM_ROUND1 3 //seconds for the first scheduling round
#define QUANTUM_REST 7 //seconds for all subsequent rounds

//task structure representing a client request
typedef struct task {
    int task_id;
    int client_id;
    char command[1024];
    int burst_time;
    int remaining_time; 
    int arrival_time;
    int round;
    int sock;
    pid_t pid;
} task_t;

void scheduler_init(void); //initializes scheduler state
void scheduler_enqueue(task_t *task); //adds a task to the scheduling queue
void scheduler_remove_client(int client_id); //removes all tasks belonging to a disconnected client
void *scheduler_thread(void *arg); //scheduler main loop (runs in a separate thread)

#endif