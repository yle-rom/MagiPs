#include "world.h"
#include <stdlib.h>
#include <unistd.h>
#include "config.h"
#include "desktop.h"

void world_scan(Arena *a, World *w)
{
    int uid = (int)getuid();
    Config cfg = config_load(a);
    w->procs = procfs_scan(a, PROC_ROOT);
    w->uptime = procfs_uptime(PROC_ROOT);
    w->tree = tree_build(a, &w->procs);

    // order matters: apps claim their processes first, services take the rest
    DesktopIndex desk = desktop_load(a, desktop_default_dirs(a));
    for (size_t i = 0; i < cfg.apps.len; i++)
        desktop_add(a, &desk, cfg.apps.items[i]);
    w->apps = apps_build(a, &w->tree, &desk, uid);
    w->services = services_build(a, &w->tree, uid, &cfg);
    w->containers = docker_list(a);
    w->ports = ports_scan(a, PROC_ROOT, &w->procs);
    w->databases = db_find(a, &w->tree, &w->services, &w->containers, &w->ports);
    w->devservers = dev_find(a, &w->tree, &w->apps, &w->ports, PROC_ROOT, uid, getenv("HOME"));
}

long world_age(unsigned long long start, double uptime)
{
    long secs = (long)(uptime - (double)start / (double)sysconf(_SC_CLK_TCK));
    return secs < 0 ? 0 : secs;
}

const char *world_port_owner(Arena *a, const World *w, const PortSummary *p)
{
    if (p->pid >= 0) {
        size_t i = tree_find(&w->tree, p->pid);
        return i != TREE_NONE ? w->tree.procs->items[i].comm : "?";
    }
    int c = docker_find_port(&w->containers, p->port);
    return c >= 0 ? arena_printf(a, "docker: %s", w->containers.items[c].name) : NULL;
}
