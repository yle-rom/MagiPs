#include "ports.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TCP_LISTEN 0x0A

typedef struct {
    unsigned long inode;
    int pid;
} SocketOwner;

/*
 * The kernel prints addresses as hex of the raw in-memory value:
 * "0100007F" is 127.0.0.1 on a little-endian machine. Reading the hex back
 * into a uint32 and copying its bytes undoes that on the same machine.
 */
static void decode_addr(const char *hex, int v6, char *out, size_t size)
{
    if (!v6) {
        struct in_addr in;
        in.s_addr = (uint32_t)strtoul(hex, NULL, 16);
        inet_ntop(AF_INET, &in, out, (socklen_t)size);
        return;
    }
    struct in6_addr in6;
    for (int k = 0; k < 4; k++) {
        char word[9];
        memcpy(word, hex + 8 * k, 8);
        word[8] = '\0';
        uint32_t raw = (uint32_t)strtoul(word, NULL, 16);
        memcpy(&in6.s6_addr[4 * k], &raw, 4);
    }
    inet_ntop(AF_INET6, &in6, out, (socklen_t)size);
}

/*
 * One line per socket:
 *   sl  local_address rem_address   st tx:rx tr:when retrnsmt uid timeout inode
 *   0: 0100007F:1F90 00000000:0000 0A ...                              12345
 */
static void parse_net(Arena *a, PortList *list, size_t *cap, const char *root,
                      const char *file, int v6)
{
    char path[512], line[512];
    snprintf(path, sizeof path, "%s/net/%s", root, file);
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    if (!fgets(line, sizeof line, f)) {             // header line
        fclose(f);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char local[64];
        unsigned state;
        unsigned long inode;
        if (sscanf(line, "%*s %63s %*s %x %*s %*s %*s %*s %*s %lu",
                   local, &state, &inode) != 3)
            continue;
        if (state != TCP_LISTEN)
            continue;

        char *colon = strchr(local, ':');
        if (!colon || (size_t)(colon - local) != (v6 ? 32u : 8u))
            continue;
        *colon = '\0';

        if (list->len == *cap)
            list->items = arena_grow(a, list->items, list->len, cap, sizeof *list->items);
        Port *p = &list->items[list->len++];
        snprintf(p->proto, sizeof p->proto, "%s", v6 ? "tcp6" : "tcp");
        decode_addr(local, v6, p->addr, sizeof p->addr);
        p->port = (int)strtol(colon + 1, NULL, 16);
        p->pid = -1;
        p->inode = inode;
    }
    fclose(f);
}

static int cmp_inode(const void *x, const void *y)
{
    const SocketOwner *a = x, *b = y;
    return (a->inode > b->inode) - (a->inode < b->inode);
}

/* Every "socket:[N]" open by any process we're allowed to look at. */
static SocketOwner *collect_sockets(Arena *a, const char *root, const ProcList *procs,
                                    size_t *len)
{
    SocketOwner *items = NULL;
    size_t cap = 0;
    *len = 0;

    for (size_t i = 0; i < procs->len; i++) {
        char dir_path[512];
        snprintf(dir_path, sizeof dir_path, "%s/%d/fd", root, procs->items[i].pid);
        DIR *dir = opendir(dir_path);
        if (!dir)
            continue;                               // not ours: permission denied

        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char fd_path[1024], target[64];
            snprintf(fd_path, sizeof fd_path, "%s/%s", dir_path, e->d_name);
            ssize_t n = readlink(fd_path, target, sizeof target - 1);
            if (n <= 0)
                continue;
            target[n] = '\0';
            if (strncmp(target, "socket:[", 8) != 0)
                continue;

            if (*len == cap)
                items = arena_grow(a, items, *len, &cap, sizeof *items);
            items[(*len)++] = (SocketOwner){ strtoul(target + 8, NULL, 10),
                                             procs->items[i].pid };
        }
        closedir(dir);
    }
    if (*len)
        qsort(items, *len, sizeof *items, cmp_inode);
    return items;
}

static int cmp_port(const void *x, const void *y)
{
    const Port *a = x, *b = y;
    if (a->port != b->port)
        return (a->port > b->port) - (a->port < b->port);
    return strcmp(a->proto, b->proto);
}

PortList ports_scan(Arena *a, const char *root, const ProcList *procs)
{
    PortList list = { 0 };
    size_t cap = 0;
    parse_net(a, &list, &cap, root, "tcp", 0);
    parse_net(a, &list, &cap, root, "tcp6", 1);

    size_t nsock;
    SocketOwner *owners = collect_sockets(a, root, procs, &nsock);
    for (size_t i = 0; i < list.len && nsock; i++) {
        SocketOwner key = { .inode = list.items[i].inode };
        SocketOwner *hit = bsearch(&key, owners, nsock, sizeof key, cmp_inode);
        if (hit)
            list.items[i].pid = hit->pid;
    }

    if (list.len)
        qsort(list.items, list.len, sizeof *list.items, cmp_port);
    return list;
}
