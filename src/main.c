#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "actions.h"
#include "apps.h"
#include "desktop.h"
#include "docker.h"
#include "ports.h"
#include "procfs.h"
#include "services.h"
#include "table.h"
#include "tree.h"
#include "util.h"

#define PROC_ROOT "/proc"

/* How long ago a process started: "2d4h", "3h12m", "22m", "40s". */
static const char *format_age(Arena *a, unsigned long long start, double uptime)
{
    long secs = (long)(uptime - (double)start / (double)sysconf(_SC_CLK_TCK));
    long m = secs / 60, h = m / 60, d = h / 24;
    if (d)
        return arena_printf(a, "%ldd%ldh", d, h % 24);
    if (h)
        return arena_printf(a, "%ldh%02ldm", h, m % 60);
    if (m)
        return arena_printf(a, "%ldm", m);
    return arena_printf(a, "%lds", secs < 0 ? 0 : secs);
}

static void print_apps(Arena *a, const AppList *apps, double uptime)
{
    const char *cols[] = { "APP", "PID", "PROCS", "MEM", "UPTIME" };
    Table *t = table_new(a, "APPS", 5, cols);
    for (size_t i = 0; i < apps->len; i++) {
        const App *app = &apps->items[i];
        const char *row[] = {
            app->name,
            arena_printf(a, "%d", app->pid),
            arena_printf(a, "%zu", app->members.len),
            format_mem(a, app->mem_kb),
            format_age(a, app->start, uptime),
        };
        table_row(t, row);
    }
    table_print(t);
}

static void print_services(Arena *a, const ServiceList *services, double uptime)
{
    const char *cols[] = { "SERVICE", "SCOPE", "PID", "PROCS", "MEM", "UPTIME" };
    Table *t = table_new(a, "SERVICES", 6, cols);
    for (size_t i = 0; i < services->len; i++) {
        const Service *s = &services->items[i];
        const char *row[] = {
            s->name,
            s->scope == SCOPE_SYSTEM ? "system" : "user",
            arena_printf(a, "%d", s->pid),
            arena_printf(a, "%zu", s->members.len),
            format_mem(a, s->mem_kb),
            format_age(a, s->start, uptime),
        };
        table_row(t, row);
    }
    table_print(t);
}

static void print_containers(Arena *a, const ContainerList *containers)
{
    const char *cols[] = { "CONTAINER", "IMAGE", "PORTS", "STATUS" };
    Table *t = table_new(a, "CONTAINERS", 4, cols);
    for (size_t i = 0; i < containers->len; i++) {
        const Container *c = &containers->items[i];
        const char *row[] = { c->name, c->image, c->ports, c->status };
        table_row(t, row);
    }
    table_print(t);
}

/* Who owns a port: the process if we can see it, else a container publishing it. */
static const char *port_owner(Arena *a, const PortSummary *p, const Tree *tree,
                              const ContainerList *containers)
{
    if (p->pid >= 0) {
        size_t i = tree_find(tree, p->pid);
        return i != TREE_NONE ? tree->procs->items[i].comm : "?";
    }
    int c = docker_find_port(containers, p->port);
    return c >= 0 ? arena_printf(a, "docker: %s", containers->items[c].name) : NULL;
}

/* Ports nobody visible owns are system plumbing: hidden, only counted. */
static void print_ports(Arena *a, const PortList *ports, const Tree *tree,
                        const ContainerList *containers)
{
    const char *cols[] = { "PORT", "OPEN TO", "PID", "OWNER" };
    Table *t = table_new(a, "PORTS", 4, cols);
    PortSummary *sums = arena_alloc(a, (ports->len + 1) * sizeof *sums);
    size_t n = ports_summarize(ports, sums), hidden = 0;

    for (size_t i = 0; i < n; i++) {
        const char *owner = port_owner(a, &sums[i], tree, containers);
        if (!owner) {
            hidden++;
            continue;
        }
        const char *row[] = {
            arena_printf(a, "%d", sums[i].port),
            port_reach_label(&sums[i]),
            sums[i].pid >= 0 ? arena_printf(a, "%d", sums[i].pid) : "-",
            owner,
        };
        table_row(t, row);
    }
    table_print_note(t, hidden ? arena_printf(a, "(%zu system ports hidden)", hidden) : NULL);
}

static void usage(FILE *out)
{
    fputs("usage: whatsup [-i]\n"
          "  (no option)  print apps, services, containers and ports\n"
          "  -i           interactive: pick something and stop it\n"
          "  -h           this help\n", out);
}

int main(int argc, char **argv)
{
    bool interactive = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "whatsup: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }

    Arena *a = arena_new(1 << 16);     // 64 KB blocks
    int uid = (int)getuid();

    ProcList procs = procfs_scan(a, PROC_ROOT);
    double uptime = procfs_uptime(PROC_ROOT);
    Tree tree = tree_build(a, &procs);

    // order matters: apps claim their processes first, services take the rest
    DesktopIndex desk = desktop_load(a, desktop_default_dirs(a));
    AppList apps = apps_build(a, &tree, &desk, uid);
    ServiceList services = services_build(a, &tree, uid);
    ContainerList containers = docker_list(a);
    PortList ports = ports_scan(a, PROC_ROOT, &procs);

    int status = 0;
    if (interactive) {
        Snapshot snap = { &apps, &services, &containers, &ports, &tree, PROC_ROOT };
        status = actions_interactive(a, &snap);
    } else {
        print_apps(a, &apps, uptime);
        print_services(a, &services, uptime);
        print_containers(a, &containers);
        print_ports(a, &ports, &tree, &containers);
    }

    arena_free(a);
    return status;
}
