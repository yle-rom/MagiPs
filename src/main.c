#include <stdio.h>

#include "apps.h"
#include "docker.h"
#include "ports.h"
#include "procfs.h"
#include "table.h"
#include "util.h"

int main(void)
{
    Arena *a = arena_new(1 << 16);     // 64 KB blocks

    ProcList procs = procfs_scan(a, "/proc");
    // TEMP: check step 1a
    printf("%zu processes\n", procs.len);
    for (size_t i = 0; i < procs.len; i += procs.len / 8)
        printf("  pid %-6d ppid %-6d uid %-5d rss %-8ld %-16s %.50s\n", procs.items[i].pid,
               procs.items[i].ppid, procs.items[i].uid, procs.items[i].rss_kb,
               procs.items[i].comm, procs.items[i].cmdline);
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
