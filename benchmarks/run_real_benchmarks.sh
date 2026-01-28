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

CORPUS_DIR="/tmp/test-corpus"
FFIND_DIR="/home/runner/work/ffind/ffind"

# Check if test corpus exists
if [ ! -d "$CORPUS_DIR" ]; then
    echo "ERROR: Test corpus not found at $CORPUS_DIR"
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
FILE_COUNT=$(find "$CORPUS_DIR" -type f | wc -l)
DIR_COUNT=$(find "$CORPUS_DIR" -type d | wc -l)
TOTAL_SIZE=$(du -sh "$CORPUS_DIR" | cut -f1)

echo "========================================="
echo "Test corpus: $CORPUS_DIR"
echo "Files: $FILE_COUNT"
echo "Directories: $DIR_COUNT"
echo "Total size: $TOTAL_SIZE"
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

# Function to run a benchmark and capture timing
run_benchmark() {
    local name="$1"
    local cmd="$2"
    
    # Run the command 3 times and take the median
    TIMES=()
    for i in 1 2 3; do
        START=$(date +%s.%N)
        eval "$cmd" > /dev/null 2>&1 || true
        END=$(date +%s.%N)
        ELAPSED=$(echo "$END - $START" | bc)
        TIMES+=($ELAPSED)
    done
    
    # Sort and get median
    IFS=$'\n' SORTED=($(sort -n <<<"${TIMES[*]}"))
    MEDIAN=${SORTED[1]}
    echo "$MEDIAN"
}

# Benchmark 1: Find all .c files
echo "=== Benchmark 1: Find all .c files ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "find (*.c)" "find '$CORPUS_DIR' -name '*.c'")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (*.c)" "./ffind '*.c'")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster"
echo ""

# Benchmark 2: Find all .h files
echo "=== Benchmark 2: Find all .h files ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "find (*.h)" "find '$CORPUS_DIR' -name '*.h'")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (*.h)" "./ffind '*.h'")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster"
echo ""

# Benchmark 3: Find files by path pattern
echo "=== Benchmark 3: Find files in include/* ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "find (include/*)" "find '$CORPUS_DIR/include' -type f 2>/dev/null")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (include/*)" "./ffind -path 'include/*' -type f")
echo "${FFIND_TIME}s"
if [ "$(echo "$FIND_TIME > 0" | bc)" -eq 1 ]; then
    SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
    echo "  Speedup: ${SPEEDUP}x faster"
else
    echo "  (path not found in corpus)"
fi
echo ""

# Benchmark 4: Find large files (>100KB)
echo "=== Benchmark 4: Find files >100KB ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "find (>100k)" "find '$CORPUS_DIR' -type f -size +100k")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (>100k)" "./ffind -type f -size +100k")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster"
echo ""

# Benchmark 5: Content search for "static"
echo "=== Benchmark 5: Content search 'static' ==="
echo -n "  grep -r: "
GREP_TIME=$(run_benchmark "grep -r (static)" "grep -r 'static' '$CORPUS_DIR'")
echo "${GREP_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (static)" "./ffind -c 'static'")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x"
echo ""

# Check if ag is available
if command -v ag &> /dev/null; then
    echo "=== Benchmark 5b: ag search 'static' ==="
    echo -n "  ag: "
    AG_TIME=$(run_benchmark "ag (static)" "ag 'static' '$CORPUS_DIR'")
    echo "${AG_TIME}s"
    SPEEDUP=$(echo "scale=1; $AG_TIME / $FFIND_TIME" | bc)
    echo "  Speedup vs ag: ${SPEEDUP}x"
    echo ""
fi

# Check if ripgrep is available
if command -v rg &> /dev/null; then
    echo "=== Benchmark 5c: ripgrep search 'static' ==="
    echo -n "  ripgrep: "
    RG_TIME=$(run_benchmark "rg (static)" "rg 'static' '$CORPUS_DIR'")
    echo "${RG_TIME}s"
    SPEEDUP=$(echo "scale=1; $RG_TIME / $FFIND_TIME" | bc)
    echo "  Speedup vs ripgrep: ${SPEEDUP}x"
    echo ""
fi

# Benchmark 6: Regex content search
echo "=== Benchmark 6: Regex search 'EXPORT_SYMBOL|MODULE_' ==="
echo -n "  grep -rE: "
GREP_TIME=$(run_benchmark "grep -rE (regex)" "grep -rE 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'")
echo "${GREP_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (regex)" "./ffind -c 'EXPORT_SYMBOL|MODULE_' -r")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x"
echo ""

if command -v rg &> /dev/null; then
    echo "=== Benchmark 6b: ripgrep regex search ==="
    echo -n "  ripgrep: "
    RG_TIME=$(run_benchmark "rg (regex)" "rg 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'")
    echo "${RG_TIME}s"
    SPEEDUP=$(echo "scale=1; $RG_TIME / $FFIND_TIME" | bc)
    echo "  Speedup vs ripgrep: ${SPEEDUP}x"
    echo ""
fi

# Benchmark 7: Simple file listing
echo "=== Benchmark 7: List all files ==="
echo -n "  find: "
FIND_TIME=$(run_benchmark "find (all files)" "find '$CORPUS_DIR' -type f")
echo "${FIND_TIME}s"
echo -n "  ffind: "
FFIND_TIME=$(run_benchmark "ffind (all files)" "./ffind -type f")
echo "${FFIND_TIME}s"
SPEEDUP=$(echo "scale=1; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster"
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
rm -f /tmp/ffind-daemon.log

echo "========================================="
echo "Benchmarking complete!"
echo "========================================="
