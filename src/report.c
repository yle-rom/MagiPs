#include "report.h"
#include <stdbool.h>
#include <string.h>
#include "table.h"

static const char *paint_mem(Arena *a, long kb)
{
    const char *style = mem_style(kb);
    return style ? paint(a, style, format_mem(a, kb)) : format_mem(a, kb);
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
            world_age_text(a, app->start, uptime),
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
            world_age_text(a, s->start, uptime),
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

static void print_devservers(Arena *a, const DevServerList *devs, double uptime)
{
    const char *cols[] = { "PROJECT", "COMMAND", "PORT", "PID", "UPTIME", "" };
    Table *t = table_new(a, "DEV SERVERS", 6, cols);
    for (size_t i = 0; i < devs->len; i++) {
        const DevServer *d = &devs->items[i];
        const char *row[] = {
            paint(a, STYLE_BOLD, d->project),
            d->command,
            d->ports,
            arena_printf(a, "%d", d->pid),
            world_age_text(a, d->start, uptime),
            d->orphaned ? paint(a, STYLE_WARN, "orphaned") : "",
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
        bool trouble = strstr(c->status, "unhealthy") || strstr(c->status, "Restarting");
        const char *status = paint(a, trouble ? STYLE_BAD : STYLE_GOOD, c->status);
        const char *row[] = { paint(a, STYLE_BOLD, c->name), c->image, c->ports, status };
        table_row(t, row);
    }
    table_print(t);
}

/* Ports nobody visible owns are system plumbing: hidden, only counted. */
static void print_ports(Arena *a, const World *w)
{
    const char *cols[] = { "PORT", "OPEN TO", "PID", "OWNER" };
    Table *t = table_new(a, "PORTS", 4, cols);
    PortSummary *sums = arena_alloc(a, (w->ports.len + 1) * sizeof *sums);
    size_t n = ports_summarize(&w->ports, sums), hidden = 0;

    for (size_t i = 0; i < n; i++) {
        const char *owner = world_port_owner(a, w, &sums[i]);
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

void report_tables(Arena *a, const World *w)
{
    print_apps(a, &w->apps, w->uptime);
    print_services(a, &w->services, w->uptime);
    print_devservers(a, &w->devservers, w->uptime);
    print_databases(a, &w->databases, &w->containers);
    print_containers(a, &w->containers);
    print_ports(a, w);
}
