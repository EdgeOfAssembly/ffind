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

**Note**: `-c`, `-r`, and `-g` are mutually exclusive. Choose the method that best fits your search pattern.

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

## License

Dual-licensed under GPLv3 and Commercial license.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See the LICENSE file for details.

**Commercial licenses** are available for proprietary use, embedded systems, and enterprise deployments. Contact EdgeOfAssembly for commercial licensing inquiries.

## Author

**EdgeOfAssembly** - [GitHub](https://github.com/EdgeOfAssembly)

For commercial licensing inquiries: haxbox2000@gmail.com
