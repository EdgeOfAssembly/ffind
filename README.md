# ffind

A fast, daemon-based file search tool with real-time indexing via inotify.

## Why ffind?

Traditional file search tools like `find` perform recursive directory traversals on every search, which can be slow on large filesystems. `ffind` solves this by:

- **Instant searches**: Maintains a persistent in-memory index of your filesystem
- **Real-time updates**: Uses inotify to track file system changes automatically
- **Powerful filtering**: Supports glob patterns, regex, content search, and multiple filters
- **Low overhead**: Daemon runs in the background, indexing once and updating incrementally

Instead of waiting seconds or minutes for recursive searches, ffind provides instant results by querying its pre-built index.

## Requirements

- **Operating System**: Linux (requires inotify support)
- **Compiler**: g++ with C++20 support
- **Libraries**: pthread (typically included with g++)

## Building

To build ffind from source:

```bash
make
```

This produces two executables:
- `ffind-daemon` - The background daemon that indexes and monitors the filesystem
- `ffind` - The client tool for searching

## Installation

To install system-wide:

```bash
sudo make install
```

This installs:
- Binaries to `/usr/local/bin/`
- Man pages to `/usr/local/share/man/`

## Running

### Starting the Daemon

Before using `ffind`, you must start the daemon with a root directory to monitor:

**Background mode (daemonizes):**
```bash
ffind-daemon /path/to/root
```

**Foreground mode (useful for debugging):**
```bash
ffind-daemon --foreground /path/to/root
```

The daemon creates a Unix domain socket at:
```
/run/user/$UID/ffind.sock
```

The client (`ffind`) communicates with the daemon through this socket.

## Usage Examples

### Basic Glob Search

Search for files by basename pattern (legacy single-argument form):
```bash
ffind "*.cpp"
```

### Name Glob (Explicit)

Search for files matching a basename pattern:
```bash
ffind -name "*.h"
```

### Path Glob

Search for files matching a full path pattern (relative to root):
```bash
ffind -path "src/*" -type f
```

Match nested paths:
```bash
ffind -path "**/include/*.h"
```

### Case Insensitive Search

Search without case sensitivity:
```bash
ffind -name "readme*" -i
```

Case insensitive path matching:
```bash
ffind -path "*/docs/*" -i
```

### Content Search

Search for files containing a specific string (fixed substring):
```bash
ffind -c "TODO"
```

Output format shows `path:lineno:line` for each match:
```
src/main.cpp:42:    // TODO: fix this bug
src/utils.cpp:15:    // TODO: optimize
```

### Regex Content Search

Search file contents using regular expressions:
```bash
ffind -c "TODO.*fix" -r
```

Case-insensitive regex:
```bash
ffind -c "error" -r -i
```

### Type Filters

Find only files:
```bash
ffind -type f
```

Find only directories:
```bash
ffind -type d
```

### Size Filters

Files larger than 1 megabyte:
```bash
ffind -size +1M
```

Files smaller than 100 kilobytes:
```bash
ffind -size -100k
```

Exactly 512 bytes:
```bash
ffind -size 512c
```

Supported size units:
- `c` - bytes
- `b` - 512-byte blocks
- `k` - kilobytes (1024 bytes)
- `M` - megabytes (1024*1024 bytes)
- `G` - gigabytes (1024*1024*1024 bytes)

### Modification Time Filters

Files modified in the last 7 days:
```bash
ffind -mtime -7
```

Files modified more than 30 days ago:
```bash
ffind -mtime +30
```

Files modified exactly N days ago:
```bash
ffind -mtime 5
```

### Combined Filters

All filters can be combined with AND logic:

Large log files modified recently:
```bash
ffind -name "*.log" -size +10M -mtime +7 -type f
```

Content search with name filter (case insensitive):
```bash
ffind -c "FIXME" -name "*.cpp" -i
```

Complex query with multiple filters:
```bash
ffind -path "src/**/*.cpp" -c "deprecated" -r -size +1k -mtime -30 -type f
```

## Features

- **Real-time indexing**: Changes to the filesystem are tracked automatically via inotify
- **Glob patterns**: fnmatch-style wildcards for name and path matching
- **Regular expressions**: Full regex support for content search
- **Multiple filters**: Combine name, path, content, type, size, and mtime filters
- **Binary file detection**: Automatically skips binary files in content searches
- **Case sensitivity control**: Optional case-insensitive matching for all search types
- **Efficient**: In-memory index provides instant search results

## Limitations

- Single root directory per daemon instance
- Directory renames may require daemon restart for optimal behavior
- No persistence between daemon restarts (re-indexes on start)

## License

ffind is dual-licensed:

- **GNU General Public License v3.0 (GPLv3)** - for open source use
- **Commercial License** - for proprietary/commercial use without GPL restrictions

For commercial licensing inquiries, please contact the author.

## Author

**EdgeOfAssembly**
- GitHub: [@EdgeOfAssembly](https://github.com/EdgeOfAssembly)

## Contributing

This project is maintained by EdgeOfAssembly. For bug reports or feature requests, please open an issue on GitHub.
