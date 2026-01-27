# ffind - Fast Daemon-Based File Finder

## What is ffind?

`ffind` is a fast, daemon-based file finder with real-time inotify indexing. It provides instant search results by maintaining an in-memory index of files, updated in real-time using Linux inotify.

## Why use ffind?

- **Instant searches**: No need to wait for slow recursive directory traversal
- **Real-time index**: File changes are automatically tracked and indexed
- **Memory-resident**: Index stays in memory for maximum performance
- **Advanced filtering**: Supports name/path globs, content search, size, type, and modification time filters

## Requirements

- **Operating System**: Linux (requires inotify support)
- **Compiler**: g++ with C++20 support
- **Libraries**: pthread

## Build

To build the project:

```bash
make
```

This produces two executables:
- `ffind-daemon` - The daemon that maintains the file index
- `ffind` - The client command to search for files

## Install

To install system-wide:

```bash
sudo make install
```

This installs:
- Executables to `/usr/local/bin/`
- Man pages to `/usr/local/share/man/`

## Running the Daemon

Start the daemon to index a directory tree:

```bash
# Run as background daemon
ffind-daemon /path/to/directory

# Run in foreground (useful for testing)
ffind-daemon --foreground /path/to/directory
```

The daemon communicates via a Unix domain socket at:
```
/run/user/$UID/ffind.sock
```

## Usage Examples

### Basic name glob search
```bash
ffind "*.cpp"
```

### Explicit name pattern
```bash
ffind -name "*.h"
```

### Path glob with type filter
```bash
ffind -path "src/*" -type f
```

### Case-insensitive search
```bash
ffind -name "readme*" -i
```

### Content search (fixed string)
```bash
ffind -c "TODO"
```

### Content search with regex
```bash
ffind -c "TODO.*fix" -r
```

### Regex content search with case insensitivity
```bash
ffind -c "error" -r -i
```

### Type filters
```bash
# Files only
ffind -type f

# Directories only
ffind -type d
```

### Size filters
```bash
# Larger than 1MB
ffind -size +1M

# Smaller than 100KB
ffind -size -100k

# Exactly 512 bytes
ffind -size 512b
```

Size units: `c` (bytes), `b` (512-byte blocks), `k` (kilobytes), `M` (megabytes), `G` (gigabytes)

### Modification time filters
```bash
# Modified within last 7 days
ffind -mtime -7

# Older than 30 days
ffind -mtime +30

# Exactly 5 days old
ffind -mtime 5
```

### Combined filters
```bash
# Large log files
ffind -name "*.log" -size +10M -type f

# Recent source files with TODO comments
ffind -name "*.cpp" -mtime -7 -c "TODO"
```

## Features

- **Name matching**: Glob patterns for file names
- **Path matching**: Glob patterns for file paths relative to indexed root
- **Case sensitivity control**: Use `-i` flag for case-insensitive matching
- **Content search**: Search file contents with fixed strings or regex patterns (`-r`)
- **Type filtering**: Filter by file type (`-type f` for files, `-type d` for directories)
- **Size filtering**: Filter by file size with `+`, `-`, or exact match
- **Modification time**: Filter by modification time in days
- **Binary file detection**: Automatically skips binary files in content search
- **Real-time updates**: File system changes are automatically tracked

## License

ffind is dual-licensed:

- **GNU General Public License v3.0** (GPLv3) for open source use
- **Commercial License** available for proprietary use

For commercial licensing inquiries, contact EdgeOfAssembly.

## Project Structure

- `ffind-daemon.cpp` - Daemon implementation with inotify and indexing
- `ffind.cpp` - Client command-line interface
- `ffind.1` - Man page for ffind client
- `ffind-daemon.8` - Man page for ffind daemon
- `Makefile` - Build configuration
