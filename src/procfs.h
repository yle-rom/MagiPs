#ifndef PROCFS_H
#define PROCFS_H

#include <stdbool.h>
#include <stddef.h>
#include "util.h"

typedef struct {
    int pid, ppid, uid;
    int sid;                    // session id: pid == sid for the shell a terminal opened
    char state;                 // R running, S sleeping, Z zombie...
    char comm[16];              // short name from /proc/pid/stat (kernel max 15 + '\0')
    char *cmdline;              // full command line ("" for kernel threads)
    char *exe;                  // program path ("" if not readable)
    char *cgroup;               // systemd cgroup path ("" if unknown)
    long rss_kb;                // resident memory
    unsigned long long start;   // start time, clock ticks since boot
} Proc;

typedef struct {
    Proc *items;
    size_t len;
} ProcList;

/* Read every process under root (normally "/proc"). */
ProcList procfs_scan(Arena *a, const char *root);

/* Seconds since boot, from <root>/uptime. Proc.start / CLK_TCK is on the same clock. */
double procfs_uptime(const char *root);

/* Is pid still the same process (same start time) and not a zombie? */
bool procfs_alive(const char *root, int pid, unsigned long long start);

#endif
