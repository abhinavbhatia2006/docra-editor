# docra

> A high-performance, multithreaded collaborative terminal editor built in pure C — engineered from scratch with a custom CRDT synchronization engine, raw TCP sockets, and strict POSIX concurrency control.

![Language](https://img.shields.io/badge/language-C-blue?style=flat-square)
![Build](https://img.shields.io/badge/build-make-brightgreen?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-informational?style=flat-square)
![Course](https://img.shields.io/badge/course-Operating%20Systems%20(2nd%20Year)-orange?style=flat-square)

---

## Table of Contents

- [Overview](#overview)
- [OS Concepts Demonstrated](#os-concepts-demonstrated)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Components](#components)
- [How CRDT Works in docra](#how-crdt-works-in-docra)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Running](#running)
- [Testing](#testing)
- [Makefile Targets](#makefile-targets)
- [Design Decisions](#design-decisions)
- [Known Limitations](#known-limitations)
- [License](#license)

---

## Overview

**docra** is a real-time collaborative text editor that runs entirely in the terminal. Multiple clients connect to a central server over TCP and edit a shared document simultaneously. All edits are conflict-free and converge to a consistent state across every peer — even under concurrent, out-of-order operations — thanks to a hand-rolled **Conflict-free Replicated Data Type (CRDT)** engine implemented entirely in C.

This project was built as the capstone assignment for a 2nd-year Operating Systems course, with the goal of applying systems-programming theory — process/thread management, synchronization, IPC, and network I/O — to a real, working application.

---

## OS Concepts Demonstrated

| Concept | Where Applied |
|---|---|
| **POSIX Threads (`pthreads`)** | Each client connection is handled by a dedicated server thread |
| **Mutex / Condition Variables** | Guards shared document state across concurrent writer threads |
| **Raw TCP Sockets** | Client–server communication via `socket()`, `bind()`, `listen()`, `accept()` |
| **`select()` / Non-blocking I/O** | TUI client multiplexes keyboard input and network reads |
| **Process Synchronization** | CRDT merge operations are lock-protected to prevent torn reads/writes |
| **File I/O & Archiving** | Session archiver persists document snapshots to disk |
| **Signal Handling** | Graceful shutdown on `SIGINT`/`SIGTERM` |
| **Terminal Control (ncurses)** | Raw-mode terminal UI with real-time rendering |

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    docra_server                     │
│                                                     │
│  main thread                                        │
│   └─ accept() loop                                  │
│        └─ per-client pthread ──► session manager    │
│                                      │              │
│                              ┌───────▼──────┐       │
│                              │  CRDT Engine │       │
│                              │  (shared/)   │       │
│                              └───────┬──────┘       │
│                                      │              │
│                              ┌───────▼──────┐       │
│                              │   Archiver   │       │
│                              │ (disk I/O)   │       │
│                              └──────────────┘       │
└──────────────────────┬──────────────────────────────┘
         TCP           │          TCP
   ┌─────▼─────┐       │    ┌─────▼─────┐
   │  Client A │       │    │  Client B │
   │  (TUI)    │       │    │  (TUI)    │
   └───────────┘       │    └───────────┘
                  (more peers)
```

- The **server** maintains a single authoritative copy of the document, protected by a mutex.
- Each connecting client gets its own thread on the server. That thread reads incoming CRDT operations, applies them to the shared document, and broadcasts the result to all other connected clients.
- Each **client** runs a `ncurses`-based TUI. A background thread handles inbound network messages while the foreground thread handles keyboard input and re-renders the screen.
- The **CRDT engine** (in `shared/`) is used by both sides, ensuring that the same merge logic is applied consistently.

---

## Project Structure

```
docra-editor/
├── client/
│   ├── include/
│   │   └── client.h          # Client-side type definitions and prototypes
│   └── src/
│       ├── main.c            # Entry point: argument parsing, connect, launch TUI
│       ├── network.c         # TCP connect, send/recv loop, background reader thread
│       └── tui.c             # ncurses UI: rendering, keyboard handling
│
├── server/
│   ├── include/
│   │   └── server.h          # Server-side type definitions and prototypes
│   └── src/
│       ├── main.c            # Entry point: socket setup, accept loop, signal handlers
│       ├── network.c         # Per-client thread, broadcast logic
│       ├── session.c         # Session management, client registry
│       └── archiver.c        # Periodic document snapshots to disk
│
├── shared/
│   ├── include/
│   │   └── crdt.h            # CRDT node/document types, operation structs
│   └── src/
│       └── crdt.c            # Insert/delete operations, merge, total ordering
│
├── tools/
│   ├── logger.c              # Standalone log dashboard (logger_dashboard binary)
│   └── tester.c              # Concurrent stress tester (spawns threads, fires ops)
│
├── .vscode/                  # Editor configuration (optional)
├── Makefile
├── LICENSE
└── README.md
```

---

## Components

### `server/`

| File | Responsibility |
|---|---|
| `main.c` | Binds to a TCP port, spawns a per-client `pthread` on each `accept()`, installs `SIGINT` handler for graceful shutdown |
| `network.c` | Per-client thread loop: deserializes incoming CRDT operations, acquires the document mutex, calls the CRDT merge function, then broadcasts the updated operation to all other clients |
| `session.c` | Maintains the connected client list; provides thread-safe add/remove/iterate primitives |
| `archiver.c` | Periodically serializes the document state to a `.log` file so sessions can survive a server restart |

### `client/`

| File | Responsibility |
|---|---|
| `main.c` | Parses host/port args, establishes TCP connection, launches TUI and network threads |
| `network.c` | Background reader thread: blocks on `recv()`, deserializes inbound operations, applies them to the local CRDT copy, signals the TUI to redraw |
| `tui.c` | ncurses front-end: raw-mode keyboard capture, cursor movement, character insert/delete, full-document re-render on each update |

### `shared/`

The CRDT engine is compiled into both binaries.

| File | Responsibility |
|---|---|
| `crdt.h` | Defines `CRDTNode` (character + unique ID + tombstone flag), `CRDTDoc` (sorted node array), and `CRDTOp` (the wire-format operation struct) |
| `crdt.c` | `crdt_insert()`, `crdt_delete()`, `crdt_merge()` — the core conflict-resolution logic; implements a position-based unique ID scheme so concurrent inserts at the same position resolve deterministically |

### `tools/`

| Binary | Purpose |
|---|---|
| `logger_dashboard` | Tails and pretty-prints server `.log` files in real time |
| `tester` | Stress-test harness: spawns multiple pthreads, each firing a burst of random insert/delete operations at the server to validate CRDT convergence and lock correctness |

---

## How CRDT Works in docra

A CRDT (Conflict-free Replicated Data Type) is a data structure designed so that concurrent edits from multiple sources can always be merged without conflicts.

docra implements a **sequence CRDT** (similar in spirit to LSEQ or Logoot):

1. Every character in the document is represented as a `CRDTNode` with a globally unique ID (derived from a `{client_id, logical_clock}` pair).
2. An **insert** operation carries the new character plus the ID of the node it should follow. Because IDs are globally unique and the ordering is deterministic, two clients inserting at the "same" position will always resolve to the same final order.
3. A **delete** operation is a **tombstone** — the node is marked deleted but not removed from the array. This ensures that a delete that arrives after a concurrent insert on the same position still applies correctly.
4. The server serializes all operations and broadcasts them. Every client applies every operation through the same `crdt_merge()` function, guaranteeing **eventual consistency** — all replicas converge to the same document.

This satisfies the core OS/distributed-systems property: **commutativity and idempotency** of operations means that regardless of the order messages arrive, the final document state is identical on every peer.

---

## Prerequisites

- GCC (or any C99-compatible compiler)
- GNU Make
- `ncurses` development library
- POSIX-compliant OS (Linux or macOS)
- `pthreads` (included in glibc/libpthread on Linux)

**Install on Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential libncurses-dev
```

**Install on macOS (Homebrew):**
```bash
brew install ncurses
```

---

## Building

Clone the repository and build all targets:

```bash
git clone https://github.com/abhinavbhatia2006/docra-editor.git
cd docra-editor
make
```

This produces four binaries in the project root:

| Binary | Description |
|---|---|
| `docra_server` | The collaboration server |
| `docra_client` | The terminal editor client |
| `logger_dashboard` | Real-time log viewer |
| `tools/tester` | Concurrent stress-test harness |

---

## Running

### 1. Start the server

```bash
./docra_server <port>
```

Example:
```bash
./docra_server 8080
```

The server will listen for incoming client connections on the specified port and log activity to `server.log`.

### 2. Connect a client

Open a new terminal (or connect from a remote machine):

```bash
./docra_client <host> <port>
```

Example (local):
```bash
./docra_client 127.0.0.1 8080
```

Repeat in as many terminals as you like — all connected clients will see each other's edits in real time.

### 3. (Optional) Watch the log dashboard

```bash
./logger_dashboard
```

### Basic TUI Controls

| Key | Action |
|---|---|
| Arrow keys | Move cursor |
| Any printable character | Insert at cursor |
| `Backspace` / `Delete` | Delete character |
| `Ctrl+Q` or `Ctrl+C` | Quit |

---

## Testing

Run the built-in concurrent stress tester:

```bash
make test
```

This compiles and executes `tools/tester`, which spawns multiple threads that simultaneously connect to a running server and fire random insert/delete operations. The harness then verifies that all replicas converge to the same final document state, confirming CRDT correctness and the absence of data races.

> **Note:** A `docra_server` instance must be running before executing `make test`.

---

## Makefile Targets

| Target | Description |
|---|---|
| `make` / `make all` | Build all four binaries |
| `make test` | Build and run the stress-test harness |
| `make clean` | Remove all compiled binaries and `.log` files |

**Compiler flags used:**

```
-Wall -Wextra -O2 -pthread
```

`-pthread` links `libpthread` and enables POSIX thread-safety annotations in glibc headers.

---

## Design Decisions

**Why pure C?**
The assignment targets systems-level OS concepts. C gives direct control over memory, sockets, and thread primitives — no runtime abstractions hiding what's actually happening.

**Why a sequence CRDT instead of Operational Transformation (OT)?**
OT requires a central server to serialize and transform operations, which couples the consistency model to the network topology. A CRDT is commutative and idempotent by construction — correct convergence is a mathematical property of the data structure, not an operational guarantee that depends on message ordering.

**Why one thread per client on the server?**
For a course project with a bounded number of peers, the thread-per-connection model is straightforward to reason about and makes POSIX mutex semantics easy to demonstrate. A production system would use an event loop (`epoll`/`kqueue`) for scalability.

**Why tombstones instead of physical deletion?**
Physically removing a node changes array indices, which can corrupt the unique-ID-based position references held by in-flight operations from other clients. Tombstoning preserves the logical structure of the CRDT until it is safe to garbage-collect.

---

## Known Limitations

- The document is held entirely in memory; very large files may exhaust the CRDT node array.
- No authentication — any client that can reach the port can connect and edit.
- The archiver writes a full document snapshot on each interval rather than an incremental delta log.
- No TLS; traffic is plaintext TCP.
- Tested on Linux (Ubuntu 22.04/24.04) and macOS 13+. Windows is not supported (requires WSL).

---

## License

This project is licensed under the **MIT License**. See [`LICENSE`](LICENSE) for the full text.

---

*Built for the Operating Systems course — 2nd Year B.E. Computer Science.*
