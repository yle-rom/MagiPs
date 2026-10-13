#ifndef APPS_H
#define APPS_H

#include <stddef.h>
#include "procfs.h"
#include "util.h"

typedef struct {
    Proc *root;                 // top-level process of the app
    size_t nprocs;              // root + all descendants
    long mem_kb;                // summed rss
} App;

typedef struct {
    App *items;
    size_t len;
} AppList;

/* Group processes into top-level apps, hiding kernel/system noise. */
AppList apps_build(Arena *a, const ProcList *procs);

#endif
