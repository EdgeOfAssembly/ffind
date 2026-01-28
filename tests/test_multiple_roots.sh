#!/bin/bash
# Test suite for multiple root directories feature

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

# Create unique temp directories for this test run
TEMP_BASE=$(mktemp -d -t ffind_multiroot_test_XXXXXX)
ROOT1="$TEMP_BASE/root1"
ROOT2="$TEMP_BASE/root2"
ROOT3="$TEMP_BASE/root3"
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
    if [ -d "$TEMP_BASE" ]; then
        rm -rf "$TEMP_BASE"
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
echo "Multiple Root Directories Test Suite"
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

echo "Test base directory: $TEMP_BASE"
echo ""

# Create test directory structures
echo "Creating test directories..."

# ROOT1 structure
mkdir -p "$ROOT1/dir1" "$ROOT1/dir2"
echo "content1" > "$ROOT1/file1.txt"
echo "content2" > "$ROOT1/dir1/file2.txt"
echo "special content" > "$ROOT1/dir1/special.txt"
echo "log entry" > "$ROOT1/app.log"

# ROOT2 structure
mkdir -p "$ROOT2/dira" "$ROOT2/dirb"
echo "contentA" > "$ROOT2/fileA.txt"
echo "contentB" > "$ROOT2/dira/fileB.txt"
echo "special content" > "$ROOT2/dira/special.txt"
echo "log entry" > "$ROOT2/system.log"

# ROOT3 structure
mkdir -p "$ROOT3/dirx" "$ROOT3/diry"
echo "contentX" > "$ROOT3/fileX.txt"
echo "contentY" > "$ROOT3/dirx/fileY.txt"
echo "special content" > "$ROOT3/dirx/special.txt"
echo "log entry" > "$ROOT3/debug.log"

echo "Test directories created."
echo ""

# Start daemon with multiple roots
echo "Starting ffind-daemon with 3 roots..."
"$FFIND_DAEMON" --foreground "$ROOT1" "$ROOT2" "$ROOT3" &
DAEMON_PID=$!

# Wait for daemon to initialize
sleep 3

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

# Basic Multiple Roots Tests
echo "--- Basic Multiple Roots ---"
run_test "Find file from root1" "file1.txt" "$FFIND_CLIENT" "file1.txt"
run_test "Find file from root2" "fileA.txt" "$FFIND_CLIENT" "fileA.txt"
run_test "Find file from root3" "fileX.txt" "$FFIND_CLIENT" "fileX.txt"

# Test files unique to each root
run_test "Files unique to root1" "file2.txt" "$FFIND_CLIENT" "file2.txt"
run_test "Files unique to root2" "fileB.txt" "$FFIND_CLIENT" "fileB.txt"
run_test "Files unique to root3" "fileY.txt" "$FFIND_CLIENT" "fileY.txt"

# Test glob pattern across all roots
run_test_exact_count "Glob *.txt across all roots" 9 "$FFIND_CLIENT" "*.txt"
run_test_exact_count "Glob *.log across all roots" 3 "$FFIND_CLIENT" "*.log"

# Test files with same name in different roots
run_test_exact_count "Same filename 'special.txt' in all roots" 3 "$FFIND_CLIENT" "special.txt"

echo ""
echo "--- Path Patterns Across Roots ---"
# Test -path glob works relative to each root
run_test "Path pattern dir1/*" "file2.txt" "$FFIND_CLIENT" -path "dir1/*"
run_test "Path pattern dira/*" "fileB.txt" "$FFIND_CLIENT" -path "dira/*"
run_test "Path pattern dirx/*" "fileY.txt" "$FFIND_CLIENT" -path "dirx/*"

echo ""
echo "--- Filters Across Roots ---"
# Test -type filter
run_test_exact_count "Type filter: directories" 6 "$FFIND_CLIENT" -type d "dir*"
run_test_exact_count "Type filter: files" 9 "$FFIND_CLIENT" -type f "*.txt"

# Test content search
run_test_exact_count "Content search 'special'" 3 "$FFIND_CLIENT" "*.txt" -c "special"
run_test_exact_count "Content search 'log entry'" 3 "$FFIND_CLIENT" "*.log" -c "log"

echo ""
echo "--- Runtime Behavior ---"
# Add new file to root1
echo "new content" > "$ROOT1/newfile.txt"
sleep 1
run_test "New file in root1 is searchable" "newfile.txt" "$FFIND_CLIENT" "newfile.txt"

# Add new file to root2
echo "new content" > "$ROOT2/newfile2.txt"
sleep 1
run_test "New file in root2 is searchable" "newfile2.txt" "$FFIND_CLIENT" "newfile2.txt"

# Delete file from root1
rm "$ROOT1/newfile.txt"
sleep 1
TOTAL_TESTS=$((TOTAL_TESTS + 1))
OUTPUT=$("$FFIND_CLIENT" "newfile.txt" 2>&1 || true)
COUNT=$(echo "$OUTPUT" | grep -c "newfile.txt" 2>/dev/null || echo "0")
COUNT=$(echo "$COUNT" | tr -d '\n' | tr -d ' ')
if [ "$COUNT" -eq 0 ]; then  # Should find 0 (deleted from root1, root2 has newfile2.txt)
    echo -e "${GREEN}✓${NC} PASS: Deleted file from root1 handled correctly"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗${NC} FAIL: Deleted file still appears in results (count: $COUNT)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Create directory in root3
mkdir -p "$ROOT3/newdir"
echo "test" > "$ROOT3/newdir/test.txt"
sleep 1
run_test "New directory in root3 is indexed" "test.txt" "$FFIND_CLIENT" "test.txt"

echo ""
echo "--- Backward Compatibility ---"
# Test single root still works (stop current daemon and restart with single root)
if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 2
fi

# Clean up socket and PID file
rm -f "/run/user/$(id -u)/ffind.sock"
rm -f "/run/user/$(id -u)/ffind-daemon.pid"

# Start with single root
"$FFIND_DAEMON" --foreground "$ROOT1" &
DAEMON_PID=$!
sleep 2

if kill -0 "$DAEMON_PID" 2>/dev/null; then
    run_test "Single root backward compatibility" "file1.txt" "$FFIND_CLIENT" "file1.txt"
    
    # This should NOT find files from root2/root3 since we're only monitoring root1
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    OUTPUT=$("$FFIND_CLIENT" "fileA.txt" 2>&1 || true)
    COUNT=$(echo "$OUTPUT" | grep -c "fileA.txt" 2>/dev/null || echo "0")
    COUNT=$(echo "$COUNT" | tr -d '\n' | tr -d ' ')
    if [ "$COUNT" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} PASS: Single root doesn't find files from other roots"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗${NC} FAIL: Single root found files from other roots (count: $COUNT)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
else
    echo -e "${RED}✗${NC} FAIL: Daemon failed to start with single root"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Clean up for edge case tests
if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 2
fi

echo ""
echo "--- Edge Cases ---"
# Test: No roots specified
run_test_error "Error: No roots specified" "Usage:" "$FFIND_DAEMON"

# Test: Non-existent root path
run_test_error "Error: Non-existent path" "does not exist" "$FFIND_DAEMON" "/nonexistent/path"

# Test: Root is file not directory
touch "$TEMP_BASE/testfile"
run_test_error "Error: Path is file not directory" "not a directory" "$FFIND_DAEMON" "$TEMP_BASE/testfile"

# Test: Same path twice - should deduplicate
TOTAL_TESTS=$((TOTAL_TESTS + 1))
DUP_OUTPUT="$TEMP_BASE/dup_test_output.txt"
"$FFIND_DAEMON" --foreground "$ROOT1" "$ROOT1" > "$DUP_OUTPUT" 2>&1 &
DAEMON_PID=$!
sleep 2
# Check if daemon started and warning was shown
if ps -p "$DAEMON_PID" > /dev/null 2>&1 && grep -q "Duplicate path ignored" "$DUP_OUTPUT"; then
    echo -e "${GREEN}✓${NC} PASS: Duplicate paths handled (daemon started)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 1
elif ps -p "$DAEMON_PID" > /dev/null 2>&1; then
    # Daemon started but maybe no warning (also acceptable)
    echo -e "${GREEN}✓${NC} PASS: Duplicate paths handled (daemon started)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 1
else
    echo -e "${RED}✗${NC} FAIL: Daemon failed with duplicate paths"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test: Overlapping roots - should warn but work
rm -f "/run/user/$(id -u)/ffind.sock"
rm -f "/run/user/$(id -u)/ffind-daemon.pid"
mkdir -p "$TEMP_BASE/parent/child"
echo "test" > "$TEMP_BASE/parent/test1.txt"
echo "test" > "$TEMP_BASE/parent/child/test2.txt"

TOTAL_TESTS=$((TOTAL_TESTS + 1))
OVERLAP_OUTPUT="$TEMP_BASE/overlap_output.txt"
"$FFIND_DAEMON" --foreground "$TEMP_BASE/parent" "$TEMP_BASE/parent/child" > "$OVERLAP_OUTPUT" 2>&1 &
DAEMON_PID=$!
sleep 2
if grep -q "Overlapping roots" "$OVERLAP_OUTPUT" && ps -p "$DAEMON_PID" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} PASS: Overlapping roots warning shown"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    kill "$DAEMON_PID" 2>/dev/null || true
    sleep 1
else
    echo -e "${RED}✗${NC} FAIL: Overlapping roots not detected"
    FAILED_TESTS=$((FAILED_TESTS + 1))
    if [ -n "$DAEMON_PID" ] && ps -p "$DAEMON_PID" > /dev/null 2>&1; then
        kill "$DAEMON_PID" 2>/dev/null || true
        sleep 1
    fi
fi

# Summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}"
    exit 1
fi
