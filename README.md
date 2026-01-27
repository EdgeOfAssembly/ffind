# ffind

Fast daemon-based file finder with real-time inotify indexing.

## What is ffind?

`ffind` is an ultra-fast file search tool that combines the power of `find` and `grep`. It consists of a daemon that maintains an in-memory index of your filesystem using Linux inotify for real-time updates, and a client that performs instant searches against this index.

## Why use ffind?

- **Instant searches**: No more waiting for recursive directory traversal - the index is always in memory
- **Real-time indexing**: The daemon watches your filesystem and updates the index automatically as files are created, modified, or deleted
- **Powerful filters**: Search by name, path, content, size, modification time, and file type with glob patterns or regex
- **Content search**: Search inside files with fixed-string or regex patterns
- **Combined filters**: Mix multiple criteria for precise results

## Requirements

- Linux (uses inotify for filesystem monitoring)
- g++ with C++20 support
- pthread library

## Build

```bash
make
```

This produces two executables:
- `ffind-daemon` - the indexing daemon
- `ffind` - the search client

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

## License

Dual-licensed under GPLv3 and Commercial license.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See the LICENSE file for details.

**Commercial licenses** are available for proprietary use, embedded systems, and enterprise deployments. Contact EdgeOfAssembly for commercial licensing inquiries.

## Author

**EdgeOfAssembly** - [GitHub](https://github.com/EdgeOfAssembly)

For commercial licensing inquiries: haxbox2000@gmail.com
