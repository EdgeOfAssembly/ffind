#!/bin/bash
set -e

# Benchmark script for ffind performance testing
# This script runs real benchmarks against actual source code
#
# Setup Instructions:
# 1. Copy a large source tree to /tmp/test-corpus, for example:
#    cp -r /usr/src/linux-headers-* /tmp/test-corpus
#    OR download GCC source (if network available):
#    cd /tmp && wget http://ftp.gnu.org/gnu/gcc/gcc-9.5.0/gcc-9.5.0.tar.xz
#    tar -xf gcc-9.5.0.tar.xz && mv gcc-9.5.0 test-corpus
# 2. Build ffind: make
# 3. Run this script: ./benchmarks/run_real_benchmarks.sh
#    For fair benchmarks with cache flushing: sudo ./benchmarks/run_real_benchmarks.sh

CORPUS_DIR="/tmp/test-corpus"
FFIND_DIR="${FFIND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

# Check if we have sudo privileges (required for cache flushing)
CAN_FLUSH_CACHE=false
if [ "$EUID" -eq 0 ] || sudo -n true 2>/dev/null; then
    CAN_FLUSH_CACHE=true
fi

# Function to flush filesystem cache (Linux only)
flush_cache() {
    if [ "$CAN_FLUSH_CACHE" = true ]; then
        # Check if we're on Linux (drop_caches only works on Linux)
        if [ ! -f /proc/sys/vm/drop_caches ]; then
            return 0  # Skip cache flushing on non-Linux systems
        fi
        
        sync
        if [ "$EUID" -eq 0 ]; then
            echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
        else
            sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || true
        fi
        sleep 0.5
    fi
}

# Check if test corpus exists
if [ ! -d "$CORPUS_DIR" ]; then
    echo "ERROR: Test corpus not found at $CORPUS_DIR"
    exit 1
fi

# Check if required dependencies exist
if ! command -v bc &> /dev/null; then
    echo "ERROR: 'bc' is required but not installed. Please install it first."
    exit 1
fi

# Check if ffind binaries exist
if [ ! -x "$FFIND_DIR/ffind" ] || [ ! -x "$FFIND_DIR/ffind-daemon" ]; then
    echo "ERROR: ffind binaries not found or not executable"
    echo "Please run 'make' in $FFIND_DIR first"
    exit 1
fi

cd "$FFIND_DIR"

# Count files and directories
echo "Analyzing test corpus..."
FILE_COUNT=$(find "$CORPUS_DIR" -type f 2>/dev/null | wc -l)
DIR_COUNT=$(find "$CORPUS_DIR" -type d 2>/dev/null | wc -l)
TOTAL_SIZE=$(du -sh "$CORPUS_DIR" 2>/dev/null | cut -f1)

# Get system information
CPU_INFO=$(lscpu 2>/dev/null | grep "Model name" | cut -d: -f2 | xargs || echo "Unknown")
CORE_COUNT=$(nproc 2>/dev/null || echo "Unknown")
RAM_INFO=$(free -h 2>/dev/null | grep Mem | awk '{print $2}' || echo "Unknown")

echo "========================================="
echo "BENCHMARK METHODOLOGY"
echo "========================================="
echo ""
if [ "$CAN_FLUSH_CACHE" = true ]; then
    echo "Cache Flushing: ENABLED (fair comparison)"
    echo "  - Before each find/grep: Cache cleared (cold start)"
    echo "  - Before each ffind: No flush (data in daemon RAM)"
    echo "  - This simulates: find reads from disk, ffind from memory"
else
    echo "Cache Flushing: DISABLED"
    echo "  ⚠️  Run with sudo for fair benchmarks: sudo $0"
    echo "  ⚠️  Without cache flushing, results may favor find/grep"
    echo "  ⚠️  ffind runs first and warms the cache for find/grep"
fi
echo ""
echo "Runs per benchmark: 3"
echo "Statistical method: Median (reduces outlier impact)"
echo ""
echo "System Information:"
echo "  CPU: $CPU_INFO"
echo "  Cores: $CORE_COUNT"
echo "  RAM: $RAM_INFO"
echo ""
echo "Test Corpus: $CORPUS_DIR"
echo "  Files: $FILE_COUNT"
echo "  Directories: $DIR_COUNT"
echo "  Total size: $TOTAL_SIZE"
echo "========================================="
echo ""

# Clean up any existing daemon
EXISTING_PID=$(ps aux | grep "[f]find-daemon" | awk '{print $2}' | head -1)
if [ -n "$EXISTING_PID" ]; then
    kill -9 $EXISTING_PID 2>/dev/null || true
fi
rm -f /run/user/$(id -u)/ffind.sock 2>/dev/null || true
sleep 1

# Start ffind-daemon and wait for indexing
echo "Starting ffind-daemon and indexing corpus..."
./ffind-daemon --foreground "$CORPUS_DIR" > /tmp/ffind-daemon.log 2>&1 &
DAEMON_PID=$!
echo "Daemon PID: $DAEMON_PID"

# Wait for "Daemon ready" message
WAIT_COUNT=0
while [ $WAIT_COUNT -lt 120 ]; do
    if grep -q "Daemon ready" /tmp/ffind-daemon.log 2>/dev/null; then
        echo "Daemon is ready!"
        sleep 2  # Extra safety margin
        break
    fi
    
    if ! ps -p $DAEMON_PID > /dev/null; then
        echo "ERROR: Daemon process died"
        echo "=== Daemon log ==="
        cat /tmp/ffind-daemon.log
        exit 1
    fi
    
    sleep 1
    WAIT_COUNT=$((WAIT_COUNT + 1))
done

# Check if we timed out
if [ $WAIT_COUNT -eq 120 ]; then
    echo "ERROR: Daemon didn't become ready in 120 seconds"
    echo "=== Last 50 lines of daemon log ==="
    tail -50 /tmp/ffind-daemon.log
    kill $DAEMON_PID 2>/dev/null || true
    exit 1
fi

# Verify socket exists
if [ ! -S "/run/user/$(id -u)/ffind.sock" ]; then
    echo "ERROR: Socket file not found even though daemon reported ready"
    ls -la /run/user/$(id -u)/
    exit 1
fi

echo "Indexing complete. Starting benchmarks..."
echo ""

# Warmup run (discarded)
echo "Performing warmup run..."
# Warm up common query types used in benchmarks: extension, type, path, size, content, regex
./ffind "*.c" > /dev/null 2>&1 || true
./ffind "*.h" > /dev/null 2>&1 || true
./ffind -type f > /dev/null 2>&1 || true
./ffind -path "*/include/*" > /dev/null 2>&1 || true
./ffind -size +100k > /dev/null 2>&1 || true
./ffind -c "static" > /dev/null 2>&1 || true
./ffind -c "EXPORT_SYMBOL" -r > /dev/null 2>&1 || true
echo "Warmup complete."
echo ""

# Function to run a benchmark and capture timing with statistics
run_benchmark() {
    local cmd="$1"
    local should_flush="$2"  # "yes" or "no"
    
    # Run the command 3 times and collect times
    local TIMES=()
    local i
    for i in 1 2 3; do
        # Flush cache before run if requested
        if [ "$should_flush" = "yes" ]; then
            flush_cache
        fi
        
        local START=$(date +%s.%N)
        # Use mktemp for secure temporary file creation
        local tmpfile
        tmpfile=$(mktemp -t bench_output_${i}_XXXXXX)
        eval "$cmd" > "$tmpfile" 2>&1 || true
        local END=$(date +%s.%N)
        rm -f "$tmpfile"
        
        local ELAPSED=$(echo "$END - $START" | bc 2>/dev/null || echo "0")
        
        # Guard against negative elapsed times (e.g., due to system clock adjustments)
        if [ "$(echo "$ELAPSED < 0" | bc 2>/dev/null || echo "0")" -eq 1 ]; then
            ELAPSED=0
        fi
        TIMES+=("$ELAPSED")
    done
    
    # Sort and get median, min, max
    local SORTED
    IFS=$'\n' SORTED=($(sort -n <<<"${TIMES[*]}"))
    local MIN=${SORTED[0]}
    local MEDIAN=${SORTED[1]}
    local MAX=${SORTED[2]}
    local RANGE=$(echo "$MAX - $MIN" | bc 2>/dev/null || echo "0")
    
    # Check for high range (>20% of median)
    local RANGE_PCT=0
    if [ -n "$MEDIAN" ] && [ "$(echo "$MEDIAN > 0" | bc 2>/dev/null || echo "0")" -eq 1 ]; then
        RANGE_PCT=$(echo "scale=2; ($RANGE / $MEDIAN) * 100" | bc 2>/dev/null || echo "0")
    fi
    
    local HIGH_RANGE=0
    if [ -n "$RANGE_PCT" ] && [ "$(echo "$RANGE_PCT > 20" | bc 2>/dev/null || echo "0")" -eq 1 ]; then
        HIGH_RANGE=1
    fi
    
    # Output to stderr so it doesn't interfere with return value
    echo "    Run 1: ${TIMES[0]}s" >&2
    echo "    Run 2: ${TIMES[1]}s" >&2
    echo "    Run 3: ${TIMES[2]}s" >&2
    echo "    Median: ${MEDIAN}s" >&2
    echo "    Min: ${MIN}s, Max: ${MAX}s, Range: ${RANGE}s" >&2
    
    if [ "$HIGH_RANGE" -eq 1 ]; then
        echo "    ⚠️  High range (${RANGE_PCT}%) - results may be unreliable" >&2
    fi
    
    # Return the median value (to stdout for capture)
    echo "$MEDIAN"
}

# Function to verify result consistency
verify_results() {
    local find_cmd="$1"
    local ffind_cmd="$2"
    
    # Create secure temporary files
    local find_results=$(mktemp /tmp/ffind_bench_find.XXXXXX)
    local ffind_results=$(mktemp /tmp/ffind_bench_ffind.XXXXXX)
    
    # Run commands and compare output
    eval "$find_cmd" 2>/dev/null | sort > "$find_results" || true
    eval "$ffind_cmd" 2>/dev/null | sort > "$ffind_results" || true
    
    if [ -s "$find_results" ] && [ -s "$ffind_results" ]; then
        if ! diff -q "$find_results" "$ffind_results" > /dev/null 2>&1; then
            echo "  ⚠️  WARNING: Results differ between find and ffind!"
            echo "  First 20 differences:"
            diff "$find_results" "$ffind_results" 2>/dev/null | head -20 || true
        fi
    fi
    
    rm -f "$find_results" "$ffind_results"
}

# Function to calculate speedup safely
calculate_speedup() {
    local time1="$1"
    local time2="$2"
    
    # Check if time2 is zero or very close to zero
    if [ "$(echo "$time2 > 0.0001" | bc 2>/dev/null || echo "0")" -eq 1 ]; then
        echo "scale=1; $time1 / $time2" | bc 2>/dev/null || echo "0"
    else
        echo "N/A (time too small)"
    fi
}

# Benchmark 1: Find all .c files
echo "=== Benchmark 1: Find all .c files ==="
echo "  find (cold cache):"
FIND_TIME=$(run_benchmark "find '$CORPUS_DIR' -name '*.c'" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind '*.c'" "no")
SPEEDUP=$(calculate_speedup "$FIND_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup: ${SPEEDUP}x faster"
verify_results "find '$CORPUS_DIR' -name '*.c'" "./ffind '*.c'"
echo ""

# Benchmark 2: Find all .h files
echo "=== Benchmark 2: Find all .h files ==="
echo "  find (cold cache):"
FIND_TIME=$(run_benchmark "find '$CORPUS_DIR' -name '*.h'" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind '*.h'" "no")
SPEEDUP=$(calculate_speedup "$FIND_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup: ${SPEEDUP}x faster"
verify_results "find '$CORPUS_DIR' -name '*.h'" "./ffind '*.h'"
echo ""

# Benchmark 3: Find files by path pattern
echo "=== Benchmark 3: Find files in include/* ==="
echo "  find (cold cache):"
FIND_TIME=$(run_benchmark "find '$CORPUS_DIR/include' -type f 2>/dev/null" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind -path 'include/*' -type f" "no")
echo ""
if [ "$(echo "$FIND_TIME > 0" | bc 2>/dev/null || echo "0")" -eq 1 ]; then
    SPEEDUP=$(calculate_speedup "$FIND_TIME" "$FFIND_TIME")
    echo "  Speedup: ${SPEEDUP}x faster"
else
    echo "  (path not found in corpus)"
fi
echo ""

# Benchmark 4: Find large files (>100KB)
echo "=== Benchmark 4: Find files >100KB ==="
echo "  find (cold cache):"
FIND_TIME=$(run_benchmark "find '$CORPUS_DIR' -type f -size +100k" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind -type f -size +100k" "no")
SPEEDUP=$(calculate_speedup "$FIND_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup: ${SPEEDUP}x faster"
echo ""

# Benchmark 5: Content search for "static"
echo "=== Benchmark 5: Content search 'static' ==="
echo "  grep -r (cold cache):"
GREP_TIME=$(run_benchmark "grep -r 'static' '$CORPUS_DIR'" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind -c 'static'" "no")
SPEEDUP=$(calculate_speedup "$GREP_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup vs grep: ${SPEEDUP}x"
echo ""

# Check if ag is available
if command -v ag &> /dev/null; then
    echo "=== Benchmark 5b: ag search 'static' ==="
    echo "  ag (cold cache):"
    AG_TIME=$(run_benchmark "ag 'static' '$CORPUS_DIR'" "yes")
    SPEEDUP=$(calculate_speedup "$AG_TIME" "$FFIND_TIME")
    echo ""
    echo "  Speedup vs ag: ${SPEEDUP}x"
    echo ""
fi

# Check if ripgrep is available
if command -v rg &> /dev/null; then
    echo "=== Benchmark 5c: ripgrep search 'static' ==="
    echo "  ripgrep (cold cache):"
    RG_TIME=$(run_benchmark "rg 'static' '$CORPUS_DIR'" "yes")
    SPEEDUP=$(calculate_speedup "$RG_TIME" "$FFIND_TIME")
    echo ""
    echo "  Speedup vs ripgrep: ${SPEEDUP}x"
    echo ""
fi

# Benchmark 6: Regex content search
echo "=== Benchmark 6: Regex search 'EXPORT_SYMBOL|MODULE_' ==="
echo "  grep -rE (cold cache):"
GREP_TIME=$(run_benchmark "grep -rE 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind -c 'EXPORT_SYMBOL|MODULE_' -r" "no")
SPEEDUP=$(calculate_speedup "$GREP_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup vs grep: ${SPEEDUP}x"
echo ""

if command -v rg &> /dev/null; then
    echo "=== Benchmark 6b: ripgrep regex search ==="
    echo "  ripgrep (cold cache):"
    RG_TIME=$(run_benchmark "rg 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'" "yes")
    SPEEDUP=$(calculate_speedup "$RG_TIME" "$FFIND_TIME")
    echo ""
    echo "  Speedup vs ripgrep: ${SPEEDUP}x"
    echo ""
fi

# Benchmark 7: Simple file listing
echo "=== Benchmark 7: List all files ==="
echo "  find (cold cache):"
FIND_TIME=$(run_benchmark "find '$CORPUS_DIR' -type f" "yes")
echo ""
echo "  ffind (warm - data in RAM):"
FFIND_TIME=$(run_benchmark "./ffind -type f" "no")
SPEEDUP=$(calculate_speedup "$FIND_TIME" "$FFIND_TIME")
echo ""
echo "  Speedup: ${SPEEDUP}x faster"
verify_results "find '$CORPUS_DIR' -type f" "./ffind -type f"
echo ""

# Cleanup
echo "Cleaning up..."
kill $DAEMON_PID 2>/dev/null || true
sleep 1
# Kill any remaining ffind-daemon processes
REMAINING_PID=$(ps aux | grep "[f]find-daemon" | awk '{print $2}' | head -1)
if [ -n "$REMAINING_PID" ]; then
    kill -9 $REMAINING_PID 2>/dev/null || true
fi

# Safely remove benchmark temporary files in /tmp
# Note: mktemp files from verify_results are already cleaned up within the function
# Only daemon log needs cleanup here (securely created temp files don't use wildcards)
if [ -f /tmp/ffind-daemon.log ]; then
    rm -f /tmp/ffind-daemon.log
fi

echo "========================================="
echo "Benchmarking complete!"
if [ "$CAN_FLUSH_CACHE" = false ]; then
    echo ""
    echo "Note: Benchmarks ran without cache flushing."
    echo "For fair comparison, run with: sudo $0"
fi
echo "========================================="
