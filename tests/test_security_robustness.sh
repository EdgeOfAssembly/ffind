#!/bin/bash
# Test suite for security and robustness features

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
TEMP_DIR=$(mktemp -d -t ffind_security_test_XXXXXX)
DAEMON_PID=""

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Kill daemon if running
    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "Stopping daemon (PID: $DAEMON_PID)"
        kill "$DAEMON_PID" 2>/dev/null || true
        sleep 2
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
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "\n${YELLOW}Test:${NC} $test_name"
}

pass_test() {
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
}

fail_test() {
    local reason="$1"
    echo -e "${RED}✗ FAIL${NC}: $reason"
    FAILED_TESTS=$((FAILED_TESTS + 1))
}

# Check binaries exist
if [ ! -f "$FFIND_DAEMON" ]; then
    echo -e "${RED}Error: ffind-daemon not found at $FFIND_DAEMON${NC}"
    exit 1
fi

if [ ! -f "$FFIND_CLIENT" ]; then
    echo -e "${RED}Error: ffind not found at $FFIND_CLIENT${NC}"
    exit 1
fi

echo "====================================="
echo "Security & Robustness Test Suite"
echo "====================================="

# Test 1: Size parsing with valid values
run_test "Size parsing - valid values"
cd "$TEMP_DIR"
mkdir -p test_size
echo "test" > test_size/file1.txt
echo "larger content here" > test_size/file2.txt

# Start daemon
timeout 10 $FFIND_DAEMON --foreground "$TEMP_DIR" > /tmp/daemon_size_log.txt 2>&1 &
DAEMON_PID=$!
sleep 2

if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    fail_test "Daemon failed to start"
else
    # Test valid size queries (look for any file)
    if timeout 2 $FFIND_CLIENT -size +0c "file1.txt" 2>&1 | grep -q "file1.txt"; then
        pass_test
    else
        fail_test "Size query failed"
    fi
fi

kill -TERM "$DAEMON_PID" 2>/dev/null || true
sleep 1
kill -9 "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
sleep 1

# Test 2: Size parsing with overflow detection
run_test "Size parsing - overflow detection"
output=$($FFIND_CLIENT -size 9999999999999G . 2>&1 || true)
if echo "$output" | grep -q "too large\|out of range"; then
    pass_test
else
    fail_test "Did not detect size overflow"
fi

# Test 3: Size parsing with invalid input
run_test "Size parsing - invalid input"
output=$($FFIND_CLIENT -size xyz . 2>&1 || true)
if echo "$output" | grep -q "Invalid size\|out of range"; then
    pass_test
else
    fail_test "Did not detect invalid size input"
fi

# Test 4: Socket creation error handling
run_test "Socket creation error handling (basic check)"
# This test just verifies the binary has the error checking code path
# We can't easily simulate socket() failure without special setup
if strings "$FFIND_CLIENT" | grep -q "Failed to create socket"; then
    pass_test
else
    fail_test "Socket error handling message not found in binary"
fi

# Test 5: Connection limiting - verify code exists
run_test "Connection limiting - verify implementation"
if grep -q "MAX_CONCURRENT_CLIENTS" "$PROJECT_ROOT/ffind-daemon.cpp" && \
   grep -q "active_connections" "$PROJECT_ROOT/ffind-daemon.cpp" && \
   grep -q "Connection limit reached" "$PROJECT_ROOT/ffind-daemon.cpp"; then
    pass_test
else
    fail_test "Connection limiting implementation not found"
fi

# Test 6: EINTR handling (verify code exists)
run_test "EINTR handling in read loop (code verification)"
if grep -q "errno == EINTR" "$PROJECT_ROOT/ffind.cpp"; then
    pass_test
else
    fail_test "EINTR handling not found in client code"
fi

# Test 7: RAII wrapper verification
run_test "RAII wrapper for file descriptors (code verification)"
if grep -q "ScopedFd" "$PROJECT_ROOT/ffind-daemon.cpp"; then
    pass_test
else
    fail_test "ScopedFd class not found in daemon code"
fi

# Test 8: Verify no bits/stdc++.h
run_test "Portability - no bits/stdc++.h"
if ! grep -q "#include <bits/stdc++.h>" "$PROJECT_ROOT/ffind.cpp" && \
   ! grep -q "#include <bits/stdc++.h>" "$PROJECT_ROOT/ffind-daemon.cpp"; then
    pass_test
else
    fail_test "Found non-portable bits/stdc++.h include"
fi

# Test 9: Verify explicit includes
run_test "Portability - explicit standard headers"
if grep -q "#include <iostream>" "$PROJECT_ROOT/ffind.cpp" && \
   grep -q "#include <vector>" "$PROJECT_ROOT/ffind-daemon.cpp"; then
    pass_test
else
    fail_test "Missing explicit standard library includes"
fi

# Test 10: Connection limit constant verification
run_test "Connection limiting constant defined"
if grep -q "MAX_CONCURRENT_CLIENTS" "$PROJECT_ROOT/ffind-daemon.cpp"; then
    pass_test
else
    fail_test "MAX_CONCURRENT_CLIENTS constant not found"
fi

# Print summary
echo ""
echo "====================================="
echo "Test Summary"
echo "====================================="
echo -e "Total tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"
echo "====================================="

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
