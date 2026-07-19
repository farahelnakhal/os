# MyShell: Custom UNIX Shell with Multithreaded Client-Server Scheduler

A C project built in four phases for an Operating Systems course. It starts as a
standalone UNIX shell supporting redirection and pipelines, then grows into a
multithreaded client-server system where a scheduler thread simulates a single
CPU running multiple clients' jobs concurrently (Round Robin + Shortest-Job-
Remaining-First + FCFS tiebreak).

## Features

- **Standalone shell (`myshell`)**
  - Simple command execution via `fork`/`execvp`
  - Built-in `echo` (with `-e` escape interpretation)
  - I/O redirection: `<`, `>`, `2>`
  - Pipelines of arbitrary length: `cmd1 | cmd2 | cmd3`
  - Quoted-argument parsing (`"..."`, `'...'`)

- **Client-server mode (`server` + `client`)**
  - TCP socket server (port `8080`) accepting multiple concurrent clients
  - One thread per client (`pthread_create` + `pthread_detach`)
  - Regular shell commands run immediately and their captured stdout/stderr
    is sent back to the client
  - Special `./demo N` commands are **not** run immediately — they're wrapped
    into a `task_t` and handed to a dedicated **scheduler thread**, which
    simulates a single CPU timesharing all connected clients' jobs

- **Scheduler**
  - One shared task queue protected by a mutex + semaphore
  - Selection policy: shortest-remaining-time-first, tie-broken by arrival
    time (FCFS), with a fairness rule to avoid starving other clients
  - Round-robin quantum: `3s` for a task's first run, `7s` for every
    subsequent resumption (`QUANTUM_ROUND1` / `QUANTUM_REST` in `scheduler.h`)
  - Preemption via `SIGSTOP` / `SIGCONT` on the child process, so a job's
    progress is never lost between quanta
  - Per-client cleanup: disconnecting a client kills/dequeues all of its
    in-flight and queued tasks
  - Prints live `waiting` / `running` / `ended` state transitions and a final
    execution summary (e.g. `Summary: 0)->P1-(3)-P2-(9)-...`)

## Repository Layout

```
os-main/
├── main.c              # entry point for the standalone shell (myshell)
├── simple.c/.h         # simple (non-piped) command execution + redirection
├── pipeline.c/.h       # multi-stage pipeline parsing + execution
├── server.c            # TCP server, per-client threads, command dispatch
├── client.c            # TCP client (interactive REPL over a socket)
├── scheduler.c/.h      # scheduler thread: queue, RR+SJRF+FCFS, preemption
├── demo/
│   └── demo.c          # dummy CPU-bound program used to demo scheduling
│   └── demo_inst.md    # instructions on how to run demo
├── makefile
├── test.sh             # automated regression tests for the shell/server
└── reports/            # phase 1-4 project reports (PDF)
```

## Building

Requires `gcc` and POSIX threads (Linux/macOS/WSL).

```bash
make            # builds myshell, server, client, and demo
make clean      # removes binaries and object files
```

> **Note:** the `demo` target compiles `demo.c` from the current directory.
> If it's not found, copy it up first: `cp demo/demo.c . && make`

## Running

### Standalone shell
```bash
./myshell
$ echo -e "hello\nworld"
$ ls -la > out.txt
$ cat nofile.txt 2> err.txt
$ cat file.txt | grep foo | wc -l
$ exit
```

### Client-server mode
Terminal 1:
```bash
./server
```
Terminal 2 (one or more, to simulate multiple clients):
```bash
./client
>>> ls -la
>>> ./demo 5
>>> exit
```

Any normal shell command runs immediately. `./demo N` submits a simulated
N-second job to the scheduler instead of running it right away — see the demo
guide below to see the scheduling in action.
