#include <stdio.h>
#include <unistd.h>

#include "apps.h"
#include "desktop.h"
#include "docker.h"
#include "ports.h"
#include "procfs.h"
#include "services.h"
#include "tree.h"
#include "table.h"
#include "util.h"

int main(void)
{
    Arena *a = arena_new(1 << 16);     // 64 KB blocks

    ProcList procs = procfs_scan(a, "/proc");
    Tree tree = tree_build(a, &procs);
    DesktopIndex desk = desktop_load(a, desktop_default_dirs(a));
    AppList apps = apps_build(a, &tree, &desk, (int)getuid());

    ServiceList services = services_build(a, &tree, (int)getuid());

    // TEMP: check step 1e
    for (size_t i = 0; i < services.len; i++)
        printf("  %-6s %-28s %-7d %-4zu %ldM\n",
               services.items[i].scope == SCOPE_SYSTEM ? "system" : "user",
               services.items[i].name, services.items[i].pid,
               services.items[i].nprocs, services.items[i].mem_kb / 1024);

    PortList ports = ports_scan(a, "/proc", &procs);
    ContainerList containers = docker_list(a);

    const char *app_cols[] = { "APP", "PID", "PROCS", "MEM" };
    Table *t = table_new(a, "APPS", 4, app_cols);
    for (size_t i = 0; i < apps.len; i++) {
        // TODO: format app row
    }
    table_print(t);

    const char *ctr_cols[] = { "NAME", "IMAGE", "PORTS", "STATUS" };
    t = table_new(a, "CONTAINERS", 4, ctr_cols);
    for (size_t i = 0; i < containers.len; i++) {
        // TODO: format container row
    }
    table_print(t);

    const char *port_cols[] = { "PROTO", "ADDRESS", "PORT", "PID" };
    t = table_new(a, "PORTS", 4, port_cols);
    for (size_t i = 0; i < ports.len; i++) {
        // TODO: format port row
    }
    table_print(t);

    arena_free(a);
    return 0;
}
