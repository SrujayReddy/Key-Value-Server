
# Key-Value Server

## Table of Contents
1. [Overview](#overview)
2. [Implementors](#implementors)
3. [Project Architecture](#project-architecture)
   - [Directory Structure](#directory-structure)
   - [Ring Buffer](#ring-buffer)
   - [KV Store](#kv-store)
4. [Building the Project](#building-the-project)
5. [Running the Server and Client](#running-the-server-and-client)
   - [Command-Line Arguments](#command-line-arguments)
   - [Examples](#examples)
6. [Workload Generation](#workload-generation)
7. [Resources](#resources)
8. [License](#license)

----------

## Overview

This project implements a **concurrent Key-Value (KV) Server** that communicates with a **multi-threaded client** via a **shared memory** region. The shared memory region is divided into:

-   A **Ring Buffer** (for request submission by the client and consumption by the server), and
-   A **Request-status Board** (for the server to post results back to the client).

The key objectives are:

-   Implementing a **thread-safe**, **lockless** (or partially lock-free) ring buffer using atomic operations or minimal locking.
-   Building a **multi-threaded**, **thread-safe** KV Store with fine-grained locking or atomic operations for fast concurrent access.
-   Demonstrating **interprocess communication** (IPC) using a file-backed `mmap`.
-   Ensuring high concurrency and minimal blocking between client and server threads.

----------

## Implementors

1.  **Dhruv Desai**
    
    -   **CS Login:** ddesai
    -   **NetID:** ddesai7
    -   **Email:** [ddesai7@wisc.edu](mailto:ddesai7@wisc.edu)
2.  **Srujay Jakkidi**
    
    -   **CS Login:** srujay
    -   **NetID:** jakkidi
    -   **Email:** [jakkidi@wisc.edu](mailto:jakkidi@wisc.edu)

----------

## Project Architecture

### Directory Structure
```
Key-Value-Server/
├── README.md               # This README
├── Makefile                # Builds client and server
├── include/
│   ├── common.h            # Contains shared typedefs and hash function (DO NOT MODIFY)
│   └── ring_buffer.h       # Ring Buffer interface
├── src/
│   ├── client.c            # Client code (producer)
│   ├── ring_buffer.c       # Ring Buffer implementation
│   ├── kv_store.c          # KV Store + server main() implementation
│   └── kv_store.h          # If a separate header is created for KV Store
├── scripts/
│   └── gen_workload.py     # Helper script to generate workloads
├── tests/
│   ├── workload.txt        # Example workload file
│   └── resources.txt       # Document describing external resources used
├── solution.txt            # Explanation of the implementation approach

```

**Note:**

-   `kv_store.h` is optional; create it if you prefer a separate header for your KV Store data structures and prototypes.
-   `solution.txt` can be used to provide further insights into your design.

### Ring Buffer

-   **Location**: `src/ring_buffer.c` / `include/ring_buffer.h`
-   **Purpose**: Acts as a **lockless** (or minimally locked) producer-consumer queue between client and server processes.
-   **Key Operations**:
    -   `init_ring(struct ring *r)`: Initializes the ring buffer’s head and tail indices and any synchronization primitives (e.g., atomic counters).
    -   `ring_submit(struct ring *r, struct buffer_descriptor *bd)`: Thread-safe enqueue operation.
    -   `ring_get(struct ring *r, struct buffer_descriptor *bd)`: Thread-safe dequeue operation.

### KV Store

-   **Location**: `src/kv_store.c` (and optionally `src/kv_store.h`)
-   **Purpose**: Maintains key-value mappings. Must handle concurrent `PUT` and `GET` operations from multiple server threads.
-   **Features**:
    -   **Hashtable** with open addressing (linear probing) or chaining.
    -   **Fine-Grained Locking** or lock-free approach for concurrency.
    -   **Rehashing / Migration** support (if implemented) to handle table growth.

----------

## Building the Project

A `Makefile` is provided at the project root. By default, it produces two executables: `client` and `server`.

1.  **Navigate** to the project directory.
    
2.  **Build** both client and server by running:
    
    ```bash
    make
    
    ```
    
    This triggers the `all` target, compiling `client.c`, `kv_store.c`, and `ring_buffer.c`.
    
3.  **Clean** (optional):
    
    ```bash
    make clean
    
    ```
    
    Removes object files and any existing `client`/`server` binaries.
    

----------

## Running the Server and Client

### Command-Line Arguments

1.  **Server**
    
    ```
    ./server -n <num_server_threads> -s <initial_hashtable_size>
    
    ```
    
    -   `-n`: Number of **server threads**.
    -   `-s`: The **initial hashtable size** for the KV Store.
2.  **Client**
    
    ```
    ./client -n <num_client_threads> -w <windows_per_client_thread>
    
    ```
    
    -   `-n`: Number of **client threads**.
    -   `-w`: Number of **Request-status Board windows** each client thread owns.

> **Note:**
> -   The **shared memory** is created in the client using a file named `shmem_file`.
> -   The **server** also maps the same file for interprocess communication.

### Examples

-   **Starting the Server** with 2 threads and an initial hashtable size of 5:
    
    ```bash
    ./server -n 2 -s 5
    
    ```
    
-   **Starting the Client** with 3 threads and 2 windows per thread:
    
    ```bash
    ./client -n 3 -w 2
    
    ```
    
-   **Automation**: You can run the client first (which might fork the server in some implementations) or manually start the server and then the client, depending on your design.

----------

## Workload Generation

A script named `gen_workload.py` is provided under `scripts/`. Use it to generate input workloads (PUT/GET requests) for stress testing your KV store. The generated output can be redirected to `workload.txt` or any file of your choice.

Example usage:

```bash
python3 scripts/gen_workload.py 1000 > tests/workload.txt

```


## Resources

-   **DPDK Ring** concepts: [DPDK Documentation](https://doc.dpdk.org/)
-   **Concurrent Hash Tables**: “Concurrent Hash Tables: Fast and General (?)” by Jacobson et al.

----------

## License

This project was developed as part of the **CS 537: Introduction to Operating Systems** course at the University of Wisconsin–Madison. It is shared strictly for educational and learning purposes only.

**Important Notes:**
- Redistribution or reuse of this code for academic submissions is prohibited and may violate academic integrity policies.
- The project is licensed under the [MIT License](https://opensource.org/licenses/MIT). Any usage outside academic purposes must include proper attribution.


----------

**Enjoy building and experimenting with your concurrent Key-Value Server!**

