#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

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

/* printf into a new arena string. */
static const char *fmt(Arena *a, const char *format, ...)
{
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof buf, format, args);
    va_end(args);
    return arena_strdup(a, buf);
}

static const char *fmt_mem(Arena *a, long kb)
{
    if (kb >= 1024 * 1024)
        return fmt(a, "%.1fG", kb / (1024.0 * 1024.0));
    if (kb >= 1024)
        return fmt(a, "%ldM", kb / 1024);
    return fmt(a, "%ldK", kb);
}

/* How long ago a process started: "2d4h", "3h12m", "22m", "40s". */
static const char *fmt_age(Arena *a, unsigned long long start, double uptime)
{
    long secs = (long)(uptime - (double)start / (double)sysconf(_SC_CLK_TCK));
    long m = secs / 60, h = m / 60, d = h / 24;
    if (d)
        return fmt(a, "%ldd%ldh", d, h % 24);
    if (h)
        return fmt(a, "%ldh%02ldm", h, m % 60);
    if (m)
        return fmt(a, "%ldm", m);
    return fmt(a, "%lds", secs < 0 ? 0 : secs);
}

static void print_apps(Arena *a, const AppList *apps, double uptime)
{
    const char *cols[] = { "APP", "PID", "PROCS", "MEM", "UPTIME" };
    Table *t = table_new(a, "APPS", 5, cols);
    for (size_t i = 0; i < apps->len; i++) {
        const App *app = &apps->items[i];
        const char *row[] = {
            app->name,
            fmt(a, "%d", app->pid),
            fmt(a, "%zu", app->nprocs),
            fmt_mem(a, app->mem_kb),
            fmt_age(a, app->start, uptime),
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
            fmt(a, "%d", s->pid),
            fmt(a, "%zu", s->nprocs),
            fmt_mem(a, s->mem_kb),
            fmt_age(a, s->start, uptime),
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
static const char *port_owner(Arena *a, const Port *p, const Tree *tree,
                              const ContainerList *containers)
{
    if (p->pid >= 0) {
        size_t i = tree_find(tree, p->pid);
        return i != TREE_NONE ? tree->procs->items[i].comm : "?";
    }
    int c = docker_find_port(containers, p->port);
    if (c >= 0)
        return fmt(a, "docker: %s", containers->items[c].name);
    return "? (not yours, try sudo)";
}

static void print_ports(Arena *a, const PortList *ports, const Tree *tree,
                        const ContainerList *containers)
{
    const char *cols[] = { "PORT", "PROTO", "ADDRESS", "PID", "PROCESS" };
    Table *t = table_new(a, "PORTS", 5, cols);
    for (size_t i = 0; i < ports->len; i++) {
        const Port *p = &ports->items[i];
        const char *row[] = {
            fmt(a, "%d", p->port),
            p->proto,
            p->addr,
            p->pid >= 0 ? fmt(a, "%d", p->pid) : "-",
            port_owner(a, p, tree, containers),
        };
        table_row(t, row);
    }
    table_print(t);
}

int main(void)
{
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

    print_apps(a, &apps, uptime);
    print_services(a, &services, uptime);
    print_containers(a, &containers);
    print_ports(a, &ports, &tree, &containers);

    arena_free(a);
    return 0;
}
