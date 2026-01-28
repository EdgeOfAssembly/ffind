#!/bin/bash
# Test script for SQLite persistence features

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FFIND_DAEMON="$PROJECT_ROOT/ffind-daemon"
FFIND_CLIENT="$PROJECT_ROOT/ffind"

TEST_DIR=$(mktemp -d -t ffind_persist_test_XXXXXX)
DB_PATH="$TEST_DIR/test.db"
TEST_ROOT="$TEST_DIR/test_root"

PASSED=0
FAILED=0

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Kill any running daemons
    if [ -f /run/user/$(id -u)/ffind-daemon.pid ]; then
        PID=$(cat /run/user/$(id -u)/ffind-daemon.pid 2>/dev/null || echo "")
        if [ -n "$PID" ]; then
            kill $PID 2>/dev/null || true
            sleep 1
            kill -9 $PID 2>/dev/null || true
        fi
    fi
    
    # Clean up socket and pid file
    rm -f /run/user/$(id -u)/ffind.sock
    rm -f /run/user/$(id -u)/ffind-daemon.pid
    
    # Remove test directory
    if [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
    
    echo -e "\n${GREEN}Tests passed: $PASSED${NC}"
    if [ $FAILED -gt 0 ]; then
        echo -e "${RED}Tests failed: $FAILED${NC}"
        exit 1
    fi
}

trap cleanup EXIT INT TERM

test_result() {
    local test_name="$1"
    local result="$2"
    
    if [ "$result" = "pass" ]; then
        echo -e "${GREEN}✓${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗${NC} $test_name"
        FAILED=$((FAILED + 1))
    fi
}

echo "==================================="
echo "SQLite Persistence Test Suite"
echo "==================================="
echo ""

# Setup
mkdir -p "$TEST_ROOT"
echo "file1 content" > "$TEST_ROOT/file1.txt"
echo "file2 content" > "$TEST_ROOT/file2.txt"
mkdir -p "$TEST_ROOT/subdir"
echo "file3 content" > "$TEST_ROOT/subdir/file3.txt"

echo "Test 1: Database creation"
timeout 35 $FFIND_DAEMON --foreground --db "$DB_PATH" "$TEST_ROOT" 2>&1 > /dev/null &
DAEMON_PID=$!
sleep 35
kill $DAEMON_PID 2>/dev/null || true
wait $DAEMON_PID 2>/dev/null || true

if [ -f "$DB_PATH" ]; then
    test_result "Database file created" "pass"
else
    test_result "Database file created" "fail"
    exit 1
fi

echo ""
echo "Test 2: Entries persisted"
COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM entries;")
if [ "$COUNT" -eq 4 ]; then
    test_result "Correct number of entries ($COUNT)" "pass"
else
    test_result "Correct number of entries (expected 4, got $COUNT)" "fail"
fi

echo ""
echo "Test 3: Database schema"
TABLES=$(sqlite3 "$DB_PATH" "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;")
if echo "$TABLES" | grep -q "meta" && echo "$TABLES" | grep -q "entries" && echo "$TABLES" | grep -q "sync_state"; then
    test_result "All required tables exist" "pass"
else
    test_result "All required tables exist" "fail"
fi

echo ""
echo "Test 4: Root paths stored"
ROOT_JSON=$(sqlite3 "$DB_PATH" "SELECT value FROM meta WHERE key = 'root_paths';")
if echo "$ROOT_JSON" | grep -q "$TEST_ROOT"; then
    test_result "Root paths stored in meta table" "pass"
else
    test_result "Root paths stored in meta table" "fail"
fi

echo ""
echo "Test 5: Load from database on restart"
# Start daemon again with same database
timeout 35 $FFIND_DAEMON --foreground --db "$DB_PATH" "$TEST_ROOT" 2>&1 > /tmp/daemon_log.txt &
DAEMON_PID=$!
sleep 3

# Check log for "Loaded X entries from database"
if grep -q "Loaded 4 entries from database" /tmp/daemon_log.txt; then
    test_result "Entries loaded from database on restart" "pass"
else
    test_result "Entries loaded from database on restart" "fail"
    cat /tmp/daemon_log.txt
fi

kill $DAEMON_PID 2>/dev/null || true
wait $DAEMON_PID 2>/dev/null || true

echo ""
echo "Test 6: Reconciliation - new file added"
# Add a new file while daemon is not running
echo "file4 content" > "$TEST_ROOT/file4.txt"

# Start daemon again
timeout 35 $FFIND_DAEMON --foreground --db "$DB_PATH" "$TEST_ROOT" 2>&1 > /dev/null &
DAEMON_PID=$!
sleep 35
kill $DAEMON_PID 2>/dev/null || true
wait $DAEMON_PID 2>/dev/null || true

# Check that new file was added
NEW_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM entries;")
if [ "$NEW_COUNT" -eq 5 ]; then
    test_result "New file detected and persisted" "pass"
else
    test_result "New file detected and persisted (expected 5, got $NEW_COUNT)" "fail"
fi

echo ""
echo "Test 7: Reconciliation - file deleted"
# Delete a file while daemon is not running
rm -f "$TEST_ROOT/file1.txt"

# Start daemon again
timeout 35 $FFIND_DAEMON --foreground --db "$DB_PATH" "$TEST_ROOT" 2>&1 > /dev/null &
DAEMON_PID=$!
sleep 35
kill $DAEMON_PID 2>/dev/null || true
wait $DAEMON_PID 2>/dev/null || true

# Check that deleted file was removed
FINAL_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM entries;")
if [ "$FINAL_COUNT" -eq 4 ]; then
    test_result "Deleted file removed from database" "pass"
else
    test_result "Deleted file removed from database (expected 4, got $FINAL_COUNT)" "fail"
fi

echo ""
echo "All tests completed!"
