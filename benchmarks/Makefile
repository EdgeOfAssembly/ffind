# Makefile for ffind benchmarks utilities
#
# Targets:
#   make              - Build the cache-flush utility (default)
#   make test         - Test the cache-flush binary with sudo
#   make clean        - Remove built binaries

CC = gcc
CFLAGS = -O2 -Wall -Wextra

# Default target
all: cache-flush

# Build cache-flush utility
cache-flush: cache_flush.c
	$(CC) $(CFLAGS) -o cache-flush cache_flush.c
	@echo ""
	@echo "======================================================================"
	@echo "cache-flush binary built successfully!"
	@echo ""
	@echo "To test: make test"
	@echo "To run benchmarks: ./run_real_benchmarks.sh"
	@echo ""
	@echo "Note: cache-flush requires sudo to run (needs write access to"
	@echo "      /proc/sys/vm/drop_caches for clearing filesystem caches)"
	@echo "======================================================================"

# Test the cache-flush binary with sudo
test: cache-flush
	@echo "Testing cache-flush binary with sudo..."
	@echo ""
	sudo ./cache-flush && echo "✓ Cache flush successful!" || echo "✗ Cache flush failed"

# Clean build artifacts
clean:
	rm -f cache-flush

.PHONY: all test clean