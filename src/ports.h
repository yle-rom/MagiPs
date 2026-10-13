#ifndef PORTS_H
#define PORTS_H

#include <stddef.h>
#include "procfs.h"
#include "util.h"

typedef struct {
    char proto[5];              // "tcp" / "tcp6"
    char addr[64];              // "127.0.0.1", "::"
    int port;
    int pid;                    // -1 if unknown (other user's process, needs root)
    unsigned long inode;        // socket id, links the port to a process
} Port;

typedef struct {
    Port *items;
    size_t len;
} PortList;

/* Who can connect to a listening socket. */
typedef enum {
    REACH_LOCAL,        // 127.x.x.x / ::1: only this machine
    REACH_NETWORK,      // 0.0.0.0 / ::  : every network interface
    REACH_ADDRESS,      // one specific address (e.g. a VM bridge 192.168.122.1)
} Reach;

Reach port_reach(const Port *p);

/*
 * Listening TCP sockets from <root>/net/tcp{,6}. The owner is found by
 * looking for "socket:[inode]" among each process's open files (<root>/<pid>/fd).
 * Sorted by port.
 */
PortList ports_scan(Arena *a, const char *root, const ProcList *procs);

#endif
