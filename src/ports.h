#ifndef PORTS_H
#define PORTS_H

#include <stddef.h>
#include "procfs.h"
#include "util.h"

typedef struct {
    char proto[4];              // "tcp" / "udp"
    char addr[64];              // "127.0.0.1", "::"
    int port;
    int pid;                    // -1 if unknown (no permission)
} Port;

typedef struct {
    Port *items;
    size_t len;
} PortList;

/* Listening sockets from root/net/{tcp,tcp6,udp,udp6}, matched to pids. */
PortList ports_scan(Arena *a, const char *root, const ProcList *procs);

#endif
