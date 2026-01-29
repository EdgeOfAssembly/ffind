/*
 * cache_flush.c - Minimal privileged helper to flush filesystem caches
 *
 * This tiny utility flushes Linux kernel filesystem caches for fair benchmarking.
 * It's designed to be small, auditable, and run with CAP_SYS_ADMIN capability.
 *
 * Build:
 *   gcc -O2 -o cache-flush cache_flush.c
 *   sudo setcap cap_sys_admin+ep cache-flush
 *
 * Usage:
 *   ./cache-flush    # Flush caches (no sudo needed after setcap)
 *
 * Security Best Practice:
 *   - Grant minimal privileges to this tiny binary (CAP_SYS_ADMIN only)
 *   - NEVER run benchmarks or ffind-daemon as root
 *   - Keep this code minimal for easy security auditing
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    FILE *f;
    
    /* Step 1: Flush dirty pages to disk */
    sync();
    
    /* Step 2: Drop caches (page cache, dentries, inodes) */
    f = fopen("/proc/sys/vm/drop_caches", "w");
    if (!f) {
        perror("fopen /proc/sys/vm/drop_caches");
        fprintf(stderr, "\nTo fix:\n");
        fprintf(stderr, "  sudo setcap cap_sys_admin+ep cache-flush\n");
        fprintf(stderr, "Or:\n");
        fprintf(stderr, "  sudo ./cache-flush\n");
        return 1;
    }
    
    fprintf(f, "3\n");
    fclose(f);
    
    return 0;
}
