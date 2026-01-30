# ffind

```
  __  __ _           _ 
 / _|/ _(_)_ __   __| |
| |_| |_| | '_ \ / _` |
|  _|  _| | | | | (_| |
|_| |_| |_|_| |_|\__,_|
```

**Fast file finder with instant search**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

Fast daemon-based file finder with real-time inotify indexing.

---

## Table of Contents

- [What is ffind?](#what-is-ffind)
- [Why use ffind?](#why-use-ffind)
- [Features](#features)
- [Comparison with Other Tools](#comparison-with-other-tools)
- [Quick Start](#quick-start)
- [Requirements](#requirements)
- [Build](#build)
- [Install](#install)
- [Usage](#usage)
  - [Start the daemon](#start-the-daemon)
  - [SQLite Persistence](#sqlite-persistence-optional)
  - [Multiple Root Directories](#multiple-root-directories)
  - [Search examples](#search-examples)
  - [Size units](#size-units)
  - [Content search methods](#content-search-methods)
  - [Context lines](#context-lines)
  - [Color output](#color-output)
- [Directory Monitoring](#directory-monitoring)
- [Service Management](#service-management)
- [FAQ](#faq)
- [License](#license)
- [Author](#author)

---

## What is ffind?

`ffind` is an ultra-fast file search tool that combines the power of `find` and `grep`. It consists of a daemon that maintains an in-memory index of your filesystem using Linux inotify for real-time updates, and a client that performs instant searches against this index.

## Why use ffind?

- **Instant searches**: No more waiting for recursive directory traversal - the index is always in memory
- **Real-time indexing**: The daemon watches your filesystem and updates the index automatically as files are created, modified, or deleted
- **Parallel content search**: Utilizes all CPU cores for 6x faster content search than ripgrep
- **Powerful filters**: Search by name, path, content, size, modification time, and file type with glob patterns or regex
- **Content search**: Search inside files with fixed-string or regex patterns
- **Combined filters**: Mix multiple criteria for precise results

## Features

✓ **Real-time indexing** with Linux inotify  
✓ **Instant search** from in-memory index  
✓ **Parallel content search** using thread pool (all CPU cores)  
✓ **Content search** with fixed-string, regex, or glob patterns  
✓ **Context lines** (grep-style -A/-B/-C)  
✓ **Multiple filters** (name, path, type, size, mtime)  
✓ **Multiple root directories** support  
✓ **SQLite persistence** for fast startup  
✓ **Colored output** with auto-detection  
✓ **Glob and regex** support  
✓ **Graceful directory renames** (no restart needed)  

## Comparison with Other Tools

| Feature | find | locate | ag | ripgrep | ffind |
|---------|:----:|:------:|:--:|:-------:|:-----:|
| Real-time indexing | ✗ | ✗ | ✗ | ✗ | ✓ |
| Content search | ✗ | ✗ | ✓ | ✓ | ✓ |
| Regex support | ◐ | ✗ | ✓ | ✓ | ✓ |
| Glob patterns | ✓ | ✓ | ✗ | ✓ | ✓ |
| Context lines | ✗ | ✗ | ✓ | ✓ | ✓ |
| Multiple roots | ✓ | ✗ | ✗ | ✗ | ✓ |
| Persistence | ✗ | ✓ | ✗ | ✗ | ✓ |
| Speed (indexed) | Slow | Fast | Slow | Fast | **Instant** |

**Legend**: ◐ = partial/limited support  
**Note**: `find` has limited regex support (`-regex` for path matching). `find` and `ag`/`ripgrep` need to traverse the filesystem on every search. `locate` uses a pre-built index but doesn't update in real-time. `ffind` combines the best of both: real-time updates with instant search.

## Performance Benchmarks

**Test System:**
- CPU: Intel Core i5-8300H @ 2.30GHz (8 cores)
- RAM: 32GB
- Disk: XFS on SSD
- OS: Linux

**Test Corpus:** `/usr/include`
- Files: 63,093
- Directories: 3,841
- Total size: 712MB

Real-world benchmarks on SSD showing ffind's advantages over traditional tools:

| Operation | find/grep | ripgrep | ffind | Speedup vs find/grep | Speedup vs ripgrep |
|-----------|-----------|---------|-------|---------------------|-------------------|
| Find *.c files | 0.536s | - | **0.009s** | **59.6x faster** | - |
| Find *.h files | 0.547s | - | **0.039s** | **14.0x faster** | - |
| Find files >100KB | 2.958s | - | **0.019s** | **155.8x faster** | - |
| Content search "static" | 17.1s | 4.02s | **0.69s** | **24.7x faster** | **5.8x faster** |
| Regex search | 16.7s | 4.00s | **1.89s** | **8.8x faster** | **2.1x faster** |
| List all files | 0.568s | - | **0.029s** | **19.5x faster** | - |

**Key Results:**
- **File metadata searches**: Massive speedups (14x-156x) because ffind serves results from RAM while find traverses the filesystem
- **Content searches**: Significantly faster than both grep (24.7x) and ripgrep (5.8x) due to parallel processing and memory-mapped I/O
- **Regex searches**: Outperforms grep (8.8x) and ripgrep (2.1x) using the RE2 regex engine

**Benchmark Methodology:** Tests use fair cache management methodology:
- `find`/`grep` run with cold cache (caches flushed before each run when run with proper privileges)
- `ffind` runs with warm cache (data in daemon's RAM index)
- Each benchmark runs 3 times, median time reported
- This represents real-world usage: `find`/`grep` read from disk, `ffind` serves from memory

**Why ffind is faster:**
- **File metadata searches**: No disk I/O - results served instantly from in-memory index
- **Content searches**: Parallel processing using all CPU cores with minimal disk latency
- **Real-time indexing**: Index stays up-to-date automatically via inotify

For complete benchmark details and full results, see [`BENCHMARK_RESULTS.md`](BENCHMARK_RESULTS.md).

### Indexing Performance

Initial indexing of test corpus (5,629 files, 44 directories, 28MB):
- Time: ~1-2 seconds
- Memory: ~20-30 MB resident
- Rate: ~3,000-5,000 files/second

*Note: ffind times exclude initial indexing. Subsequent searches use the in-memory index for instant results. The daemon maintains the index in real-time as files change.*

### Benchmark Methodology

The benchmark script (`benchmarks/run_real_benchmarks.sh`) uses scientifically sound methodology for fair comparisons:

**Cache Management with Dedicated Binary:**
- ✓ **Cache-flush utility**: Minimal C binary requiring `sudo` for secure cache clearing
- ✓ **Security best practice**: Only the tiny `cache-flush` binary runs with elevated privileges
- ✓ **Minimal sudo usage**: Only cache-flush requires sudo, benchmark scripts run as normal user
- ✓ **Fair comparison**: `find`/`grep` run with cold cache (disk reads), `ffind` with warm cache (RAM)

**Security Model:**
- The `cache-flush` binary must run with `sudo` to write to `/proc/sys/vm/drop_caches`
- Only this tiny, auditable C program (~50 lines) needs elevated privileges
- All other components (ffind-daemon, ffind client, benchmark scripts) run as normal user

**Setup (one-time):**
```bash
cd benchmarks
make      # Build the cache-flush utility
```

**Statistical Rigor:**
- Each benchmark runs **3 times**
- Reports **median** (reduces outlier impact), **min**, **max**, and **variance**
- Warns if variance exceeds 20% (indicates unreliable results)
- Warmup run discarded to eliminate cold start effects

**Fairness:**
- `find`/`grep`: Cold cache (reads from disk) - simulates real-world "first query" scenario
- `ffind`: Warm cache (data in RAM) - simulates daemon's persistent index
- This comparison is **fair** because it shows the true benefit of ffind's in-memory indexing

**Benchmark script available:** `benchmarks/run_real_benchmarks.sh`

To reproduce these benchmarks:
```bash
# Build ffind
make

# Build cache-flush utility (one-time)
cd benchmarks
make

# Run benchmarks (will prompt for sudo password for cache flushing)
./run_real_benchmarks.sh
```

**Note on sudo**: The benchmark script internally calls `sudo` for cache-flush only. The script itself should be run as a normal user, not with sudo. You will be prompted for your password when cache clearing is needed.

**Note on Results Interpretation:**
- **With cache flushing**: Shows true speedup of in-memory index vs disk traversal
- **Without cache flushing**: Results may be misleading if ffind runs first and warms the cache for find/grep


## Quick Start

```bash
# 1. Build
make

# 2. Install (optional)
sudo make install

# 3. Start the daemon in foreground to watch a directory
./ffind-daemon --foreground ~/projects

# 4. In another terminal, search for files
./ffind "*.cpp"                    # Find all C++ files
./ffind -c "TODO"                  # Search for "TODO" in file contents
./ffind -name "*.h" -mtime -7      # Header files modified in last 7 days
```

That's it! The daemon keeps the index updated in real-time as files change.

## Requirements

- Linux (uses inotify for filesystem monitoring)
- g++ with C++20 support
- pthread library
- sqlite3 development libraries (for persistence feature)
- RE2 library (for regex support)

## Build

### Install Dependencies

On Ubuntu/Debian:
```bash
sudo apt-get install build-essential libsqlite3-dev libre2-dev
```

On Fedora/RHEL:
```bash
sudo dnf install gcc-c++ sqlite-devel re2-devel
```

On Arch Linux:
```bash
sudo pacman -S base-devel sqlite re2
```

### Compile

```bash
make
```

This produces two executables:
- `ffind-daemon` - the indexing daemon
- `ffind` - the search client

### Build Cache-Flush Utility (Optional - for benchmarking)

For fair benchmark comparisons with cache flushing:

```bash
cd benchmarks
make
```

This creates a minimal helper (~50 lines of C) that flushes filesystem caches. The helper requires `sudo` to run because writing to `/proc/sys/vm/drop_caches` requires elevated privileges.

**Security Note**: Only this tiny, auditable binary needs elevated privileges - never run ffind-daemon or the main ffind client as root.

## Install

```bash
sudo make install
```

This installs the binaries to `/usr/local/bin/` and man pages to `/usr/local/share/man/`.

## Usage

### Start the daemon

The daemon needs to be running before you can search. It will index the specified directory tree:

```bash
# Run in background (daemonize)
ffind-daemon /path/to/index

# Run in foreground (useful for debugging)
ffind-daemon --foreground /path/to/index
```

The daemon creates a Unix socket at `/run/user/$UID/ffind.sock` for client communication.

### SQLite Persistence (Optional)

For faster startup on large directory trees, enable SQLite persistence:

```bash
# Enable persistence with --db flag
ffind-daemon --db ~/.cache/ffind.db ~/projects

# With foreground mode
ffind-daemon --foreground --db ~/.cache/ffind.db ~/projects

# Multiple roots with persistence
ffind-daemon --db /var/cache/ffind/index.db /home/user/projects /var/log
```

**Benefits:**
- **Fast startup**: Loads index from database instead of full filesystem scan
- **Crash-safe**: Atomic writes with WAL mode ensure database consistency
- **Automatic reconciliation**: Detects filesystem changes between runs
- **Periodic sync**: Database updated every 30 seconds or after 100 changes

**How it works:**
1. On first run, creates SQLite database and indexes filesystem
2. Changes are tracked in memory and periodically flushed to database
3. On restart, loads entries from database and reconciles with actual filesystem
4. Graceful shutdown ensures all pending changes are written

**Notes:**
- Database file is created if it doesn't exist
- Database uses WAL (Write-Ahead Logging) mode for better performance and safety
- Parent directories must exist for the database path
- If root paths change, full reconciliation is triggered automatically

### Multiple Root Directories

Monitor multiple directories simultaneously by specifying them as additional arguments:

```bash
# Monitor multiple directories
ffind-daemon /home/user/projects /var/log /etc/config

# With foreground mode
ffind-daemon --foreground ~/code ~/documents ~/Downloads
```

When searching with multiple roots:
- Search results include files from all monitored directories
- Path globs (`-path`) are matched relative to each root's base directory
- All filters and features work seamlessly across all roots

**Notes:**
- Overlapping roots (e.g., `/home` and `/home/user`) are detected and warned about
- Duplicate paths are automatically deduplicated
- Each root is indexed and monitored independently

### Search examples

All features are fully implemented:

```bash
# Basic glob search (searches file names)
ffind "*.cpp"

# Explicit name search
ffind -name "*.h"

# Path glob search with type filter
ffind -path "src/*" -type f

# Case-insensitive name search
ffind -name "readme*" -i

# Content search (fixed string)
ffind -c "TODO"

# Content search with regex
ffind -c "TODO.*fix" -r

# Regex content search with case insensitivity
ffind -c "error" -r -i

# Content glob (fnmatch patterns on lines)
ffind -g "TODO*"

# Content glob: lines containing "error" (case insensitive)
ffind -g "*error*" -i

# Content glob: function calls in C files
ffind -g "func_*(*)" -name "*.c"

# Content glob: lines starting with digit, with 2 lines of context
ffind -g "[0-9]*" -A 2

# Context lines: show 3 lines after each match (like grep -A)
ffind -c "error" -A 3

# Context lines: show 2 lines before each match (like grep -B)
ffind -c "TODO" -B 2

# Context lines: show 5 lines before and after (like grep -C)
ffind -c "FIXME" -C 5

# Context with regex and case insensitive
ffind -c "bug" -A 2 -B 1 -r -i

# Find only files
ffind -type f

# Find only directories
ffind -type d

# Size filters (larger than 1MB)
ffind -size +1M

# Size filters (smaller than 100KB)
ffind -size -100k

# Modification time (modified within last 7 days)
ffind -mtime -7

# Modification time (older than 30 days)
ffind -mtime +30

# Combined filters (large log files)
ffind -name "*.log" -size +10M -type f

# Color output (force colors)
ffind "*.cpp" --color=always

# Color output (no colors, good for scripting)
ffind -c "error" --color=never

# Color output (auto-detect TTY, default behavior)
ffind -c "TODO" --color=auto
```

### Size units

- `c` - bytes (default if no unit specified)
- `b` - 512-byte blocks
- `k` - kilobytes (1024 bytes)
- `M` - megabytes (1024*1024 bytes)
- `G` - gigabytes (1024*1024*1024 bytes)

### Size operators

- `+SIZE` - greater than SIZE
- `-SIZE` - less than SIZE
- `SIZE` - exactly SIZE (rarely used)

### Time operators

- `-N` - modified within last N days
- `+N` - modified more than N days ago
- `N` - modified exactly N days ago (rarely used)

### Content search methods

ffind provides three ways to search file contents, each suited for different use cases:

#### Fixed string search (`-c`)
Fast substring matching - searches for exact text within lines:
```bash
ffind -c "TODO"           # Find lines containing "TODO"
ffind -c "error" -i       # Case-insensitive search
```

#### Regex search (`-c` + `-r`)
Powerful pattern matching using ECMAScript regular expressions:
```bash
ffind -c "TODO.*fix" -r           # Match TODO followed by fix
ffind -c "bug|error" -r -i        # Match bug OR error (case-insensitive)
ffind -c "^[0-9]+" -r             # Lines starting with digits
```

#### Glob search (`-g`)
Shell-style wildcard patterns for intuitive matching:
```bash
ffind -g "TODO*"                  # Lines starting with TODO
ffind -g "*error*" -i             # Lines containing error (case-insensitive)
ffind -g "func_*(*)"              # Function call patterns
ffind -g "[0-9]*"                 # Lines starting with a digit
```

Glob patterns support:
- `*` - matches any sequence of characters
- `?` - matches exactly one character
- `[abc]` - matches any character in the set
- `[a-z]` - matches any character in the range

**Note**: `-g` is mutually exclusive with `-c` and `-r`. Use `-c` for fixed string search, `-c` with `-r` for regex search, or `-g` for glob pattern search.

### Context lines

Context lines work just like grep's `-A`, `-B`, and `-C` options, showing surrounding lines for better context when searching file contents. These flags require `-c` or `-g` (content search).

- `-A N` - Show N lines **after** each match
- `-B N` - Show N lines **before** each match
- `-C N` - Show N lines **before and after** each match (shorthand for `-A N -B N`)

#### Output format

Context lines use the same grep-style format:
- **Match lines**: `path:lineno:content` (colon before content)
- **Context lines**: `path:lineno-content` (dash before content)
- **Group separator**: `--` appears between non-contiguous match groups

When match contexts overlap, they are automatically merged into a single group without separators.

Example:
```bash
# Show 2 lines after each TODO
ffind -c "TODO" -A 2

# Show 3 lines before each error
ffind -c "error" -i -B 3

# Show 5 lines of context around each FIXME
ffind -c "FIXME" -C 5

# Combine with regex and other filters
ffind -c "bug|error" -r -A 3 -B 1 -name "*.cpp"
```

### Color output

The `--color` option controls colored output for better readability:

- `--color=auto` (default) - Automatically enables colors when output is to a terminal
- `--color=always` - Forces colored output even when piped or redirected
- `--color=never` - Disables all colored output (useful for scripts and piping)

When colors are enabled:
- **File paths** are displayed in bold
- **Line numbers** (in content search) are shown in cyan
- **Matched content** is highlighted in bold red

Example:
```bash
# Search with colors in terminal (auto-detect)
ffind -c "TODO" 

# Force colors even when piping to less -R
ffind -c "error" --color=always | less -R

# Disable colors for scripting
ffind -c "warning" --color=never > results.txt
```

## Directory Monitoring

The daemon handles directory renames gracefully using inotify with cookie-based rename tracking - no restart needed. When a directory is renamed within the watched tree:

- All file paths are automatically updated to reflect the new location
- Internal watch descriptors are updated
- Files remain searchable at their new paths immediately

In foreground mode (`--foreground`), directory changes are logged to stderr with color-coded INFO messages:
- Directory created/deleted events
- Directory rename events with old and new paths
- Number of entries updated during renames

The implementation uses modern inotify (not DNOTIFY) for reliable filesystem event monitoring.

## Service Management

### Gentoo (OpenRC)

For Gentoo systems using OpenRC, you can run ffind-daemon as a system service:

1. Copy the init script:
   ```bash
   sudo cp ffind-daemon.openrc /etc/init.d/ffind-daemon
   sudo chmod +x /etc/init.d/ffind-daemon
   ```

2. Copy and configure the service settings:
   ```bash
   sudo cp etc-conf.d-ffind-daemon.example /etc/conf.d/ffind-daemon
   sudo nano /etc/conf.d/ffind-daemon
   ```
   
   Edit `FFIND_ROOTS` to specify which directories to index:
   ```bash
   FFIND_ROOTS="/home /var/www"
   ```
   
   Optionally enable SQLite persistence:
   ```bash
   FFIND_OPTS="--db /var/cache/ffind/index.db"
   ```

3. Start the service:
   ```bash
   sudo rc-service ffind-daemon start
   ```

4. Enable at boot (optional):
   ```bash
   sudo rc-update add ffind-daemon default
   ```

5. Check service status:
   ```bash
   sudo rc-service ffind-daemon status
   ```

**Note**: If using `--db` option, ensure the parent directory exists and is writable:
```bash
sudo mkdir -p /var/cache/ffind
sudo chown root:root /var/cache/ffind
```

### systemd (Most Linux Distributions)

For systems using systemd (Ubuntu, Fedora, Debian, Arch, etc.):

1. Install the service file:
   ```bash
   sudo make install-systemd
   ```

2. Edit the configuration:
   ```bash
   sudo nano /etc/ffind/config.yaml
   ```

3. Modify the service if needed:
   ```bash
   sudo systemctl edit ffind-daemon
   ```
   
   Override the ExecStart to change indexed directories:
   ```ini
   [Service]
   ExecStart=
   ExecStart=/usr/local/bin/ffind-daemon --foreground --db /var/cache/ffind/index.db /home /var/www
   ```

4. Start the service:
   ```bash
   sudo systemctl start ffind-daemon
   ```

5. Enable at boot:
   ```bash
   sudo systemctl enable ffind-daemon
   ```

6. Check status:
   ```bash
   sudo systemctl status ffind-daemon
   ```

**Note**: The default service monitors `/home`. Edit the service to change directories.

## FAQ

### Q: How fast is it?

**A:** For indexed searches, ffind is nearly instantaneous. Once the daemon has indexed your directories, searches complete in milliseconds regardless of directory size. Traditional tools like `find` need to traverse the filesystem on every search, which can take seconds or minutes on large directory trees.

The initial indexing time depends on the size of your directory tree. On modern hardware:
- Small directories (< 10,000 files): < 1 second
- Medium directories (10,000 - 100,000 files): 1-5 seconds  
- Large directories (100,000+ files): 5-30 seconds

After initial indexing, the daemon maintains the index in real-time with negligible overhead.

### Q: Does it use a lot of RAM?

**A:** Memory usage is proportional to the number of indexed files. On average:
- Small directories (< 10,000 files): < 10 MB
- Medium directories (10,000 - 100,000 files): 10-50 MB
- Large directories (100,000+ files): 50-200 MB

Each indexed entry stores the file path, size, modification time, and metadata. For most use cases, memory usage is minimal compared to modern system RAM.

### Q: What about huge directory trees?

**A:** ffind handles large directory trees efficiently:

1. **Indexing**: Initial indexing shows progress every 10,000 entries (in foreground mode)
2. **Memory**: Uses efficient C++ data structures to minimize memory overhead
3. **Persistence**: Use `--db` option to save the index to SQLite for fast startup
4. **Multiple roots**: Index only the directories you need, not the entire filesystem

For very large trees (> 1 million files), consider:
- Using SQLite persistence (`--db`) for faster restarts
- Splitting into multiple daemons with different roots
- Excluding large binary directories you don't need to search

### Q: How does persistence work?

**A:** When you enable SQLite persistence with `--db /path/to/db`:

1. **First run**: The daemon indexes your filesystem and saves entries to the database
2. **Subsequent runs**: The daemon loads the index from the database (much faster than re-scanning)
3. **Reconciliation**: The daemon automatically detects and handles changes since last run
4. **Updates**: Changes are batched and saved periodically (every 30 seconds or 100 changes)
5. **Crash safety**: Uses WAL (Write-Ahead Logging) mode for atomic writes

This means:
- ✓ Fast startup even after system reboot
- ✓ No data loss on crashes or power failures  
- ✓ Automatic sync between database and filesystem

### Q: Can I use it on network filesystems (NFS, CIFS)?

**A:** inotify (which ffind uses for real-time monitoring) only works on local filesystems. For network filesystems:
- ✗ Real-time monitoring won't work
- ✓ You can still use ffind, but you'll need to restart the daemon to pick up changes
- → Consider using `locate` or scheduled rescans for network shares

### Q: How do I search multiple directories?

**A:** Just specify multiple directories when starting the daemon:

```bash
ffind-daemon /home/user/projects /var/www /etc/config
```

The client will search across all monitored directories automatically.

### Q: What's the difference between `-c` and `-g`?

**A:** Both search file contents, but differently:
- `-c "TODO"`: Fixed-string substring search (fastest)
- `-c "TODO.*fix" -r`: Regex pattern search (powerful)
- `-g "TODO*"`: Shell-style glob pattern search (intuitive)

Use `-c` for simple text, `-c -r` for complex patterns, and `-g` for wildcard patterns.

## License

Dual-licensed under GPLv3 and Commercial license.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See the LICENSE file for details.

**Commercial licenses** are available for proprietary use, embedded systems, and enterprise deployments. Contact EdgeOfAssembly for commercial licensing inquiries.

## Author

**EdgeOfAssembly** - [GitHub](https://github.com/EdgeOfAssembly)

For commercial licensing inquiries: haxbox2000@gmail.com
