# Phase 4 Demo Guide

This walks through demonstrating the Phase 4 scheduler: multiple clients
submitting CPU-bound jobs (`./demo N`) that get interleaved by the scheduler
thread using Round Robin (3s first quantum / 7s subsequent) with
shortest-remaining-time-first selection and FCFS tiebreaking.

## 1. Build

```bash
cd os-main
cp demo/demo.c .   # makefile expects demo.c in the root
make
```

This produces four binaries: `myshell`, `server`, `client`, `demo`.

## 2. Start the server

```bash
./server
```

Expected output:
```
-------------------------
| Hello, Server Started |
-------------------------
```
The server is now listening on `127.0.0.1:8080` and the scheduler thread is
running in the background, waiting for jobs.

## 3. Connect multiple clients

Open **2–3 additional terminals** and run the client in each:
```bash
./client
```
Each prints `Connected to a server` and gives you a `>>>` prompt.

## 4. Demonstrate a plain (non-scheduled) command

In any client terminal:
```
>>> echo hello world
```
This runs immediately — no scheduling involved — and the output is echoed
back right away. Good for showing the baseline request/response path before
introducing scheduling.

## 5. Submit competing jobs to the scheduler

In **Client 1**:
```
>>> ./demo 10
```
In **Client 2** (submit within a few seconds of Client 1):
```
>>> ./demo 4
```
In **Client 3** (optional, for a 3-way interleave):
```
>>> ./demo 6
```
