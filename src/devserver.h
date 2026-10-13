#ifndef DEVSERVER_H
#define DEVSERVER_H

#include <stdbool.h>
#include <stddef.h>
#include "apps.h"
#include "ports.h"
#include "tree.h"
#include "util.h"

typedef struct {
    int pid;                    // top of the server: "npm run dev", not the node it spawned
    unsigned long long start;
    const char *project;        // folder it runs in, "~/code/web"
    const char *command;        // "vite", "python3 -m http.server 8000"
    const char *ports;          // "5173" or "5173, 24678"
    bool orphaned;              // its terminal is gone
    Members members;            // the server and everything it spawned
} DevServer;

typedef struct {
    DevServer *items;
    size_t len;
} DevServerList;

/*
 * Dev servers: your processes listening on a port that were started from a
 * terminal or run on a dev runtime (node, python, ruby, go, cargo...).
 * GUI apps (Spotify) and systemd units (syncthing) don't count.
 * home shortens project paths to "~/...".
 */
DevServerList dev_find(Arena *a, const Tree *t, const AppList *apps, const PortList *ports,
                       const char *root, int uid, const char *home);

#endif
