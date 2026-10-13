#include <stdio.h>
#include <unistd.h>

#include "apps.h"
#include "desktop.h"
#include "docker.h"
#include "ports.h"
#include "procfs.h"
#include "table.h"
#include "util.h"

int main(void)
{
    Arena *a = arena_new(1 << 16);     // 64 KB blocks

    ProcList procs = procfs_scan(a, "/proc");
    // TEMP: check step 1b
    DesktopIndex desk = desktop_load(a, desktop_default_dirs(a));
    printf("%zu desktop keys\n", desk.len);
    for (size_t i = 0; i < procs.len; i++) {
        const DesktopEntry *e = desktop_match(&desk, &procs.items[i]);
        if (e && procs.items[i].uid == (int)getuid())
            printf("  %-6d ppid %-6d %-16s -> %s%s\n", procs.items[i].pid, procs.items[i].ppid,
                   procs.items[i].comm, e->name, e->terminal ? " (terminal)" : "");
    }

    AppList apps = apps_build(a, &procs);
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
