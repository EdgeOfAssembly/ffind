# ffind Benchmarks

This directory contains real-world benchmarking scripts for ffind performance testing.

## Quick Start

```bash
# 1. Set up test corpus
cp -r /usr/src/linux-headers-* /tmp/test-corpus

# 2. Build ffind (from repository root)
cd ..
make

# 3. Run benchmarks (will prompt for sudo password)
sudo ./benchmarks/run_real_benchmarks.sh
```

## Test Corpus Setup

The benchmark script expects a test corpus at `/tmp/test-corpus`. You have several options:

### Option 1: Use Local Linux Headers (Recommended for Quick Testing)

```bash
# Copy existing Linux kernel headers (~130MB, ~16K files)
cp -r /usr/src/linux-headers-* /tmp/test-corpus
```

### Option 2: Download GCC Source Code (Network Required)

**For larger corpus (~790MB):**
```bash
cd /tmp
wget http://ftp.gnu.org/gnu/gcc/gcc-9.5.0/gcc-9.5.0.tar.xz
tar -xf gcc-9.5.0.tar.xz
mv gcc-9.5.0 test-corpus
```

**For smaller corpus (~180MB):**
```bash
cd /tmp
wget http://ftp.gnu.org/gnu/gcc/gcc-3.4.6/gcc-3.4.6.tar.gz
tar -xzf gcc-3.4.6.tar.gz
mv gcc-3.4.6 test-corpus
```

### Option 3: Download Linux Kernel (Network Required)

```bash
cd /tmp
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.1.tar.xz
tar -xf linux-5.10.1.tar.xz
mv linux-5.10.1 test-corpus
```

### Option 4: Use Any Large Source Repository

Any C/C++/Java project with 10,000+ files will work:
```bash
cp -r ~/projects/large-codebase /tmp/test-corpus
```

## Benchmark Script

### What It Tests

The `run_real_benchmarks.sh` script performs the following benchmarks:

1. **File name searches** (*.c, *.h files)
2. **File metadata searches** (files >100KB)
3. **Path pattern searches** (include/* directory)
4. **Content searches** (fixed string: "static")
5. **Regex content searches** ("EXPORT_SYMBOL|MODULE_")
6. **File listing** (all files)

### Methodology

- Each benchmark runs **3 times**, median time reported
- Compares against standard tools: `find`, `grep`
- Optionally tests against `ag` and `ripgrep` if available
- All output redirected to `/dev/null` to measure pure execution time
- Uses real filesystem (no artificial test data)

### Output Format

```
=== Benchmark X: Description ===
  find: 0.067s
  ffind: 0.004s
  Speedup: 15.9x faster
```

## System Requirements

- Linux (for inotify filesystem monitoring)
- Bash shell
- bc (basic calculator) for speedup calculations
- At least 1GB free space in /tmp
- GNU find and grep (standard on most Linux systems)

## Performance Results

Current benchmark results (Linux kernel headers, 16,548 files):

| Operation | find/grep | ffind | Speedup |
|-----------|-----------|-------|---------|
| Find *.c files | 0.067s | 0.004s | **15.9x** |
| Find *.h files | 0.068s | 0.018s | **3.8x** |
| Files >100KB | 0.090s | 0.003s | **28.4x** |
| List all files | 0.063s | 0.025s | **2.5x** |

**Note:** Content search performance is currently slower than grep, but file/metadata searches show significant speedups.

See main [README.md](../README.md#performance-benchmarks) for complete benchmark results.

## Interpreting Results

### What to Expect

**ffind should be faster for:**
- File name searches (glob patterns)
- File metadata queries (size, mtime, type)
- Path-based searches
- Repeated queries on the same dataset

**ffind may be slower for:**
- Content searches (current implementation)
- Single-use queries where index building overhead isn't amortized

### Factors Affecting Performance

- **Corpus size:** Larger file trees show bigger speedups
- **Disk type:** SSD vs HDD affects initial indexing
- **Query type:** Indexed attributes (name, size) vs content search
- **System load:** Other processes competing for I/O

## Customizing Benchmarks

To test with different patterns or operations, edit `run_real_benchmarks.sh`:

```bash
# Add a new benchmark
echo "=== Benchmark X: My custom test ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "description" "find '$CORPUS_DIR' -name 'pattern'")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "description" "./ffind 'pattern'")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster"
```

## Troubleshooting

### "Test corpus not found"
```bash
# Verify corpus exists
ls -la /tmp/test-corpus
# If not, set it up (see options above)
```

### "ffind binaries not found"
```bash
# Build from repository root
cd /home/runner/work/ffind/ffind  # adjust to your path
make
```

### Benchmark hangs or times out
```bash
# Kill any existing daemon
ps aux | grep ffind-daemon
kill <PID>

# Remove socket
rm -f /run/user/$(id -u)/ffind.sock

# Try again
./benchmarks/run_real_benchmarks.sh
```

### Inconsistent results
- Clear filesystem caches between runs using the cache-flush utility (see below)
- Ensure no other heavy I/O processes are running
- Run benchmarks multiple times and average results

## Cache-Flush Utility

The `cache-flush` C binary provides a secure, minimal utility for clearing Linux filesystem caches:

**Build and Setup (one-time):**
```bash
# Build the binary
make cache-flush
```

**Usage:**
```bash
# Flush caches (requires sudo)
sudo ./cache-flush
```

**Security Model:**
- **Why sudo is required**: Linux requires `CAP_SYS_ADMIN` capability to write to `/proc/sys/vm/drop_caches`
- **Security benefit**: Only 55 lines of auditable C code run with elevated privileges
- **Alternative**: You could use `sudo setcap cap_sys_admin+ep cache-flush` to avoid sudo prompts, but this grants permanent elevated privileges to the binary
- **Our choice**: Require explicit `sudo` each time for better security control

**Features:**
- **Security best practice**: Only 55 lines of C code with elevated privileges
- **Explicit privilege control**: Requires sudo each time (no permanent capabilities)
- **Portable**: Simple C with no external dependencies
- **Easy to audit**: Small, focused code that's easy to security review

**Manual usage:**
```bash
# Flush caches before a benchmark
sudo ./cache-flush
time find /usr -name "*.so"

# Compare warm vs cold cache
time find /usr -name "*.so"  # Warm cache
sudo ./cache-flush
time find /usr -name "*.so"  # Cold cache
```

**Technical details:**
- Calls `sync()` to flush dirty pages to disk
- Writes "3" to `/proc/sys/vm/drop_caches` to clear page cache, dentries, and inodes
- Requires `CAP_SYS_ADMIN` capability or root privileges (Linux security requirement)
- Returns 0 on success, 1 on error

The benchmark script (`run_real_benchmarks.sh`) automatically uses this utility with sudo when running benchmarks.

## Contributing

Found a performance issue or have optimization suggestions? Please:
1. Run benchmarks with your changes
2. Include before/after results in PR description
3. Document test environment and corpus used

## License

Same as main ffind project (GPL v3 / Commercial dual-license).
