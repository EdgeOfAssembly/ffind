# ffind TODO Summary - January 2026

## Implemented (current version)
- Daemon/client split (ffind-daemon + ffind)
- Foreground mode (--foreground)
- Real-time indexing with inotify
- Basename glob (-name or positional, case-sensitive default)
- Full-path relative glob (-path, case-insensitive with -i)
- Case-insensitive mode (-i) for name/path/content
- Content search (-c fixed string substring)
- Regex content search (-c + -r, ±i)
- Content glob (-g fnmatch pattern on each line, ±i)
- Binary file skip (null-byte check)
- Type filter (-type f|d)
- Size filter (-size +N/-N/N with units c/b/k/M/G, find-style)
- Mtime filter (-mtime +N/-N/N days, find-style)
- All filters combinable (AND logic)
- Legacy single-arg name glob support
- Color output (--color=auto/always/never)
- Context lines (-A/-B/-C for grep-like before/after/context)
- Better directory rename handling (cookie-based inotify rename tracking with path updates)
- Multiple root directories support

## Remaining / Not Implemented
- PID file creation (/run/ffind.pid or similar) for service management
- Persistence (binary dump/load on shutdown/start for faster init)
- SQLite backend option (for huge trees, lower RAM)

## Low Priority / Optional
- -perm mode filter
- Invert match (-v)
- Line numbers only (-l)
- Full ECMAScript regex extensions if needed