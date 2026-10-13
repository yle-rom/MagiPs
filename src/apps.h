#ifndef APPS_H
#define APPS_H

#include <stddef.h>
#include "desktop.h"
#include "tree.h"
#include "util.h"

typedef struct {
    const char *name;           // "Firefox", or "Alacritty → nvim" for terminals
    int pid;                    // top-level process (kill this)
    Members members;            // root(s) + all descendants: what a kill stops
    long mem_kb;                // summed rss
    unsigned long long start;   // clock ticks since boot
    const DesktopEntry *entry;
} App;

typedef struct {
    App *items;
    size_t len;
} AppList;

/*
 * An app is a process of uid whose program matches a .desktop entry and
 * whose ancestors don't (firefox, not its 15 children). Claims each app's
 * whole subtree in t. Sorted by memory, biggest first.
 */
AppList apps_build(Arena *a, Tree *t, const DesktopIndex *d, int uid);

#endif
