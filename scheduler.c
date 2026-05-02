#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

#define C_BLUE   "\033[0;34m"
#define C_GREEN  "\033[0;32m"
#define C_YELLOW "\033[0;33m"
#define C_RED    "\033[0;31m"
#define C_RESET  "\033[0m"

//scheduler state
static task_t *queue[MAX_QUEUE_SIZE]; //task queue
static int queue_size = 0; //current queue size
static task_t *running_task = NULL; //currently running task
static int last_selected_id = -1; //last scheduled task id (for fairness)
static int next_task_id = 1; //incremental task id
static struct timespec server_start; //server start time

//execution summary buffer
static char summary[8192];
static int summary_len = 0;

//synchronization
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t queue_sem;

//ansi colors for logging
#define C_WAIT "\033[0;33m"
#define C_RUN "\033[0;32m"
#define C_END "\033[0;31m"
#define C_INFO "\033[0;36m"
#define C_RESET "\033[0m"

//returns elapsed time since server start
static int elapsed_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int)(now.tv_sec - server_start.tv_sec);
}

//logs task state (waiting/running/ended)
static void log_state(int client_id, const char *state, int value) {
    const char *color = C_RESET;

    if (strcmp(state, "waiting") == 0) color = C_YELLOW;
    else if (strcmp(state, "running") == 0) color = C_GREEN;
    else if (strcmp(state, "ended") == 0) color = C_RED;

    printf("%s[%d]--- %s (%d)%s\n", color, client_id, state, value, C_RESET);
    fflush(stdout);
}

//appends to execution summary
static void summary_append(int client_id, int time_point) {
    int written = snprintf(
        summary + summary_len,
        sizeof(summary) - summary_len,
        "P%d-(%d)-",
        client_id,
        time_point
    );

    if (written > 0 && summary_len + written < (int)sizeof(summary)) {
        summary_len += written;
    }
}

//prints execution summary
static void summary_print(void) {
    if (summary_len == 0) return;

    //remove trailing arrow
    if (summary_len >= 2 && summary[summary_len - 1] == '>' && summary[summary_len - 2] == '-') {
        summary_len -= 2;
        summary[summary_len] = '\0';
    }

    // printf("%s[SCHEDULER]%s Execution summary: 0)->%s\n", C_INFO, C_RESET, summary);
    printf("Summary: 0)->%s\n", summary);
    fflush(stdout);
    summary_len = 0;
    summary[0] = '\0';
}

//removes task from queue at index i
static void queue_remove_at(int i) {
    free(queue[i]);
    for (int j = i; j < queue_size - 1; j++)
        queue[j] = queue[j + 1];
    queue_size--;
}

//selects next task (sjrf + fairness)
static int select_next(void) {
    if (queue_size == 0) return -1;
    int all_same = 1;

    //check if all tasks have same id as last selected
    for (int i = 0; i < queue_size; i++) {
        if (queue[i]->task_id != last_selected_id) {
            all_same = 0;
            break;
        }
    }

    int best = -1;
    for (int i = 0; i < queue_size; i++) {
        task_t *t = queue[i];

        if (!all_same && t->task_id == last_selected_id) continue; //avoid picking same task if possible
        if (best == -1) {
            best = i;
            continue;
        }

        task_t *b = queue[best];

        //shortest remaining time first
        if (t->remaining_time < b->remaining_time) {
            best = i;
            continue;
        }

        //tiebreak by arrival time (fcfs)
        if (t->remaining_time == b->remaining_time &&
            t->arrival_time < b->arrival_time) {
            best = i;
        }
    }

    return best;
}

//starts demo process for task and sets up pipe
static pid_t start_demo_child(task_t *task) {
    int pfd[2];

    if (pipe(pfd) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    if (pid == 0) {
        //child process
        close(pfd[0]);

        //redirect stdout/stderr to pipe
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);

        char n_str[32];
        snprintf(n_str, sizeof(n_str), "%d", task->burst_time);
        char *args[] = {"./demo", n_str, NULL};
        execvp("./demo", args);

        fprintf(stderr, "demo: exec failed\n");
        _exit(1);
    }

    //parent process
    close(pfd[1]);
    extern int sched_pipe_read_fd;
    sched_pipe_read_fd = pfd[0];

    return pid;
}

int sched_pipe_read_fd = -1; //pipe fd for reading child output

//forwards nonblocking output to client
static void forward_output(task_t *task) {
    if (sched_pipe_read_fd < 0) return;
    int flags = fcntl(sched_pipe_read_fd, F_GETFL, 0);
    fcntl(sched_pipe_read_fd, F_SETFL, flags | O_NONBLOCK);

    char buf[4096];
    ssize_t n;

    while ((n = read(sched_pipe_read_fd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';

        if (task->output_len + n < (int)sizeof(task->output_buffer)) {
            memcpy(task->output_buffer + task->output_len, buf, n);
            task->output_len += n;
            task->output_buffer[task->output_len] = '\0';
        }
    }

    fcntl(sched_pipe_read_fd, F_SETFL, flags & ~O_NONBLOCK);
}

//drains remaining output after process ends
static void drain_output(task_t *task) {
    if (sched_pipe_read_fd < 0) return;

    char buf[4096];
    ssize_t n;
    while ((n = read(sched_pipe_read_fd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';

        if (task->output_len + n < (int)sizeof(task->output_buffer)) {
            memcpy(task->output_buffer + task->output_len, buf, n);
            task->output_len += n;
            task->output_buffer[task->output_len] = '\0';
        }
    }

    close(sched_pipe_read_fd);
    sched_pipe_read_fd = -1;
}

//initializes scheduler
void scheduler_init(void) {
    queue_size = 0;
    running_task = NULL;
    last_selected_id = -1;
    next_task_id = 1;
    summary_len = 0;
    summary[0] = '\0';

    clock_gettime(CLOCK_MONOTONIC, &server_start);
    if (sem_init(&queue_sem, 0, 0) != 0) {
        perror("sem_init");
        exit(EXIT_FAILURE);
    }

    // printf("%s[SCHEDULER]%s Initialised (RR q1=%ds q*=%ds + SJRF + FCFS)\n", C_INFO, C_RESET, QUANTUM_ROUND1, QUANTUM_REST);
    fflush(stdout);
}

//adds task to scheduler queue
void scheduler_enqueue(task_t *task) {
    task->arrival_time = elapsed_seconds();
    task->output_len = 0;
    task->output_buffer[0] = '\0';

    pthread_mutex_lock(&queue_mutex);

    if (queue_size >= MAX_QUEUE_SIZE) {
        // fprintf(stderr, "[SCHEDULER] Queue full. Dropping task %d\n", task->task_id);
        pthread_mutex_unlock(&queue_mutex);
        free(task);
        return;
    }

    task->task_id = next_task_id++;
    queue[queue_size++] = task;

    log_state(task->client_id, "waiting", task->remaining_time);
    pthread_mutex_unlock(&queue_mutex);
    sem_post(&queue_sem);
}

//removes all tasks for a disconnected client
void scheduler_remove_client(int client_id) {
    pthread_mutex_lock(&queue_mutex);

    //kill running task if it belongs to client
    if (running_task != NULL && running_task->client_id == client_id) {
        if (running_task->pid > 0) {
            kill(running_task->pid, SIGKILL);
            waitpid(running_task->pid, NULL, 0);
        }

        if (sched_pipe_read_fd >= 0) {
            close(sched_pipe_read_fd);
            sched_pipe_read_fd = -1;
        }

        free(running_task);
        running_task = NULL;
    }

    //remove queued tasks for client
    for (int i = 0; i < queue_size;) {
        if (queue[i]->client_id == client_id) {
            if (queue[i]->pid > 0) kill(queue[i]->pid, SIGKILL);
            queue_remove_at(i);
            sem_trywait(&queue_sem);
        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&queue_mutex);
}

//main scheduler loop (runs in separate thread)
void *scheduler_thread(void *arg) {
    (void)arg;
    // printf("%s[SCHEDULER]%s Thread started.\n", C_INFO, C_RESET);
    fflush(stdout);

    while (1) {
        //wait for available tasks
        sem_wait(&queue_sem);
        pthread_mutex_lock(&queue_mutex);
        int idx = select_next();

        if (idx < 0) {
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }

        //remove selected task from queue
        task_t *task = queue[idx];
        for (int j = idx; j < queue_size - 1; j++) queue[j] = queue[j + 1];
        queue_size--;

        running_task = task;
        last_selected_id = task->task_id;

        int quantum = (task->round == 0) ? QUANTUM_ROUND1 : QUANTUM_REST;

        log_state(task->client_id, "running", task->remaining_time);
        // summary_append(task->client_id, task->burst_time);
        if (task->round == 0) {
            summary_append(task->client_id, task->burst_time);
        }
        pthread_mutex_unlock(&queue_mutex);

        //start or resume process
        if (task->pid <= 0) {
            task->pid = start_demo_child(task);

            if (task->pid < 0) {
                const char *err = "Error: failed to start demo process\n";
                send(task->sock, err, strlen(err), 0);
                pthread_mutex_lock(&queue_mutex);
                running_task = NULL;
                pthread_mutex_unlock(&queue_mutex);

                free(task);
                continue;
            }
        } else {
            kill(task->pid, SIGCONT);
        }

        int elapsed = 0;
        int done = 0;

        //run for quantum or until completion
        while (elapsed < quantum && elapsed < task->remaining_time) {
            sleep(1);
            elapsed++;
            forward_output(task);
            int status;
            pid_t result = waitpid(task->pid, &status, WNOHANG);
            if (result == task->pid) {
                task->remaining_time = 0;
                done = 1;
                break;
            }
        }

        if (!done) {
            task->remaining_time -= elapsed;
        }

        //if task finished
        if (done || task->remaining_time <= 0) {
            drain_output(task);
            log_state(task->client_id, "ended", 0);
            pthread_mutex_lock(&queue_mutex);
            running_task = NULL;
            send(task->sock, task->output_buffer, task->output_len, 0);

            printf("[%d]<<< %d bytes sent\n", task->client_id, task->output_len);
            fflush(stdout);

            if (queue_size == 0) {
                pthread_mutex_unlock(&queue_mutex);
                summary_print();
            } else {
                pthread_mutex_unlock(&queue_mutex);
            }


            free(task);

        } else {
            //preempt and requeue
            kill(task->pid, SIGSTOP);
            // forward_output(task);

            task->round++;
            log_state(task->client_id, "waiting", task->remaining_time);
            pthread_mutex_lock(&queue_mutex);
            running_task = NULL;
            if (queue_size < MAX_QUEUE_SIZE) {
                queue[queue_size++] = task;
            } else {
                // fprintf(stderr, "[SCHEDULER] Queue full on reenqueue. Dropping\n");

                kill(task->pid, SIGKILL);
                waitpid(task->pid, NULL, 0);

                if (sched_pipe_read_fd >= 0) {
                    close(sched_pipe_read_fd);
                    sched_pipe_read_fd = -1;
                }

                free(task);
                pthread_mutex_unlock(&queue_mutex);
                continue;
            }

            pthread_mutex_unlock(&queue_mutex);
            sem_post(&queue_sem);
        }
    }

    return NULL;
}