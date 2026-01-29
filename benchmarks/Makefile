# Makefile for ffind benchmarks utilities
#
# Targets:
#   make cache-flush      - Build the cache-flush utility
#   make install-caps     - Grant CAP_SYS_ADMIN to cache-flush (requires sudo)
#   make clean           - Remove built binaries

CC = gcc
CFLAGS = -O2 -Wall -Wextra

# Main target
cache-flush: cache_flush.c
	$(CC) $(CFLAGS) -o cache-flush cache_flush.c
	@echo ""
	@echo "======================================================================"
	@echo "cache-flush binary built successfully!"
	@echo ""
	@echo "To enable cache flushing without sudo, run:"
	@echo "    make install-caps"
	@echo ""
	@echo "Or manually grant the capability:"
	@echo "    sudo setcap cap_sys_admin+ep ./cache-flush"
	@echo "======================================================================"

# Grant CAP_SYS_ADMIN capability (requires sudo)
install-caps: cache-flush
	@echo "Granting CAP_SYS_ADMIN capability to cache-flush..."
	sudo setcap cap_sys_admin+ep ./cache-flush
	@echo ""
	@echo "✓ Capability granted successfully!"
	@echo ""
	@echo "You can now run cache-flush without sudo:"
	@echo "    ./cache-flush"

# Test the cache-flush binary
test: cache-flush
	@echo "Testing cache-flush binary..."
	@echo ""
	@echo "Attempting to flush caches (may fail if capabilities not set)..."
	./cache-flush || echo "Note: Run 'make install-caps' to enable passwordless flushing"

# Clean build artifacts
clean:
	rm -f cache-flush

# Help
help:
	@echo "ffind Benchmarks Utilities"
	@echo ""
	@echo "Targets:"
	@echo "  make cache-flush     - Build the cache-flush utility"
	@echo "  make install-caps    - Grant CAP_SYS_ADMIN to cache-flush (requires sudo)"
	@echo "  make test           - Test the cache-flush binary"
	@echo "  make clean          - Remove built binaries"
	@echo ""
	@echo "Usage:"
	@echo "  1. make cache-flush"
	@echo "  2. make install-caps"
	@echo "  3. ./run_real_benchmarks.sh"

.PHONY: install-caps test clean help