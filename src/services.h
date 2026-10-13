#ifndef SERVICES_H
#define SERVICES_H

#include <stddef.h>
#include "tree.h"
#include "util.h"

typedef enum { SCOPE_SYSTEM, SCOPE_USER } Scope;

typedef struct {
    const char *name;           // unit name ("docker") or process name ("waybar")
    Scope scope;
    int pid;                    // first-started process of the group
    size_t nprocs;
    long mem_kb;
    unsigned long long start;
} Service;

typedef struct {
    Service *items;
    size_t len;
} ServiceList;

/*
 * Everything running under the hood that apps_build didn't claim:
 *   - processes in a systemd .service unit, grouped by unit (system or user)
 *   - other processes of uid, grouped by process tree (waybar, mako, ollama)
 * Kernel threads, PID 1, containers and an "essentials" list are hidden.
 * Run after apps_build. Sorted: system first, then by name.
 */
ServiceList services_build(Arena *a, Tree *t, int uid);

#endif
