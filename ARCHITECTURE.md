# ffind Architecture Documentation

**Version:** 1.0  
**Last Updated:** 2026-01-29  
**Purpose:** Comprehensive architectural documentation for ffind daemon-based file finder

---

## Table of Contents

1. [Overview](#overview)
2. [System Architecture](#system-architecture)
3. [Component Descriptions](#component-descriptions)
4. [Data Flow Diagrams](#data-flow-diagrams)
5. [Protocol Specification](#protocol-specification)
6. [Threading Model](#threading-model)
7. [Security Architecture](#security-architecture)
8. [Storage and Persistence](#storage-and-persistence)
9. [Error Handling](#error-handling)
10. [Performance Optimizations](#performance-optimizations)

---

## Overview

ffind is a high-performance file search system consisting of two main components:

- **ffind-daemon** - Long-running server that maintains an in-memory file index
- **ffind** - Client CLI tool that queries the daemon and displays results

The system uses Linux inotify for real-time filesystem monitoring and provides instant search capabilities through memory-resident indexes.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          ffind System Architecture                   │
└─────────────────────────────────────────────────────────────────────┘

                    ┌──────────────────────────────┐
                    │      User Terminal           │
                    │   $ ffind "*.cpp" -c "TODO"  │
                    └──────────────┬───────────────┘
                                   │
                                   │ (executes)
                                   │
                    ┌──────────────▼───────────────┐
                    │      ffind Client            │
                    │   (ffind.cpp - ~400 LOC)     │
                    │                              │
                    │  • Parse CLI arguments       │
                    │  • Serialize query           │
                    │  • Connect to daemon socket  │
                    │  • Stream and colorize output│
                    └──────────────┬───────────────┘
                                   │
                                   │ Unix Domain Socket
                                   │ /run/user/{UID}/ffind.sock
                                   │
                    ┌──────────────▼───────────────┐
                    │    ffind-daemon              │
                    │  (ffind-daemon.cpp ~2500 LOC)│
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │  Main Event Loop       │  │
                    │  │  • Accept connections  │  │
                    │  │  • Monitor inotify     │  │
                    │  │  • Handle queries      │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │  In-Memory Index       │  │
                    │  │  vector<Entry> entries │  │
                    │  │  • File paths          │  │
                    │  │  • Sizes, mtimes       │  │
                    │  │  • Real-time updates   │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │  Thread Pool           │  │
                    │  │  • Parallel content    │  │
                    │  │    search              │  │
                    │  │  • N worker threads    │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │  SQLite Persistence    │  │
                    │  │  (Optional --db)       │  │
                    │  │  • WAL mode            │  │
                    │  │  • Periodic flushes    │  │
                    │  └────────────────────────┘  │
                    └───────┬──────────┬───────────┘
                            │          │
                    ┌───────▼──┐   ┌──▼────────┐
                    │ inotify  │   │ Filesystem│
                    │ Events   │   │ I/O       │
                    └──────────┘   └───────────┘
```

---

## Component Descriptions

### ffind Client (ffind.cpp)

**Purpose:** Command-line interface for querying the daemon

**Key Responsibilities:**
- Parse command-line arguments and options
- Validate input parameters
- Serialize query into binary protocol
- Connect to daemon via Unix socket
- Stream results with color highlighting
- Handle connection errors gracefully

**Key Functions:**
- `main()` - Entry point, argument parsing, connection management
- Output formatting with ANSI color codes
- Regex/content match highlighting

**Dependencies:**
- RE2 library (for regex support)
- Unix socket API

---

### ffind-daemon (ffind-daemon.cpp)

**Purpose:** Long-running server maintaining file index

**Key Responsibilities:**
- Build and maintain in-memory file index
- Monitor filesystem changes via inotify
- Handle client queries efficiently
- Persist index to SQLite (optional)
- Manage thread pool for parallel searches

**Key Modules:**

#### 1. Index Management
```cpp
struct Entry {
    string path;           // Full file path
    uint64_t size;         // File size in bytes
    time_t mtime;          // Modification time
    bool is_directory;     // Directory flag
};
vector<Entry> entries;     // Main in-memory index
```

#### 2. inotify Integration
- Watches all directories recursively
- Handles CREATE, DELETE, MODIFY, MOVE events
- Cookie-based rename tracking
- Automatic watch descriptor management

#### 3. Query Handler
- Deserializes client requests
- Filters entries by multiple criteria
- Executes parallel content searches
- Batches results for efficient transmission

#### 4. Thread Pool
- Pre-allocated worker threads
- Task queue for content search jobs
- Synchronization via mutex and condition variables

#### 5. SQLite Persistence
- WAL mode for concurrent access
- Periodic flushes (every 30s or 100 changes)
- Reconciliation on startup
- Atomic transactions

---

## Data Flow Diagrams

### Query Processing Flow

```
┌──────────┐
│  Client  │
│ (ffind)  │
└────┬─────┘
     │
     │ 1. Serialize Query
     │    ┌────────────────────────────────────┐
     │    │ Binary Protocol (Network Byte Order)│
     │    │ • name_pattern_len (4 bytes)       │
     │    │ • name_pattern (UTF-8)             │
     │    │ • path_pattern_len (4 bytes)       │
     │    │ • path_pattern (UTF-8)             │
     │    │ • content_pattern_len (4 bytes)    │
     │    │ • content_pattern (UTF-8)          │
     │    │ • flags (1 byte)                   │
     │    │   - case_insensitive: bit 0        │
     │    │   - use_regex: bit 1               │
     │    │   - use_glob: bit 2                │
     │    │ • type_filter (1 byte)             │
     │    │ • size_op, size_val (9 bytes)      │
     │    │ • mtime_op, mtime_days (5 bytes)   │
     │    │ • before_ctx, after_ctx (2 bytes)  │
     │    └────────────────────────────────────┘
     │
     │ 2. Send to Daemon
     ▼
┌─────────────────┐
│   Unix Socket   │
│ /run/user/{UID}/│
│  ffind.sock     │
└────┬────────────┘
     │
     │ 3. Daemon Receives
     ▼
┌────────────────────────────────────────────────┐
│              ffind-daemon                      │
│                                                │
│  ┌──────────────────────────────────────────┐ │
│  │ handle_client()                          │ │
│  │                                          │ │
│  │  Step 1: Deserialize query               │ │
│  │  ├─ Validate string lengths (<1MB)       │ │
│  │  └─ Parse all filter parameters          │ │
│  │                                          │ │
│  │  Step 2: Lock mutex, access entries[]    │ │
│  │                                          │ │
│  │  Step 3: Filter Phase 1 (Metadata)       │ │
│  │  ├─ Name pattern (glob/regex)            │ │
│  │  ├─ Path pattern (glob/regex)            │ │
│  │  ├─ Type filter (file/dir)               │ │
│  │  ├─ Size filter (+/-/=)                  │ │
│  │  └─ Mtime filter (+/-/=)                 │ │
│  │       ↓                                  │ │
│  │   candidates[] vector                    │ │
│  │                                          │ │
│  │  Step 4: Content Search (if needed)      │ │
│  │  ├─ Dispatch to thread pool              │ │
│  │  ├─ Each thread:                         │ │
│  │  │   • mmap file (MAP_PRIVATE)           │ │
│  │  │   • Scan lines                        │ │
│  │  │   • Match pattern (fixed/regex/glob)  │ │
│  │  │   • Collect context lines             │ │
│  │  └─ Aggregate results                    │ │
│  │                                          │ │
│  │  Step 5: Send Results                    │ │
│  │  └─ Batch with writev() for efficiency   │ │
│  └──────────────────────────────────────────┘ │
└────────────────────────────────────────────────┘
     │
     │ 4. Stream Results
     ▼
┌──────────┐
│  Client  │
│  ├─ Read line by line                       │
│  ├─ Apply color highlighting                │
│  └─ Output to stdout                        │
└──────────┘
```

### Filesystem Change Propagation

```
┌───────────────┐
│  Filesystem   │
│   Changes     │
│               │
│  • create()   │
│  • unlink()   │
│  • rename()   │
│  • write()    │
└───────┬───────┘
        │
        │ Kernel generates events
        ▼
┌─────────────────────────────────────┐
│      Linux inotify Subsystem        │
│                                     │
│  Event Types:                       │
│  • IN_CREATE                        │
│  • IN_DELETE                        │
│  • IN_MODIFY                        │
│  • IN_MOVED_FROM / IN_MOVED_TO      │
│  • IN_DELETE_SELF                   │
└───────┬─────────────────────────────┘
        │
        │ Events queued in kernel buffer
        │ Read via read() on inotify fd
        ▼
┌──────────────────────────────────────────┐
│        ffind-daemon Event Loop           │
│                                          │
│  ┌────────────────────────────────────┐  │
│  │ process_events()                   │  │
│  │                                    │  │
│  │  while (true) {                    │  │
│  │    poll(inotify_fd, timeout)       │  │
│  │    if (events_ready) {             │  │
│  │      read(inotify_fd, buf, size)   │  │
│  │      for each event:               │  │
│  │        switch (event.mask) {       │  │
│  │          case IN_CREATE:           │  │
│  │            add_entry()             │  │
│  │          case IN_DELETE:           │  │
│  │            remove_entry()          │  │
│  │          case IN_MODIFY:           │  │
│  │            update_mtime()          │  │
│  │          case IN_MOVED_FROM:       │  │
│  │            store_cookie()          │  │
│  │          case IN_MOVED_TO:         │  │
│  │            handle_rename()         │  │
│  │        }                           │  │
│  │    }                               │  │
│  │  }                                 │  │
│  └────────────────────────────────────┘  │
│                                          │
│  Index Updates:                          │
│  ┌────────────────────────────────────┐  │
│  │ Lock entries_mutex                 │  │
│  │ Modify entries[] vector            │  │
│  │ Update pending_changes counter     │  │
│  │ Unlock entries_mutex               │  │
│  └────────────────────────────────────┘  │
│                                          │
│  Periodic DB Flush:                      │
│  ┌────────────────────────────────────┐  │
│  │ if (pending >= 100 || 30s elapsed) │  │
│  │   flush_changes_to_db()            │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

---

## Protocol Specification

### Client-to-Daemon Binary Protocol

All multi-byte integers use **network byte order** (big-endian).

```
┌─────────────────────────────────────────────────────────────┐
│                    Query Request Format                      │
├─────────────────────────────────────────────────────────────┤
│ Offset │ Length │ Type    │ Field             │ Notes        │
├────────┼────────┼─────────┼───────────────────┼──────────────┤
│   0    │   4    │ uint32  │ name_pattern_len  │ Network order│
│   4    │   N    │ char[]  │ name_pattern      │ UTF-8        │
│  4+N   │   4    │ uint32  │ path_pattern_len  │ Network order│
│  8+N   │   M    │ char[]  │ path_pattern      │ UTF-8        │
│ 8+N+M  │   4    │ uint32  │ content_pat_len   │ Network order│
│12+N+M  │   K    │ char[]  │ content_pattern   │ UTF-8        │
│12+N+M+K│   1    │ uint8   │ flags             │ See below    │
│13+N+M+K│   1    │ uint8   │ type_filter       │ 0=all,1=f,2=d│
│14+N+M+K│   1    │ uint8   │ size_op           │ 0=none,1=+,-,=│
│15+N+M+K│   8    │ uint64  │ size_val          │ Only if op≠0 │
│23+N+M+K│   1    │ uint8   │ mtime_op          │ 0=none,1=+,-,=│
│24+N+M+K│   4    │ int32   │ mtime_days        │ Only if op≠0 │
│28+N+M+K│   1    │ uint8   │ before_ctx        │ Lines before │
│29+N+M+K│   1    │ uint8   │ after_ctx         │ Lines after  │
└─────────────────────────────────────────────────────────────┘

Flags byte (bit fields):
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ 7 │ 6 │ 5 │ 4 │ 3 │ 2 │ 1 │ 0 │
├───┼───┼───┼───┼───┼───┼───┼───┤
│   │   │   │   │   │ G │ R │ I │
└───┴───┴───┴───┴───┴───┴───┴───┘
  I = case_insensitive (bit 0)
  R = use_regex (bit 1)
  G = use_glob (bit 2)
```

### Daemon-to-Client Response Format

Results are streamed as newline-delimited text:

```
For metadata-only searches:
    /path/to/file1\n
    /path/to/file2\n
    ...

For content searches (no context):
    /path/to/file:123:matched line content\n
    /path/to/file:456:another match\n
    ...

For content searches (with context):
    /path/to/file:120-context line before\n
    /path/to/file:121-another context\n
    /path/to/file:122:MATCHED LINE\n
    /path/to/file:123-context after\n
    --\n
    /path/to/file:200:ANOTHER MATCH\n
    ...

Format:
    {path}:{line_number}{separator}{line_content}\n
    
    Separator:
      ':' = matching line
      '-' = context line
    
    Group separator: '--\n' between non-contiguous groups
```

---

## Threading Model

### Overview

ffind-daemon uses a **hybrid threading model**:

1. **Main thread** - Event loop for inotify and socket accept
2. **Client handler threads** - One per active client connection (short-lived)
3. **Worker thread pool** - Pre-allocated threads for content search

```
┌────────────────────────────────────────────────────────────┐
│                  ffind-daemon Threading                     │
└────────────────────────────────────────────────────────────┘

Main Thread (Event Loop)
━━━━━━━━━━━━━━━━━━━━━━
  │
  ├──→ poll() on:
  │    ├─ inotify_fd      (filesystem events)
  │    └─ listen_sock_fd  (new client connections)
  │
  ├──→ inotify events → process_events()
  │    └─ Update entries[] vector (mutex protected)
  │
  └──→ accept() → spawn thread → handle_client()
                                      ↓
                        ┌─────────────────────────┐
                        │  Client Handler Thread  │
                        │  (short-lived)          │
                        │                         │
                        │  1. Deserialize query   │
                        │  2. Lock entries_mutex  │
                        │  3. Filter metadata     │
                        │  4. If content search:  │
                        │     dispatch to pool    │
                        │  5. Send results        │
                        │  6. Close socket        │
                        │  7. Thread exits        │
                        └────────┬────────────────┘
                                 │
                                 │ Content search dispatch
                                 ▼
        ┌───────────────────────────────────────────────┐
        │           Worker Thread Pool                  │
        │                                               │
        │  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
        │  │ Worker 1 │  │ Worker 2 │  │ Worker N │   │
        │  └─────┬────┘  └─────┬────┘  └─────┬────┘   │
        │        │             │             │         │
        │        └─────────────┴─────────────┘         │
        │                     │                        │
        │            Task Queue (mutex + CV)           │
        │         ┌──────────────────────┐             │
        │         │ Task: search file X  │             │
        │         │ Task: search file Y  │             │
        │         │ Task: search file Z  │             │
        │         └──────────────────────┘             │
        │                                               │
        │  Each worker:                                 │
        │    while (true) {                             │
        │      wait_for_task()                          │
        │      task = dequeue()                         │
        │      result = search_file(task)               │
        │      store_result(result)                     │
        │    }                                          │
        └───────────────────────────────────────────────┘

Synchronization:
  • entries_mutex: Protects entries[] vector
  • queue_mutex + queue_cv: Thread pool task queue
  • results_mutex: Protects content search results
```

### Thread Safety Guarantees

| Resource          | Protection Mechanism    | Access Pattern                |
|-------------------|-------------------------|-------------------------------|
| `entries[]`       | `entries_mutex`         | Read: many, Write: exclusive  |
| `path_index`      | `entries_mutex`         | Rebuilt when entries modified |
| Task queue        | `queue_mutex + queue_cv`| Producer-consumer             |
| Content results   | `results_mutex`         | Append-only during search     |
| SQLite database   | SQLite internal locking | WAL mode for concurrency      |
| inotify watches   | Single-threaded access  | Main thread only              |

---

## Security Architecture

### Threat Model

**Assumptions:**
- Daemon runs as unprivileged user
- Unix socket is per-user (untrusted users cannot connect)
- Filesystem is trusted (daemon watches paths it has permission to read)
- Network is local only (Unix domain sockets)

**Attack Vectors Considered:**
1. Malicious client sending crafted queries
2. Resource exhaustion (CPU, memory, file descriptors)
3. Path traversal attempts
4. Signal-based attacks during critical sections
5. Race conditions in filesystem operations

### Security Layers

```
┌──────────────────────────────────────────────────────────┐
│              Defense-in-Depth Layers                      │
└──────────────────────────────────────────────────────────┘

Layer 1: Network Input Validation
═══════════════════════════════════
  ✓ String length limits (1MB max per field)
  ✓ Integer overflow checks
  ✓ Safe deserialization with bounds checking
  ✓ Reject malformed protocol messages

      ↓ Query accepted
      
Layer 2: Filesystem Access Control
══════════════════════════════════
  ✓ Symlink detection and skipping (prevent loops)
  ✓ Permission checks (open() will fail if not readable)
  ✓ Path canonicalization (prevent ../ attacks)
  ✓ Per-user socket isolation

      ↓ Paths validated
      
Layer 3: Resource Limits
═══════════════════════
  ✓ Thread pool size bounded (prevent fork bombs)
  ✓ File size checks before mmap
  ✓ Binary file detection (skip non-text)
  ✓ Socket timeout and cleanup

      ↓ Resources allocated safely
      
Layer 4: Memory Safety
═════════════════════
  ✓ Modern C++ (smart pointers, RAII)
  ✓ Vector bounds checking (at() for critical access)
  ✓ MAP_PRIVATE for mmap (no accidental writes)
  ✓ No buffer overflows (std::string, std::vector)

      ↓ Memory operations safe
      
Layer 5: Error Handling
══════════════════════
  ✓ All syscalls checked for errors
  ✓ EINTR handling for all I/O
  ✓ EPIPE handling for broken connections
  ✓ Graceful degradation on failures

      ↓ Errors handled properly
      
Layer 6: Signal Safety
═════════════════════
  ✓ Only async-signal-safe functions in handlers
  ✓ Atomic flag for shutdown coordination
  ✓ No malloc/free in signal context
  ✓ Minimal work in handlers

      ↓ Signal-safe operation
```

### Security-Critical Code Sections

**SECURITY markers in code identify critical sections:**

1. **Network deserialization** (`handle_client()` lines 1692-1733)
   - Validates all input lengths before allocation
   - Rejects oversized requests

2. **Signal handlers** (`sig_handler()`, `crash_handler()`)
   - Uses only write() with fixed buffers
   - No heap allocation or C++ streams

3. **Filesystem traversal** (`add_directory_recursive()`)
   - Checks `is_symlink()` to prevent loops
   - Skips entries that fail permission checks

4. **Memory mapping** (`MappedFile` class)
   - Uses MAP_PRIVATE (read-only semantics)
   - Checks file size before mapping
   - Handles mmap failures gracefully

5. **Database operations** (various `*_db()` functions)
   - Parameterized queries (no SQL injection)
   - Transaction boundaries for atomicity
   - Proper error handling for corruption

---

## Storage and Persistence

### In-Memory Index Structure

```
┌──────────────────────────────────────────────────┐
│           Primary Index: vector<Entry>           │
├──────────────────────────────────────────────────┤
│                                                  │
│  Entry 0: {path="/home/user/doc.txt",           │
│            size=4096, mtime=1706572800,          │
│            is_directory=false}                   │
│                                                  │
│  Entry 1: {path="/home/user/code/",             │
│            size=0, mtime=1706572900,             │
│            is_directory=true}                    │
│                                                  │
│  Entry 2: {path="/home/user/code/main.cpp",     │
│            size=2048, mtime=1706573000,          │
│            is_directory=false}                   │
│            ...                                   │
│            (sorted by path for binary search)    │
└──────────────────────────────────────────────────┘
                      │
                      │ Supplemented by
                      ▼
┌──────────────────────────────────────────────────┐
│        PathIndex: map<string, vector<size_t>>    │
├──────────────────────────────────────────────────┤
│                                                  │
│  "home" → [1]                                    │
│  "home/user" → [1]                               │
│  "home/user/code" → [1, 2]                       │
│                                                  │
│  (Maps directory paths to entry indices for     │
│   fast path-based queries like -path "code/*")  │
└──────────────────────────────────────────────────┘
```

### SQLite Schema (Optional Persistence)

```sql
CREATE TABLE entries (
    path TEXT PRIMARY KEY,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    is_directory INTEGER NOT NULL  -- 0 or 1
);

CREATE INDEX idx_mtime ON entries(mtime);
CREATE INDEX idx_size ON entries(size);

-- Metadata table for reconciliation
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT
);
-- Stores: last_sync_time, root_paths, version
```

### Persistence Flow

```
┌───────────────────────────────────────────────────────┐
│              Persistence Lifecycle                     │
└───────────────────────────────────────────────────────┘

Daemon Startup with --db:
═════════════════════════
  1. Open/create SQLite database
  2. Enable WAL mode (sqlite3_exec("PRAGMA journal_mode=WAL"))
  3. Load entries from database → entries[] vector
  4. Perform filesystem reconciliation:
     ├─ Scan filesystem for actual files
     ├─ Compare with DB entries
     ├─ Add new files
     ├─ Remove deleted files
     └─ Update changed files
  5. Mark database as synced

Runtime Operation:
═════════════════
  Main Loop:
    ├─ Process inotify events
    ├─ Update entries[] in memory
    ├─ Increment pending_changes counter
    │
    └─ Periodic check (every iteration):
        if (pending_changes >= 100 || time_since_flush > 30s) {
          flush_changes_to_db()
          pending_changes = 0
          last_flush_time = now
        }

Graceful Shutdown:
═════════════════
  1. Receive SIGTERM/SIGINT
  2. Set shutdown flag
  3. Flush remaining changes to DB
  4. Close database connection
  5. Unlink socket file
  6. Exit

Crash Recovery:
══════════════
  • WAL mode ensures atomic commits
  • On next startup, reconcile detects inconsistencies
  • Missing entries are re-indexed from filesystem
  • Database integrity maintained
```

---

## Error Handling

### Error Handling Strategy

ffind follows a **graceful degradation** approach:

1. **Non-fatal errors**: Log and continue (e.g., permission denied on one file)
2. **Connection errors**: Close client socket, continue serving others
3. **Fatal errors**: Flush database, cleanup, exit with error code

### Error Categories and Responses

```
┌──────────────────────────────────────────────────────────┐
│                 Error Handling Matrix                     │
├─────────────────┬────────────────────┬───────────────────┤
│ Error Type      │ Example            │ Response          │
├─────────────────┼────────────────────┼───────────────────┤
│ I/O Error       │ EINTR on read()    │ Retry syscall     │
│                 │ EAGAIN on write()  │ Retry with poll() │
│                 │ EPIPE on write()   │ Close connection  │
│                 │ EIO on disk        │ Skip file, log    │
├─────────────────┼────────────────────┼───────────────────┤
│ Permission      │ EACCES on open()   │ Skip file, warn   │
│                 │ EPERM on inotify   │ Skip dir, warn    │
├─────────────────┼────────────────────┼───────────────────┤
│ Resource Limit  │ ENFILE (too many   │ Wait and retry    │
│                 │  open files)       │ or fail gracefully│
│                 │ EMFILE             │                   │
├─────────────────┼────────────────────┼───────────────────┤
│ Protocol Error  │ Invalid query      │ Send error msg,   │
│                 │ Oversized input    │ close connection  │
├─────────────────┼────────────────────┼───────────────────┤
│ Database Error  │ SQLITE_BUSY        │ Retry with backoff│
│                 │ SQLITE_CORRUPT     │ Warn, disable DB  │
├─────────────────┼────────────────────┼───────────────────┤
│ Signal          │ SIGTERM            │ Graceful shutdown │
│                 │ SIGSEGV            │ Flush DB, dump    │
│                 │                    │ stack, exit       │
└─────────────────┴────────────────────┴───────────────────┘
```

### Error Logging

**Foreground mode:** Detailed logging to stderr with color codes
**Background mode:** Minimal logging (errors only)

Log levels:
- `[INFO]` - Normal operation (foreground only)
- `[WARNING]` - Non-fatal issues (yellow)
- `[ERROR]` - Fatal errors (red)

---

## Performance Optimizations

### 1. Memory-Resident Index

**Optimization:** Keep entire file index in RAM

**Benefit:**
- Metadata queries complete in microseconds
- No disk I/O for name/path/size/mtime filters
- Binary search on sorted vector (O(log n))

**Trade-off:** Memory usage ~100 bytes per file

---

### 2. Thread Pool for Content Search

**Optimization:** Pre-allocated worker threads

**Benefit:**
- No thread creation overhead per query
- Parallel file scanning (utilizes all CPU cores)
- Task queue for load balancing

**Implementation:**
```
Thread pool size = min(32, hardware_concurrency())
Each worker processes subset of candidate files
Results aggregated before transmission
```

---

### 3. Batched Result Transmission

**Optimization:** Use `writev()` to batch multiple results

**Benefit:**
- Fewer system calls (10-100x reduction)
- Better throughput for large result sets
- Reduced per-call overhead

**Implementation:**
```cpp
vector<iovec> iov;
for (auto& result : results) {
    iov.push_back({result.data(), result.size()});
    if (iov.size() >= 100) {
        writev(fd, iov.data(), iov.size());
        iov.clear();
    }
}
```

---

### 4. Zero-Copy File Reading (mmap)

**Optimization:** Memory-map files for content search

**Benefit:**
- Kernel handles page caching
- No user-space buffer copying
- Efficient for large files

**Implementation:**
```cpp
MappedFile mf(path);
if (mf.valid()) {
    const char* data = mf.data();
    size_t size = mf.size();
    // Scan data directly without copying
}
```

---

### 5. Path Index for Directory Queries

**Optimization:** Precomputed map of directory → entries

**Benefit:**
- Fast `-path "dir/*"` queries
- Avoids scanning all entries
- O(1) lookup + O(k) iteration (k = files in dir)

**Rebuild Strategy:**
- Rebuilt when entries modified
- Cached until next modification
- Lazy evaluation

---

### 6. inotify for Real-Time Updates

**Optimization:** Use Linux inotify instead of periodic rescans

**Benefit:**
- Zero polling overhead
- Instant index updates
- No missed changes

**Watch Descriptor Management:**
- Recursive watches on all directories
- Automatic cleanup on directory deletion
- Cookie-based rename tracking

---

### 7. SQLite Write-Ahead Logging (WAL)

**Optimization:** Enable WAL mode for database

**Benefit:**
- Concurrent readers during writes
- Atomic commits
- Better crash recovery

**Configuration:**
```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;  -- Balance safety/performance
```

---

### Performance Benchmarks Summary

| Operation                | Time       | Notes                          |
|--------------------------|------------|--------------------------------|
| Index 10,000 files       | ~2 seconds | One-time startup cost          |
| Name pattern search      | <1ms       | In-memory scan with filtering  |
| Content search (grep-like)| 20-50ms   | Parallel, all cores, 5,000 files|
| Directory path search    | <1ms       | Path index lookup              |
| inotify update latency   | <1ms       | From fs event to index update  |
| Database flush (10k rows)| ~100ms     | Every 30s or 100 changes       |

---

## Appendix: Build and Development

### Build Process

```bash
# Install dependencies
sudo apt-get install build-essential libsqlite3-dev libre2-dev

# Build
make clean && make

# Outputs:
#   ffind-daemon (daemon binary)
#   ffind (client binary)
```

### Development Guidelines

1. **Code Style:**
   - Modern C++20 features preferred
   - Use RAII for all resources
   - Prefer `std::` containers over raw arrays

2. **Error Handling:**
   - Check ALL system call return values
   - Use exceptions sparingly (prefer error codes)
   - Document error conditions in function headers

3. **Testing:**
   - Run `./tests/run_tests.sh` after changes
   - Test with large directories (100k+ files)
   - Verify zero compiler warnings

4. **Security:**
   - Add `// SECURITY:` comments for security-critical code
   - Use `// REVIEWER_NOTE:` for complex logic needing review
   - Run static analyzers (cppcheck, clang-tidy)

---

## Glossary

- **Entry** - A single file or directory in the index
- **inotify** - Linux kernel subsystem for filesystem event monitoring
- **Watch Descriptor (WD)** - Kernel handle for monitored directory
- **Path Index** - Auxiliary data structure mapping directories to entries
- **Content Search** - Grep-like scanning of file contents
- **WAL** - Write-Ahead Logging (SQLite journaling mode)
- **Reconciliation** - Process of syncing database with actual filesystem
- **Context Lines** - Lines before/after a match (like grep -A/-B/-C)

---

**End of Architecture Documentation**

For implementation details, see source code comments.  
For usage information, see README.md.  
For security audit notes, see function-level documentation in source files.
