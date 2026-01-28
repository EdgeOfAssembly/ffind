#!/bin/bash
set -e

# Comprehensive Benchmark script for ffind after RE2 optimization
# This script runs ALL required benchmarks (17+) with 3 runs each
#
# Setup Instructions:
# 1. Copy a large source tree to /tmp/test-corpus:
#    cp -r /usr/src/linux-headers-* /tmp/test-corpus
# 2. Build ffind: make clean && make
# 3. Run this script: ./benchmarks/run_comprehensive_benchmarks.sh
#
# Dependencies: bc (basic calculator)

CORPUS_DIR="/tmp/test-corpus"
FFIND_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_FILE="/tmp/benchmark_results_comprehensive.txt"

# Check if bc is installed
if ! command -v bc &> /dev/null; then
    echo "ERROR: 'bc' (basic calculator) is required but not installed"
    echo "Please install it: sudo apt-get install bc"
    exit 1
fi

# Check if test corpus exists
if [ ! -d "$CORPUS_DIR" ]; then
    echo "ERROR: Test corpus not found at $CORPUS_DIR"
    echo "Please run: cp -r /usr/src/linux-headers-* /tmp/test-corpus"
    exit 1
fi

# Check if ffind binaries exist
if [ ! -x "$FFIND_DIR/ffind" ] || [ ! -x "$FFIND_DIR/ffind-daemon" ]; then
    echo "ERROR: ffind binaries not found or not executable"
    echo "Please run 'make clean && make' in $FFIND_DIR first"
    exit 1
fi

cd "$FFIND_DIR"

# Get system info
echo "=========================================" | tee "$OUTPUT_FILE"
echo "COMPREHENSIVE RE2 BENCHMARK EXECUTION" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "Execution Date: $(date)" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "=== System Specifications ===" | tee -a "$OUTPUT_FILE"
CPU_MODEL=$(cat /proc/cpuinfo | grep "model name" | head -1 | cut -d: -f2 | xargs)
CPU_CORES=$(nproc)
RAM_TOTAL=$(free -h | grep "Mem:" | awk '{print $2}')
# Try to detect disk type, default to "Unknown"
if [ -b /dev/sda ]; then
    DISK_TYPE=$(lsblk -d -o name,rota | grep sda | awk '{if ($2 == "0") print "SSD"; else print "HDD"}')
else
    DISK_TYPE="Unknown"
fi
[ -z "$DISK_TYPE" ] && DISK_TYPE="Unknown"
# Detect filesystem type
FS_TYPE=$(df -T "$CORPUS_DIR" 2>/dev/null | tail -1 | awk '{print $2}' || echo "Unknown")
OS_VERSION=$(lsb_release -d 2>/dev/null | cut -f2 || echo "Unknown")
KERNEL_VERSION=$(uname -r)
echo "CPU: $CPU_MODEL ($CPU_CORES cores allocated)" | tee -a "$OUTPUT_FILE"
echo "RAM: $RAM_TOTAL" | tee -a "$OUTPUT_FILE"
echo "Disk: $DISK_TYPE ($FS_TYPE filesystem)" | tee -a "$OUTPUT_FILE"
echo "OS: $OS_VERSION" | tee -a "$OUTPUT_FILE"
echo "Kernel: $KERNEL_VERSION" | tee -a "$OUTPUT_FILE"
echo "GNU find: $(find --version | head -1 | cut -d' ' -f4)" | tee -a "$OUTPUT_FILE"
echo "GNU grep: $(grep --version | head -1 | cut -d' ' -f4)" | tee -a "$OUTPUT_FILE"

# Check for optional tools
if command -v rg &> /dev/null; then
    echo "ripgrep: $(rg --version | head -1 | cut -d' ' -f2)" | tee -a "$OUTPUT_FILE"
else
    echo "ripgrep: Not available" | tee -a "$OUTPUT_FILE"
fi
if command -v ag &> /dev/null; then
    echo "ag: $(ag --version | head -1 | cut -d' ' -f3)" | tee -a "$OUTPUT_FILE"
else
    echo "ag: Not available" | tee -a "$OUTPUT_FILE"
fi

echo "" | tee -a "$OUTPUT_FILE"

# Verify RE2 is linked
echo "=== RE2 Verification ===" | tee -a "$OUTPUT_FILE"
if ldd ffind | grep -q "libre2"; then
    echo "✅ ffind is linked with RE2: $(ldd ffind | grep libre2)" | tee -a "$OUTPUT_FILE"
else
    echo "❌ RE2 not linked!" | tee -a "$OUTPUT_FILE"
    exit 1
fi
if ldd ffind-daemon | grep -q "libre2"; then
    echo "✅ ffind-daemon is linked with RE2: $(ldd ffind-daemon | grep libre2)" | tee -a "$OUTPUT_FILE"
else
    echo "❌ RE2 not linked!" | tee -a "$OUTPUT_FILE"
    exit 1
fi
echo "" | tee -a "$OUTPUT_FILE"

# Test corpus info
echo "=== Test Corpus ===" | tee -a "$OUTPUT_FILE"
FILE_COUNT=$(find "$CORPUS_DIR" -type f 2>/dev/null | wc -l)
DIR_COUNT=$(find "$CORPUS_DIR" -type d 2>/dev/null | wc -l)
TOTAL_SIZE=$(du -sh "$CORPUS_DIR" 2>/dev/null | cut -f1)
echo "Location: $CORPUS_DIR" | tee -a "$OUTPUT_FILE"
echo "Files: $FILE_COUNT" | tee -a "$OUTPUT_FILE"
echo "Directories: $DIR_COUNT" | tee -a "$OUTPUT_FILE"
echo "Total Size: $TOTAL_SIZE" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Clean up any existing daemon
EXISTING_PID=$(ps aux | grep "[f]find-daemon" | awk '{print $2}' | head -1)
if [ -n "$EXISTING_PID" ]; then
    kill -9 $EXISTING_PID 2>/dev/null || true
fi
rm -f /run/user/$(id -u)/ffind.sock 2>/dev/null || true
sleep 1

# Set up trap to ensure daemon cleanup on script exit
cleanup() {
    if [ -n "$DAEMON_PID" ]; then
        echo "Cleaning up daemon (PID: $DAEMON_PID)..." >&2
        kill $DAEMON_PID 2>/dev/null || true
        sleep 1
        # Kill any remaining ffind-daemon processes
        REMAINING_PID=$(ps aux | grep "[f]find-daemon" | awk '{print $2}' | head -1)
        if [ -n "$REMAINING_PID" ]; then
            kill -9 $REMAINING_PID 2>/dev/null || true
        fi
        rm -f /tmp/ffind-daemon.log
    fi
}
trap cleanup INT TERM EXIT

# Start ffind-daemon and wait for indexing
echo "Starting ffind-daemon and indexing corpus..." | tee -a "$OUTPUT_FILE"
./ffind-daemon --foreground "$CORPUS_DIR" > /tmp/ffind-daemon.log 2>&1 &
DAEMON_PID=$!
echo "Daemon PID: $DAEMON_PID" | tee -a "$OUTPUT_FILE"

# Wait for daemon to be ready
sleep 2

# Wait for indexing to complete by checking if socket is ready
WAIT_COUNT=0
while [ ! -S "/run/user/$(id -u)/ffind.sock" ] && [ $WAIT_COUNT -lt 30 ]; do
    sleep 1
    WAIT_COUNT=$((WAIT_COUNT + 1))
done

if [ ! -S "/run/user/$(id -u)/ffind.sock" ]; then
    echo "ERROR: ffind-daemon socket not ready" | tee -a "$OUTPUT_FILE"
    kill $DAEMON_PID 2>/dev/null || true
    exit 1
fi

# Additional wait for indexing to complete
sleep 3
echo "✅ Indexing complete. Starting benchmarks..." | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Function to run a benchmark and capture timing (3 runs, median)
run_benchmark() {
    local name="$1"
    local cmd="$2"
    
    # Run the command 3 times
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
    
    # Return median and all times
    echo "${MEDIAN}|${TIMES[0]}|${TIMES[1]}|${TIMES[2]}"
}

echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "A. FILE METADATA SEARCHES" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 1: Find *.c files
echo "=== Benchmark 1: Find *.c files ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find (*.c)" "find '$CORPUS_DIR' -name '*.c'")
FIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find: ${FIND_TIME}s (runs: $(echo $FIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (*.c)" "./ffind '*.c'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster than find" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 2: Find *.h files
echo "=== Benchmark 2: Find *.h files ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find (*.h)" "find '$CORPUS_DIR' -name '*.h'")
FIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find: ${FIND_TIME}s (runs: $(echo $FIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (*.h)" "./ffind '*.h'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster than find" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 3: Find files >100KB
echo "=== Benchmark 3: Find files >100KB ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find (>100k)" "find '$CORPUS_DIR' -type f -size +100k")
FIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find: ${FIND_TIME}s (runs: $(echo $FIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (>100k)" "./ffind -type f -size +100k")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster than find" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 4: Find files >1MB
echo "=== Benchmark 4: Find files >1MB ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find (>1M)" "find '$CORPUS_DIR' -type f -size +1M")
FIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find: ${FIND_TIME}s (runs: $(echo $FIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (>1M)" "./ffind -type f -size +1M")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster than find" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 5: List all files
echo "=== Benchmark 5: List all files ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find (all files)" "find '$CORPUS_DIR' -type f")
FIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find: ${FIND_TIME}s (runs: $(echo $FIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (all files)" "./ffind -type f")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_TIME / $FFIND_TIME" | bc)
echo "  Speedup: ${SPEEDUP}x faster than find" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "B. CONTENT SEARCHES - FIXED STRING" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 6: Search for "static"
echo "=== Benchmark 6: Content search 'static' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -r (static)" "grep -r 'static' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -r: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

if command -v rg &> /dev/null; then
    RESULT=$(run_benchmark "rg (static)" "rg 'static' '$CORPUS_DIR'")
    RG_TIME=$(echo $RESULT | cut -d'|' -f1)
    RG_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
    echo "  ripgrep: ${RG_TIME}s (runs: $(echo $RG_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"
fi

RESULT=$(run_benchmark "ffind (static)" "./ffind -c 'static'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
if command -v rg &> /dev/null; then
    SPEEDUP_RG=$(echo "scale=2; $RG_TIME / $FFIND_TIME" | bc)
    echo "  Speedup vs ripgrep: ${SPEEDUP_RG}x" | tee -a "$OUTPUT_FILE"
fi
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 7: Search for "TODO"
echo "=== Benchmark 7: Content search 'TODO' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -r (TODO)" "grep -r 'TODO' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -r: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (TODO)" "./ffind -c 'TODO'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 8: Search for "error" (case-insensitive)
echo "=== Benchmark 8: Content search 'error' (case-insensitive) ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -ri (error)" "grep -ri 'error' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -ri: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (error -i)" "./ffind -c 'error' -i")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 9: Search for "include"
echo "=== Benchmark 9: Content search 'include' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -r (include)" "grep -r 'include' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -r: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (include)" "./ffind -c 'include'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "C. REGEX SEARCHES" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 10: Simple regex "TODO.*fix"
echo "=== Benchmark 10: Regex 'TODO.*fix' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -rE (TODO.*fix)" "grep -rE 'TODO.*fix' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -rE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (TODO.*fix -r)" "./ffind -c 'TODO.*fix' -r")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 11: Alternation "error|warning"
echo "=== Benchmark 11: Regex 'error|warning' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -rE (error|warning)" "grep -rE 'error|warning' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -rE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (error|warning -r)" "./ffind -c 'error|warning' -r")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 12: Complex pattern "EXPORT_SYMBOL|MODULE_"
echo "=== Benchmark 12: Regex 'EXPORT_SYMBOL|MODULE_' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -rE (EXPORT_SYMBOL|MODULE_)" "grep -rE 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -rE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

if command -v rg &> /dev/null; then
    RESULT=$(run_benchmark "rg (EXPORT_SYMBOL|MODULE_)" "rg 'EXPORT_SYMBOL|MODULE_' '$CORPUS_DIR'")
    RG_TIME=$(echo $RESULT | cut -d'|' -f1)
    RG_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
    echo "  ripgrep: ${RG_TIME}s (runs: $(echo $RG_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"
fi

RESULT=$(run_benchmark "ffind (EXPORT_SYMBOL|MODULE_ -r)" "./ffind -c 'EXPORT_SYMBOL|MODULE_' -r")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
if command -v rg &> /dev/null; then
    SPEEDUP_RG=$(echo "scale=2; $RG_TIME / $FFIND_TIME" | bc)
    echo "  Speedup vs ripgrep: ${SPEEDUP_RG}x" | tee -a "$OUTPUT_FILE"
fi
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 13: Line anchors "^#include"
echo "=== Benchmark 13: Regex '^#include' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -rE (^#include)" "grep -rE '^#include' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -rE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (^#include -r)" "./ffind -c '^#include' -r")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 14: Character classes "[0-9]+"
echo "=== Benchmark 14: Regex '[0-9]+' ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -rE ([0-9]+)" "grep -rE '[0-9]+' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -rE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind ([0-9]+ -r)" "./ffind -c '[0-9]+' -r")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 15: Case-insensitive regex "fixme|todo"
echo "=== Benchmark 15: Regex 'fixme|todo' (case-insensitive) ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep -riE (fixme|todo)" "grep -riE 'fixme|todo' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -riE: ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (fixme|todo -r -i)" "./ffind -c 'fixme|todo' -r -i")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "D. COMBINED SEARCHES" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 16: Search in .c files only
echo "=== Benchmark 16: Search 'static' in *.c files ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "grep --include (*.c)" "grep -r --include='*.c' 'static' '$CORPUS_DIR'")
GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  grep -r --include='*.c': ${GREP_TIME}s (runs: $(echo $GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (*.c + static)" "./ffind -name '*.c' -c 'static'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Benchmark 17: Search in large files (>50KB)
echo "=== Benchmark 17: Search 'error' in files >50KB ===" | tee -a "$OUTPUT_FILE"
RESULT=$(run_benchmark "find + grep (>50k)" "find '$CORPUS_DIR' -type f -size +50k -exec grep -l 'error' {} \;")
FIND_GREP_TIME=$(echo $RESULT | cut -d'|' -f1)
FIND_GREP_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  find + grep: ${FIND_GREP_TIME}s (runs: $(echo $FIND_GREP_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

RESULT=$(run_benchmark "ffind (>50k + error)" "./ffind -size +50k -c 'error'")
FFIND_TIME=$(echo $RESULT | cut -d'|' -f1)
FFIND_TIMES=$(echo $RESULT | cut -d'|' -f2,3,4)
echo "  ffind: ${FFIND_TIME}s (runs: $(echo $FFIND_TIMES | tr '|' ', ')s)" | tee -a "$OUTPUT_FILE"

SPEEDUP=$(echo "scale=2; $FIND_GREP_TIME / $FFIND_TIME" | bc)
echo "  Speedup vs find+grep: ${SPEEDUP}x" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

# Cleanup
echo "Cleaning up..." | tee -a "$OUTPUT_FILE"
kill $DAEMON_PID 2>/dev/null || true
sleep 1
# Kill any remaining ffind-daemon processes
REMAINING_PID=$(ps aux | grep "[f]find-daemon" | awk '{print $2}' | head -1)
if [ -n "$REMAINING_PID" ]; then
    kill -9 $REMAINING_PID 2>/dev/null || true
fi
rm -f /tmp/ffind-daemon.log

echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "BENCHMARKING COMPLETE!" | tee -a "$OUTPUT_FILE"
echo "=========================================" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"
echo "Full results saved to: $OUTPUT_FILE" | tee -a "$OUTPUT_FILE"
echo "" | tee -a "$OUTPUT_FILE"

echo "To reproduce these benchmarks:"
echo "  1. Set up test corpus: cp -r /usr/src/linux-headers-* /tmp/test-corpus"
echo "  2. Build ffind: make clean && make"
echo "  3. Run: ./benchmarks/run_comprehensive_benchmarks.sh"
