#!/bin/bash
# flush-cache.sh - Helper script to flush filesystem caches for fair benchmarking
#
# This script provides a reusable utility for clearing Linux filesystem caches
# to ensure fair benchmark comparisons between cached and uncached operations.
#
# Usage:
#   ./flush-cache.sh              # Flush caches (requires sudo)
#   ./flush-cache.sh --check      # Check if cache flushing is available
#   ./flush-cache.sh --help       # Show this help message
#
# Exit codes:
#   0 - Success (caches flushed or check passed)
#   1 - Error (no sudo privileges or invalid usage)
#   2 - Not supported (not on Linux or /proc/sys/vm/drop_caches not available)

set -e

# Color output for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
print_error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
}

print_success() {
    echo -e "${GREEN}$1${NC}"
}

print_warning() {
    echo -e "${YELLOW}WARNING: $1${NC}" >&2
}

print_info() {
    echo "$1"
}

# Function to show help
show_help() {
    cat << EOF
flush-cache.sh - Flush Linux filesystem caches for fair benchmarking

USAGE:
    ./flush-cache.sh [OPTIONS]

OPTIONS:
    --check     Check if cache flushing is available (doesn't flush)
    --help      Show this help message

DESCRIPTION:
    This script flushes the Linux kernel's filesystem caches by writing to
    /proc/sys/vm/drop_caches. This is useful for benchmarking to ensure that
    disk operations start from a cold cache state.

    The script performs three operations:
    1. sync       - Flush dirty pages to disk
    2. echo 3 > /proc/sys/vm/drop_caches - Clear page cache, dentries, and inodes
    3. sleep 0.5  - Brief pause to ensure caches are cleared

REQUIREMENTS:
    - Linux operating system
    - Root privileges (sudo or running as root)
    - /proc/sys/vm/drop_caches must be writable

EXAMPLES:
    # Check if cache flushing is available
    ./flush-cache.sh --check

    # Flush caches before a benchmark
    sudo ./flush-cache.sh
    time find /usr -name "*.so"

    # Use in a script with error handling
    if ./flush-cache.sh --check; then
        sudo ./flush-cache.sh
        run_benchmark
    else
        echo "Cannot flush caches, results may be affected by caching"
        run_benchmark
    fi

EXIT CODES:
    0 - Success
    1 - Error (insufficient privileges or invalid usage)
    2 - Not supported (not Linux or drop_caches not available)

NOTES:
    - Cache flushing requires root/sudo privileges
    - Only works on Linux systems
    - Use responsibly - flushing caches affects system performance temporarily
    - For benchmarking, flush before each cold-cache test iteration

SEE ALSO:
    Documentation: https://www.kernel.org/doc/Documentation/sysctl/vm.txt
    man 1 sync

EOF
}

# Function to check if we're on Linux
check_linux() {
    if [[ "$(uname -s)" != "Linux" ]]; then
        print_error "This script only works on Linux systems"
        print_info "Current OS: $(uname -s)"
        return 2
    fi
    return 0
}

# Function to check if drop_caches is available
check_drop_caches() {
    if [[ ! -e /proc/sys/vm/drop_caches ]]; then
        print_error "/proc/sys/vm/drop_caches not found"
        print_info "Your kernel may not support cache dropping"
        return 2
    fi
    return 0
}

# Function to check if we have necessary privileges
check_privileges() {
    # Check if already root
    if [[ "$EUID" -eq 0 ]]; then
        return 0
    fi
    
    # Check if sudo is available and we can use it without password
    if command -v sudo >/dev/null 2>&1; then
        if sudo -n true 2>/dev/null; then
            return 0
        else
            print_error "sudo privileges required but not available"
            print_info "Try running: sudo $0"
            return 1
        fi
    else
        print_error "sudo not found and not running as root"
        return 1
    fi
}

# Function to perform the actual cache flush
do_flush() {
    print_info "Flushing filesystem caches..."
    
    # Step 1: Sync dirty pages to disk
    if ! sync 2>/dev/null; then
        print_warning "sync command failed (non-fatal)"
    fi
    
    # Step 2: Drop caches
    if [[ "$EUID" -eq 0 ]]; then
        # Already root
        if echo 3 > /proc/sys/vm/drop_caches 2>/dev/null; then
            print_success "✓ Caches flushed successfully (running as root)"
        else
            print_error "Failed to write to /proc/sys/vm/drop_caches"
            return 1
        fi
    else
        # Use sudo
        if sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null; then
            print_success "✓ Caches flushed successfully (using sudo)"
        else
            print_error "Failed to write to /proc/sys/vm/drop_caches via sudo"
            return 1
        fi
    fi
    
    # Step 3: Brief pause to ensure caches are cleared
    sleep 0.5
    
    return 0
}

# Main logic
main() {
    case "${1:-}" in
        --help|-h)
            show_help
            exit 0
            ;;
        --check)
            print_info "Checking cache flush availability..."
            
            # Check Linux
            if ! check_linux; then
                exit 2
            fi
            print_success "✓ Running on Linux"
            
            # Check drop_caches
            if ! check_drop_caches; then
                exit 2
            fi
            print_success "✓ /proc/sys/vm/drop_caches is available"
            
            # Check privileges
            if ! check_privileges; then
                exit 1
            fi
            print_success "✓ Sufficient privileges (root or passwordless sudo)"
            
            print_success "✓ Cache flushing is available and ready to use"
            exit 0
            ;;
        "")
            # No arguments - perform flush
            
            # Run all checks
            if ! check_linux; then
                exit 2
            fi
            
            if ! check_drop_caches; then
                exit 2
            fi
            
            if ! check_privileges; then
                exit 1
            fi
            
            # Perform the flush
            if ! do_flush; then
                exit 1
            fi
            
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
}

# Run main function
main "$@"
