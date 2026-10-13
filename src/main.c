#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "actions.h"
#include "apps.h"
#include "config.h"
#include "db.h"
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

/* Memory: yellow from 1 GB, red from 4 GB. */
static const char *paint_mem(Arena *a, long kb)
{
    const char *text = format_mem(a, kb);
    if (kb >= 4L * 1024 * 1024)
        return paint(a, STYLE_BAD, text);
    if (kb >= 1024L * 1024)
        return paint(a, STYLE_WARN, text);
    return text;
}

static void print_apps(Arena *a, const AppList *apps, double uptime)
{
    const char *cols[] = { "APP", "PID", "PROCS", "MEM", "UPTIME" };
    Table *t = table_new(a, "APPS", 5, cols);
    for (size_t i = 0; i < apps->len; i++) {
        const App *app = &apps->items[i];
        const char *row[] = {
            paint(a, STYLE_BOLD, app->name),
            arena_printf(a, "%d", app->pid),
            arena_printf(a, "%zu", app->members.len),
            paint_mem(a, app->mem_kb),
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
            s->scope == SCOPE_SYSTEM ? paint(a, STYLE_INFO, "system") : "user",
            arena_printf(a, "%d", s->pid),
            arena_printf(a, "%zu", s->members.len),
            paint_mem(a, s->mem_kb),
            format_age(a, s->start, uptime),
        };
        table_row(t, row);
    }
    table_print(t);
}

/* "docker: name" / "system service: mariadb" / "user: redis-server" / "process" */
static const char *db_where(Arena *a, const Database *d, const ContainerList *containers)
{
    if (d->container >= 0)
        return arena_printf(a, "docker: %s", containers->items[d->container].name);
    if (d->service)
        return arena_printf(a, "%s service: %s",
                            d->service->scope == SCOPE_SYSTEM ? "system" : "user", d->service->name);
    return "process";
}

static void print_databases(Arena *a, const DatabaseList *dbs, const ContainerList *containers)
{
    const char *cols[] = { "DATABASE", "WHERE", "PID", "PORT" };
    Table *t = table_new(a, "DATABASES", 4, cols);
    bool guessed = false;
    for (size_t i = 0; i < dbs->len; i++) {
        const Database *d = &dbs->items[i];
        guessed |= d->guessed;
        const char *row[] = {
            d->guessed ? paint(a, STYLE_WARN, arena_printf(a, "%s?", d->engine))
                       : paint(a, STYLE_BOLD, d->engine),
            db_where(a, d, containers),
            d->pid >= 0 ? arena_printf(a, "%d", d->pid) : "-",
            !d->port ? "-" : d->port_internal ? arena_printf(a, "%d (internal)", d->port)
                                              : arena_printf(a, "%d", d->port),
        };
        table_row(t, row);
    }
    table_print_note(t, guessed ? "(? = guessed from its port)" : NULL);
}

static void print_containers(Arena *a, const ContainerList *containers)
{
    const char *cols[] = { "CONTAINER", "IMAGE", "PORTS", "STATUS" };
    Table *t = table_new(a, "CONTAINERS", 4, cols);
    for (size_t i = 0; i < containers->len; i++) {
        const Container *c = &containers->items[i];
        bool trouble = strstr(c->status, "unhealthy") || strstr(c->status, "Restarting");
        const char *status = paint(a, trouble ? STYLE_BAD : STYLE_GOOD, c->status);
        const char *row[] = { paint(a, STYLE_BOLD, c->name), c->image, c->ports, status };
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
            sums[i].reach == REACH_NETWORK ? paint(a, STYLE_WARN, "network")   // reachable by others
                                           : port_reach_label(&sums[i]),
            sums[i].pid >= 0 ? arena_printf(a, "%d", sums[i].pid) : "-",
            owner,
        };
        table_row(t, row);
    }
    table_print_note(t, hidden ? arena_printf(a, "(%zu system ports hidden)", hidden) : NULL);
}

static void usage(FILE *out)
{
    fputs("usage: whatsup [-i] [--no-color]\n"
          "  (no option)  print apps, services, databases, containers and ports\n"
          "  -i           interactive: pick things and stop them (esc to quit)\n"
          "  --no-color   plain output (also: NO_COLOR=1, or piping to a file)\n"
          "  -h           this help\n"
          "config: ~/.config/whatsup/config (created on first run)\n", out);
}

/* Everything one scan finds. Lives in one arena, freed together. */
typedef struct {
    ProcList procs;
    double uptime;
    Tree tree;
    AppList apps;
    ServiceList services;
    ContainerList containers;
    PortList ports;
    DatabaseList databases;
} World;

static void scan(Arena *a, World *w)
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
}

/* Pick, act, rescan, repeat until Esc. A fresh arena per round. */
static int interactive_loop(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "whatsup: interactive mode needs a terminal\n");
        return 2;
    }
    int tab = 0;
    for (;;) {
        Arena *a = arena_new(1 << 16);
        World w;
        scan(a, &w);
        Snapshot snap = { &w.apps, &w.services, &w.containers, &w.ports, &w.databases,
                          &w.tree, PROC_ROOT };
        PickResult r = actions_pick(a, &snap, &tab);
        arena_free(a);
        if (r == PICK_QUIT)
            return 0;
    }
}

int main(int argc, char **argv)
{
    bool interactive = false, color = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            color = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "whatsup: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    style_init(color);
    if (interactive)
        return interactive_loop();

    Arena *a = arena_new(1 << 16);     // 64 KB blocks
    World w;
    scan(a, &w);
    print_apps(a, &w.apps, w.uptime);
    print_services(a, &w.services, w.uptime);
    print_databases(a, &w.databases, &w.containers);
    print_containers(a, &w.containers);
    print_ports(a, &w.ports, &w.tree, &w.containers);
    arena_free(a);
    return 0;
}
