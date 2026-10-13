#ifndef PROCFS_H
#define PROCFS_H

#include <stddef.h>
#include "util.h"

typedef struct {
    int pid, ppid, uid;
    char comm[16];              // short name from /proc/pid/stat (kernel max 15 + '\0')
    char *cmdline;              // full command line
    long rss_kb;                // resident memory
    unsigned long long start;   // start time, clock ticks since boot
} Proc;

typedef struct {
    Proc *items;
    size_t len;
} ProcList;

/* Read every process under root (normally "/proc"). */
ProcList procfs_scan(Arena *a, const char *root);

#endif
