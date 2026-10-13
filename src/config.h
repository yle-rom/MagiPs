#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "util.h"

typedef struct {
    const char **items;
    size_t len, cap;
} StrList;

/* ~/.config/magisc/config: the lists that decide what's shown. */
typedef struct {
    StrList hide_system;        // system services needed for the machine to work
    StrList hide_user;          // your desktop plumbing (portals, dbus...)
    StrList apps;               // process names to list as apps without a .desktop file
    const char *path;           // where it was read from (NULL = built-in defaults)
} Config;

/* The file written on first run. */
extern const char config_default_text[];

/*
 * Read $XDG_CONFIG_HOME/magisc/config (default ~/.config/magisc/config).
 * If it doesn't exist, write config_default_text there first.
 */
Config config_load(Arena *a);

/* Parse config text. Problems are reported on stderr, prefixed with origin. */
Config config_parse(Arena *a, const char *text, const char *origin);

/* Does name match any entry? Entries may use * and ? wildcards. */
bool config_match(const StrList *list, const char *name);

#endif
