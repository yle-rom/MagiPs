#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include "procfs.h"
#include "util.h"

/*
 * Installed apps, from freedesktop .desktop files (what app launchers show).
 * One .desktop file can give several keys: "mongodb-compass" (from Exec=)
 * and "MongoDB Compass" (the real binary that symlink points to).
 */
typedef struct {
    char *key;          // executable name to match against a process
    char *name;         // display name, "Firefox"
    bool terminal;      // Categories has TerminalEmulator (alacritty, kitty...)
} DesktopEntry;

typedef struct {
    DesktopEntry *items;
    size_t len, cap;
} DesktopIndex;

/* $XDG_DATA_HOME/applications + each $XDG_DATA_DIRS/applications, NULL-terminated. */
const char **desktop_default_dirs(Arena *a);

/* Read every *.desktop file in the given NULL-terminated list of dirs. */
DesktopIndex desktop_load(Arena *a, const char **dirs);

/* Add one process name as an app (from the config's [apps] list). */
void desktop_add(Arena *a, DesktopIndex *d, const char *name);

/* The entry whose key matches the process's exe or comm, or NULL. */
const DesktopEntry *desktop_match(const DesktopIndex *d, const Proc *p);

#endif
