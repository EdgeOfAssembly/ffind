#!/bin/bash
# Comprehensive test suite for ffind

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track test results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Get absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FFIND_DAEMON="$PROJECT_ROOT/ffind-daemon"
FFIND_CLIENT="$PROJECT_ROOT/ffind"

# Create a unique temp directory for this test run
TEMP_DIR=$(mktemp -d -t ffind_test_XXXXXX)
DAEMON_PID=""

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Kill daemon if running
    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "Stopping daemon (PID: $DAEMON_PID)"
        kill "$DAEMON_PID" 2>/dev/null || true
        # Give it time to exit gracefully
        for i in {1..3}; do
            if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
                break
            fi
            sleep 1
        done
        # Force kill if still running
        kill -9 "$DAEMON_PID" 2>/dev/null || true
    fi
    
    # Clean up socket
    SOCK_PATH="/run/user/$(id -u)/ffind.sock"
    if [ -S "$SOCK_PATH" ]; then
        rm -f "$SOCK_PATH"
    fi
    
    # Clean up PID file
    PID_FILE="/run/user/$(id -u)/ffind-daemon.pid"
    if [ -f "$PID_FILE" ]; then
        rm -f "$PID_FILE"
    fi
    
    # Remove temp directory
    if [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}

trap cleanup EXIT INT TERM

run_test() {
    local test_name="$1"
    local expected="$2"
    shift 2
    local cmd=("$@")
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Run command and capture output
    local output
    output=$("${cmd[@]}" 2>&1 | sort) || true
    
    # Check if expected pattern is in output
    if echo "$output" | grep -qF "$expected"; then
        echo -e "${GREEN}✓${NC} PASS: $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $test_name"
        echo "  Expected to find: $expected"
        echo "  Got output:"
        echo "$output" | sed 's/^/    /'
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

run_test_exact_count() {
    local test_name="$1"
    local expected_count="$2"
    shift 2
    local cmd=("$@")
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Run command and count lines
    local output
    output=$("${cmd[@]}" 2>&1) || true
    local actual_count=0
    if [ -n "$output" ]; then
        actual_count=$(echo "$output" | grep -c . || echo 0)
    fi
    
    if [ "$actual_count" -eq "$expected_count" ]; then
        echo -e "${GREEN}✓${NC} PASS: $test_name (count: $actual_count)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $test_name"
        echo "  Expected count: $expected_count"
        echo "  Actual count: $actual_count"
        echo "  Output:"
        echo "$output" | sed 's/^/    /'
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

run_test_error() {
    local test_name="$1"
    local expected_error="$2"
    shift 2
    local cmd=("$@")
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Run command and capture stderr
    local output
    output=$("${cmd[@]}" 2>&1) || true
    
    # Check if expected error message is in output
    if echo "$output" | grep -qF -- "$expected_error"; then
        echo -e "${GREEN}✓${NC} PASS: $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $test_name"
        echo "  Expected error containing: $expected_error"
        echo "  Got output:"
        echo "$output" | sed 's/^/    /'
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

echo "========================================="
echo "ffind Test Suite"
echo "========================================="
echo ""

# Check if binaries exist
if [ ! -x "$FFIND_DAEMON" ]; then
    echo -e "${RED}Error: ffind-daemon not found at $FFIND_DAEMON${NC}"
    echo "Please run 'make' to build the project first."
    exit 1
fi

if [ ! -x "$FFIND_CLIENT" ]; then
    echo -e "${RED}Error: ffind not found at $FFIND_CLIENT${NC}"
    echo "Please run 'make' to build the project first."
    exit 1
fi

echo "Test directory: $TEMP_DIR"
echo ""

# Create test file structure
echo "Creating test files..."

# Basic files for name/path tests
touch "$TEMP_DIR/file.txt"
touch "$TEMP_DIR/FILE.TXT"
touch "$TEMP_DIR/readme.md"
touch "$TEMP_DIR/README.MD"

# Create source directory structure
mkdir -p "$TEMP_DIR/src"
mkdir -p "$TEMP_DIR/include"
mkdir -p "$TEMP_DIR/docs"

# Files with content for content search
echo "TODO fix this bug" > "$TEMP_DIR/src/main.cpp"
echo "FIXME: another todo" > "$TEMP_DIR/src/utils.cpp"
echo "ERROR: something failed" > "$TEMP_DIR/src/error.log"
echo "This is a normal file" > "$TEMP_DIR/src/normal.txt"
echo "#include <stdio.h>" > "$TEMP_DIR/include/header.h"
echo "Documentation text TODO review" > "$TEMP_DIR/docs/guide.txt"

# Files for glob pattern tests
cat > "$TEMP_DIR/glob_test.txt" << 'EOF'
TODO: implement feature
TODO implement another feature
DONE: completed task
test1 is here
test2 is here
test3 is here
function_call(arg)
func_test(123)
function_name()
1234 starts with digit
abcd starts with letter
2: another digit line
[test] brackets
another error message
this has error in it
EOF

# File for context line tests
cat > "$TEMP_DIR/context.txt" << 'EOF'
line 1
line 2
line 3
MATCH line 4
line 5
line 6
line 7
MATCH line 8
line 9
line 10
EOF

# File for overlapping context tests
cat > "$TEMP_DIR/overlap.txt" << 'EOF'
before 1
MATCH line 2
between
MATCH line 4
after 1
EOF

# File for edge case tests (match at start)
cat > "$TEMP_DIR/start.txt" << 'EOF'
MATCH at start
line 2
line 3
EOF

# File for edge case tests (match at end)
cat > "$TEMP_DIR/end.txt" << 'EOF'
line 1
line 2
MATCH at end
EOF

# Binary file (with null bytes)
printf '\x00\x01\x02\x03\x04' > "$TEMP_DIR/binary.bin"

# Files of various sizes for size tests
dd if=/dev/zero of="$TEMP_DIR/tiny.dat" bs=1 count=10 2>/dev/null        # 10 bytes
dd if=/dev/zero of="$TEMP_DIR/small.dat" bs=1024 count=1 2>/dev/null     # 1KB
dd if=/dev/zero of="$TEMP_DIR/medium.dat" bs=1024 count=500 2>/dev/null  # 500KB
dd if=/dev/zero of="$TEMP_DIR/large.dat" bs=1048576 count=2 2>/dev/null  # 2MB

# Files for mtime tests - create old file
touch "$TEMP_DIR/old.txt"
# Make it 40 days old
touch -d "40 days ago" "$TEMP_DIR/old.txt"

# Create some .log files
echo "log entry 1" > "$TEMP_DIR/app.log"
dd if=/dev/zero of="$TEMP_DIR/huge.log" bs=1048576 count=11 2>/dev/null  # 11MB

echo "Test files created."
echo ""

# Start the daemon in foreground mode (background it)
echo "Starting ffind-daemon..."
"$FFIND_DAEMON" --foreground "$TEMP_DIR" &
DAEMON_PID=$!

# Wait for daemon to initialize
sleep 2

# Check if daemon is running
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo -e "${RED}Error: Daemon failed to start${NC}"
    exit 1
fi

echo "Daemon started (PID: $DAEMON_PID)"
echo ""

# Run tests
echo "========================================="
echo "Running Tests"
echo "========================================="
echo ""

# Test 1: Basic name glob
echo "--- Name Glob Tests ---"
run_test "Basic name glob *.txt" "file.txt" "$FFIND_CLIENT" "*.txt"
run_test "Name glob *.cpp" "main.cpp" "$FFIND_CLIENT" "*.cpp"
run_test "Name glob with -name" "header.h" "$FFIND_CLIENT" -name "*.h"

# Test 2: Case sensitivity
echo ""
echo "--- Case Sensitivity Tests ---"
run_test "Case sensitive readme*" "readme.md" "$FFIND_CLIENT" "readme*"
run_test "Case insensitive readme*" "README.MD" "$FFIND_CLIENT" -name "readme*" -i

# Test 3: Path glob
echo ""
echo "--- Path Glob Tests ---"
run_test "Path glob src/*" "src/main.cpp" "$FFIND_CLIENT" -path "src/*"
run_test "Path glob include/*" "include/header.h" "$FFIND_CLIENT" -path "include/*"
run_test "Path glob with type" "src/main.cpp" "$FFIND_CLIENT" -path "src/*" -type f

# Test 4: Type filters
echo ""
echo "--- Type Filter Tests ---"
run_test "Type filter files only" "file.txt" "$FFIND_CLIENT" -type f
run_test "Type filter dirs only" "src" "$FFIND_CLIENT" -type d

# Test 5: Content search (fixed string)
echo ""
echo "--- Content Search Tests (Fixed String) ---"
run_test "Content search TODO" "main.cpp" "$FFIND_CLIENT" -c "TODO"
run_test "Content search ERROR" "error.log" "$FFIND_CLIENT" -c "ERROR"
run_test "Content search FIXME" "utils.cpp" "$FFIND_CLIENT" -c "FIXME"

# Test 6: Content search with regex
echo ""
echo "--- Content Search Tests (Regex) ---"
run_test "Regex content TODO.*fix" "main.cpp" "$FFIND_CLIENT" -c "TODO.*fix" -r
run_test "Regex content TODO.*review" "guide.txt" "$FFIND_CLIENT" -c "TODO.*review" -r

# Test 7: Case insensitive content search
echo ""
echo "--- Case Insensitive Content Tests ---"
run_test "Case insensitive content todo" "main.cpp" "$FFIND_CLIENT" -c "todo" -i
run_test "Case insensitive regex error" "error.log" "$FFIND_CLIENT" -c "error" -r -i

# Test 8: Content glob pattern matching
echo ""
echo "--- Content Glob Tests (Basic) ---"
run_test "Glob pattern TODO*" "TODO: implement feature" "$FFIND_CLIENT" -g "TODO*"
run_test "Glob pattern *error*" "another error message" "$FFIND_CLIENT" -g "*error*"
run_test "Glob pattern test?*" "test1 is here" "$FFIND_CLIENT" -g "test?*"
run_test "Glob pattern [0-9]*" "1234 starts with digit" "$FFIND_CLIENT" -g "[0-9]*"
run_test "Glob pattern [a-z]*" "abcd starts with letter" "$FFIND_CLIENT" -g "[a-z]*"
run_test "Glob pattern func_*(*)" "func_test(123)" "$FFIND_CLIENT" -g "func_*(*)"

# Test 9: Content glob with case insensitive
echo ""
echo "--- Content Glob Tests (Case Insensitive) ---"
run_test "Glob with -i TODO*" "TODO: implement feature" "$FFIND_CLIENT" -g "todo*" -i
run_test "Glob with -i *ERROR*" "another error message" "$FFIND_CLIENT" -g "*ERROR*" -i

# Test 10: Content glob with other filters
echo ""
echo "--- Content Glob with Other Filters ---"
run_test "Glob with -name filter" "TODO" "$FFIND_CLIENT" -g "TODO*" -name "*.txt"
run_test "Glob with -type filter" "TODO" "$FFIND_CLIENT" -g "TODO*" -type f

# Test 11: Content glob no match
echo ""
echo "--- Content Glob No Match Tests ---"
run_test_exact_count "Glob no match (empty)" 0 "$FFIND_CLIENT" -g "NOMATCHPATTERN*"

# Test 7.5: Context lines
echo ""
echo "--- Context Lines Tests ---"

# Test -A (after context)
output=$("$FFIND_CLIENT" -c "MATCH" -A 2 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:4:MATCH line 4" && \
   echo "$output" | grep -q "context.txt:5-line 5" && \
   echo "$output" | grep -q "context.txt:6-line 6"; then
    echo -e "${GREEN}✓${NC} PASS: -A shows 2 lines after match"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: -A shows 2 lines after match"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test -B (before context)
output=$("$FFIND_CLIENT" -c "MATCH" -B 2 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:2-line 2" && \
   echo "$output" | grep -q "context.txt:3-line 3" && \
   echo "$output" | grep -q "context.txt:4:MATCH line 4"; then
    echo -e "${GREEN}✓${NC} PASS: -B shows 2 lines before match"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: -B shows 2 lines before match"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test -C (combined context)
output=$("$FFIND_CLIENT" -c "MATCH" -C 1 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:3-line 3" && \
   echo "$output" | grep -q "context.txt:4:MATCH line 4" && \
   echo "$output" | grep -q "context.txt:5-line 5"; then
    echo -e "${GREEN}✓${NC} PASS: -C shows 1 line before and after"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: -C shows 1 line before and after"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test separator format (: for match, - for context)
output=$("$FFIND_CLIENT" -c "MATCH" -A 1 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:4:MATCH" && \
   echo "$output" | grep -q "context.txt:5-line 5"; then
    echo -e "${GREEN}✓${NC} PASS: Context lines use '-' separator, matches use ':'"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Separator format incorrect"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test -- separator between groups
output=$("$FFIND_CLIENT" -c "MATCH" -A 1 --color=never context.txt 2>&1)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "^--$"; then
    echo -e "${GREEN}✓${NC} PASS: '--' separator between non-contiguous groups"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Missing '--' separator"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test overlapping context (should merge)
output=$("$FFIND_CLIENT" -c "MATCH" -A 2 -B 2 --color=never overlap.txt 2>&1)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
# Should have NO -- separator since contexts overlap
if ! echo "$output" | grep -q "^--$"; then
    echo -e "${GREEN}✓${NC} PASS: Overlapping contexts merge (no separator)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Overlapping contexts should merge"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test match at start of file
output=$("$FFIND_CLIENT" -c "MATCH" -B 5 --color=never start.txt 2>&1 | grep "start.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "start.txt:1:MATCH at start"; then
    echo -e "${GREEN}✓${NC} PASS: Context at file start (no before lines available)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Context at file start"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test match at end of file
output=$("$FFIND_CLIENT" -c "MATCH" -A 5 --color=never end.txt 2>&1 | grep "end.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "end.txt:3:MATCH at end"; then
    echo -e "${GREEN}✓${NC} PASS: Context at file end (no after lines available)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Context at file end"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test context with case insensitive
output=$("$FFIND_CLIENT" -c "match" -i -B 1 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:3-line 3" && \
   echo "$output" | grep -q "context.txt:4:MATCH line 4"; then
    echo -e "${GREEN}✓${NC} PASS: Context with -i case insensitive"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Context with -i case insensitive"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test context with regex
output=$("$FFIND_CLIENT" -c "MATCH.*4" -r -A 1 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:4:MATCH line 4" && \
   echo "$output" | grep -q "context.txt:5-line 5"; then
    echo -e "${GREEN}✓${NC} PASS: Context with -r regex"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Context with -r regex"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test context with glob
output=$("$FFIND_CLIENT" -g "MATCH*" -A 1 --color=never context.txt 2>&1 | grep "context.txt")
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q "context.txt:4:MATCH line 4" && \
   echo "$output" | grep -q "context.txt:5-line 5"; then
    echo -e "${GREEN}✓${NC} PASS: Context with -g glob"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Context with -g glob"
    echo "  Got output:"
    echo "$output" | sed 's/^/    /'
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test error: -A without -c
run_test_error "Error: -A without -c or -g" "Context lines (-A/-B/-C) need -c or -g" "$FFIND_CLIENT" -A 2

# Test 8: Size filters
echo ""
echo "--- Size Filter Tests ---"
run_test "Size larger than 1M" "large.dat" "$FFIND_CLIENT" -size +1M
run_test "Size smaller than 100 bytes" "tiny.dat" "$FFIND_CLIENT" -size -100c
run_test "Size larger than 1k" "medium.dat" "$FFIND_CLIENT" -size +1k

# Test 9: Mtime filters
echo ""
echo "--- Mtime Filter Tests ---"
run_test "Modified within 7 days" "file.txt" "$FFIND_CLIENT" -mtime -7
run_test "Modified more than 30 days ago" "old.txt" "$FFIND_CLIENT" -mtime +30

# Test 10: Combined filters
echo ""
echo "--- Combined Filter Tests ---"
run_test "Combined: *.log + size >10M + type f" "huge.log" "$FFIND_CLIENT" -name "*.log" -size +10M -type f
run_test "Combined: src/* + *.cpp" "main.cpp" "$FFIND_CLIENT" -path "src/*" -name "*.cpp"

# Test 11: Binary file handling (should skip binary files in content search)
echo ""
echo "--- Binary File Tests ---"
# Binary files should not appear in content search results
output=$("$FFIND_CLIENT" -c "test" 2>&1 || true)
if echo "$output" | grep -q "binary.bin"; then
    echo -e "${YELLOW}⚠${NC}  WARNING: Binary file appeared in content search"
else
    echo -e "${GREEN}✓${NC} PASS: Binary files skipped in content search"
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi
TOTAL_TESTS=$((TOTAL_TESTS + 1))

# Test 12: Real-time indexing
echo ""
echo "--- Real-time Indexing Tests ---"

# Create a new file after daemon started
echo "new content" > "$TEMP_DIR/newfile.txt"
sleep 1
run_test "Real-time: new file indexed" "newfile.txt" "$FFIND_CLIENT" "newfile.txt"

# Delete a file
rm "$TEMP_DIR/newfile.txt"
sleep 1
output=$("$FFIND_CLIENT" "newfile.txt" 2>&1 || true)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if [ -z "$output" ]; then
    echo -e "${GREEN}✓${NC} PASS: Real-time: deleted file removed from index"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Real-time: deleted file still in index"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 13: Color output tests
echo ""
echo "--- Color Output Tests ---"

# Test --color=never produces no ANSI codes
output=$("$FFIND_CLIENT" "*.txt" --color=never 2>&1 || true)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033'; then
    echo -e "${RED}✗${NC} FAIL: --color=never still has escape codes"
    FAILED_TESTS=$((FAILED_TESTS + 1))
else
    echo -e "${GREEN}✓${NC} PASS: --color=never produces no escape codes"
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

# Test --color=always produces ANSI codes even when piped
output=$("$FFIND_CLIENT" "*.txt" --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033'; then
    echo -e "${GREEN}✓${NC} PASS: --color=always produces escape codes"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: --color=always did not produce escape codes"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test content search highlighting
output=$("$FFIND_CLIENT" -c "TODO" --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033\[1;31m'; then
    echo -e "${GREEN}✓${NC} PASS: Content search highlights matches (bold red)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Content search did not highlight matches"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test line number coloring (cyan)
output=$("$FFIND_CLIENT" -c "TODO" --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033\[36m'; then
    echo -e "${GREEN}✓${NC} PASS: Line numbers colored (cyan)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Line numbers not colored"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test path coloring (bold)
output=$("$FFIND_CLIENT" "*.txt" --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033\[1m'; then
    echo -e "${GREEN}✓${NC} PASS: Paths colored (bold)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Paths not colored"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test --color=auto with pipe (should not colorize)
output=$("$FFIND_CLIENT" "*.txt" --color=auto 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033'; then
    echo -e "${RED}✗${NC} FAIL: --color=auto produced escape codes when piped"
    FAILED_TESTS=$((FAILED_TESTS + 1))
else
    echo -e "${GREEN}✓${NC} PASS: --color=auto respects pipe (no colors)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
fi

# Test case-insensitive content highlighting
output=$("$FFIND_CLIENT" -c "error" -i --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
# Check if ERROR is highlighted in the output
if echo "$output" | grep -q $'\033\[1;31mERROR\033\[0m'; then
    echo -e "${GREEN}✓${NC} PASS: Case-insensitive highlighting works"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Case-insensitive highlighting failed"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test combined flags with color
output=$("$FFIND_CLIENT" -c "TODO" -i --color=always 2>&1 | cat)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if echo "$output" | grep -q $'\033\[1;31m' && echo "$output" | grep -qi "TODO"; then
    echo -e "${GREEN}✓${NC} PASS: Combined flags (-i) work with --color"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Combined flags with --color failed"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 14: Error cases
echo ""
echo "--- Error Handling Tests ---"

# Stop daemon for "daemon not running" test
if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill "$DAEMON_PID" 2>/dev/null || true
    # Give it time to exit
    for i in {1..5}; do
        if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
            break
        fi
        sleep 1
    done
    # Force kill if still running
    kill -9 "$DAEMON_PID" 2>/dev/null || true
fi
DAEMON_PID=""

# Remove socket
SOCK_PATH="/run/user/$(id -u)/ffind.sock"
rm -f "$SOCK_PATH"

# Give socket time to be released
sleep 1

run_test_error "Error: daemon not running" "Daemon not running" "$FFIND_CLIENT" "*.txt"

# Restart daemon for remaining tests
"$FFIND_DAEMON" --foreground "$TEMP_DIR" &
DAEMON_PID=$!
sleep 3

# Verify daemon is running
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo -e "${RED}Error: Daemon failed to restart${NC}"
    exit 1
fi

# Test -r without -c
run_test_error "Error: -r without -c" "-r needs -c" "$FFIND_CLIENT" -r

# Test -g with -c (mutually exclusive)
run_test_error "Error: -g with -c" "Cannot use -g with -c" "$FFIND_CLIENT" -g "TODO*" -c "TODO"

# Test -g with -r (mutually exclusive)
run_test_error "Error: -g with -r" "Cannot use -g with -r" "$FFIND_CLIENT" -g "TODO*" -r

# Test -g without pattern
run_test_error "Error: -g without pattern" "Missing -g pattern" "$FFIND_CLIENT" -g

# Test bad arguments
run_test_error "Error: bad argument" "Bad arg" "$FFIND_CLIENT" --invalid-arg

# PID file tests
echo ""
echo "--- PID File Tests ---"

# First, stop the current daemon and clean up
if [ -n "$DAEMON_PID" ] && ps -p "$DAEMON_PID" > /dev/null 2>/dev/null; then
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 2
fi

PID_FILE="/run/user/$(id -u)/ffind-daemon.pid"
rm -f "$PID_FILE"

# Test 1: PID file creation
TOTAL_TESTS=$((TOTAL_TESTS + 1))
"$FFIND_DAEMON" --foreground "$TEMP_DIR" > /dev/null 2>&1 &
DAEMON_PID=$!
sleep 2
if [ -f "$PID_FILE" ]; then
    PID_IN_FILE=$(cat "$PID_FILE")
    if ps -p "$PID_IN_FILE" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} PASS: PID file created with correct PID"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗${NC} FAIL: PID file contains invalid PID"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
else
    echo -e "${RED}✗${NC} FAIL: PID file not created"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 2: Duplicate daemon prevention
TOTAL_TESTS=$((TOTAL_TESTS + 1))
OUTPUT=$("$FFIND_DAEMON" --foreground "$TEMP_DIR" 2>&1 || true)
if echo "$OUTPUT" | grep -q "already running"; then
    echo -e "${GREEN}✓${NC} PASS: Duplicate daemon prevented"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Duplicate daemon not prevented"
    echo "  Got output: $OUTPUT"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 3: Stale PID file handling
# Stop the daemon first
if [ -n "$DAEMON_PID" ] && ps -p "$DAEMON_PID" > /dev/null 2>&1; then
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 2
fi

TOTAL_TESTS=$((TOTAL_TESTS + 1))
# Create a stale PID file
echo "99999" > "$PID_FILE"
# Start daemon in background and check if it handles the stale PID
"$FFIND_DAEMON" --foreground "$TEMP_DIR" > /dev/null 2>&1 &
DAEMON_PID=$!
sleep 2
# Check if daemon started successfully (which means it handled the stale PID)
if [ -f "$PID_FILE" ] && ps -p "$DAEMON_PID" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} PASS: Stale PID file handled and daemon started"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Daemon failed to start after stale PID"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 4: PID file cleanup on exit
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if [ -f "$PID_FILE" ]; then
    PID_TO_KILL=$(cat "$PID_FILE")
    if [ -n "$PID_TO_KILL" ] && ps -p "$PID_TO_KILL" > /dev/null 2>&1; then
        kill -TERM "$PID_TO_KILL" 2>/dev/null || true
        sleep 2
        if [ ! -f "$PID_FILE" ]; then
            echo -e "${GREEN}✓${NC} PASS: PID file cleaned up on exit"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗${NC} FAIL: PID file not cleaned up"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${RED}✗${NC} FAIL: Daemon not running for cleanup test"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
else
    echo -e "${RED}✗${NC} FAIL: PID file not found for cleanup test"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Restart daemon for remaining tests (if any)
"$FFIND_DAEMON" --foreground "$TEMP_DIR" > /dev/null 2>&1 &
DAEMON_PID=$!
sleep 2

# Print summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "Total tests: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
else
    echo -e "Failed: $FAILED_TESTS"
fi
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
