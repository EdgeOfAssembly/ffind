/*
 * cache_flush.c - Minimal privileged helper to flush filesystem caches
 *
 * This tiny utility flushes Linux kernel filesystem caches for fair benchmarking.
 * It's designed to be small, auditable, and run with sudo.
 *
 * Build:
 *   gcc -O2 -o cache-flush cache_flush.c
 *
 * Usage:
 *   sudo ./cache-flush    # Flush caches (requires elevated privileges)
 *
 * Security Best Practice:
 *   - Only this minimal binary needs elevated privileges
 *   - NEVER run benchmarks or ffind-daemon as root
 *   - Keep this code minimal for easy security auditing
 *   - Use sudo each time for explicit privilege control
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    FILE *f;
    int ret;
    
    /* Step 1: Flush dirty pages to disk */
    sync();
    
    /* Step 2: Drop caches (page cache, dentries, inodes) */
    f = fopen("/proc/sys/vm/drop_caches", "w");
    if (!f) {
        perror("fopen /proc/sys/vm/drop_caches");
        fprintf(stderr, "\nTo fix:\n");
        fprintf(stderr, "  Run with sudo: sudo ./cache-flush\n");
        return 1;
    }
    
    ret = fprintf(f, "3\n");
    if (ret < 0) {
        perror("fprintf");
        fclose(f);
        return 1;
    }
    
    if (fclose(f) != 0) {
        perror("fclose");
        return 1;
    }
    
    return 0;
}