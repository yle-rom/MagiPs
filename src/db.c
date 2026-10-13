#include "db.h"
#include <stdio.h>
#include <string.h>

/*
 * What each engine looks like. Facts, not preferences, so they live in the
 * code: an update teaches every user about a new engine.
 *   procs:   process names (comm, so max 15 chars)
 *   cmdline: for engines running on a VM like java/erlang, a marker in the args
 *   images:  container image names, without registry/owner/tag
 */
typedef struct {
    const char *engine;
    const char *procs[3];
    const char *cmdline;
    const char *images[5];
    int port;
} Engine;

static const Engine engines[] = {
    { "mongodb",       { "mongod" },                 NULL,
      { "mongo", "mongodb", "mongodb-community-server" },                    27017 },
    { "mariadb",       { "mariadbd" },               NULL, { "mariadb" },     3306 },
    { "mysql",         { "mysqld" },                 NULL, { "mysql", "percona" }, 3306 },
    { "postgres",      { "postgres", "postmaster" }, NULL,
      { "postgres", "postgresql", "postgis", "timescaledb" },                 5432 },
    { "redis",         { "redis-server" },           NULL, { "redis", "redis-stack" }, 6379 },
    { "valkey",        { "valkey-server" },          NULL, { "valkey" },      6379 },
    { "memcached",     { "memcached" },              NULL, { "memcached" },   11211 },
    { "elasticsearch", { "java" },  "org.elasticsearch", { "elasticsearch" }, 9200 },
    { "clickhouse",    { "clickhouse-serv", "clickhouse" }, NULL,
      { "clickhouse-server", "clickhouse" },                                  8123 },
    { "couchdb",       { "beam.smp" },  "couchdb",       { "couchdb" },       5984 },
};
#define NENGINES (sizeof engines / sizeof engines[0])

static const Engine *match_process(const Proc *p)
{
    for (size_t e = 0; e < NENGINES; e++)
        for (size_t i = 0; i < 3 && engines[e].procs[i]; i++)
            if (strcmp(p->comm, engines[e].procs[i]) == 0 &&
                (!engines[e].cmdline || strstr(p->cmdline, engines[e].cmdline)))
                return &engines[e];
    return NULL;
}

/*
 * "docker.io/library/postgres:16" / "bitnami/mongodb@sha256:..." -> "postgres" / "mongodb":
 * the last path part, without :tag or @digest.
 */
static void image_base(const char *image, char *out, size_t size)
{
    const char *base = strrchr(image, '/');
    base = base ? base + 1 : image;
    size_t len = strcspn(base, ":@");
    snprintf(out, size, "%.*s", (int)len, base);
}

static const Engine *match_image(const char *image)
{
    char base[128];
    image_base(image, base, sizeof base);
    for (size_t e = 0; e < NENGINES; e++)
        for (size_t i = 0; i < 5 && engines[e].images[i]; i++)
            if (strcmp(base, engines[e].images[i]) == 0)
                return &engines[e];
    return NULL;
}

/*
 * Where a container's database port goes, from docker's Ports text:
 *   "0.0.0.0:5433->5432/tcp" -> 5433 (published)   "27017/tcp" -> 27017, internal
 */
static int container_port(const char *ports, int port, bool *internal)
{
    char needle[32];
    snprintf(needle, sizeof needle, "->%d/", port);
    const char *arrow = strstr(ports, needle);
    if (arrow) {
        const char *p = arrow;
        while (p > ports && p[-1] >= '0' && p[-1] <= '9')
            p--;
        *internal = false;
        int host = 0;
        sscanf(p, "%d", &host);
        return host;
    }
    snprintf(needle, sizeof needle, "%d/", port);
    for (const char *p = ports; (p = strstr(p, needle)) != NULL; p++)
        if (p == ports || p[-1] < '0' || p[-1] > '9') {    // whole number, not 127017
            *internal = true;
            return port;
        }
    return 0;
}

static const Engine *engine_for_port(int port)
{
    for (size_t e = 0; e < NENGINES; e++)
        if (engines[e].port == port)
            return &engines[e];
    return NULL;
}

/* Is pid the process at idx, or one of its descendants? */
static bool in_subtree(const Tree *t, int pid, size_t idx)
{
    for (size_t i = tree_find(t, pid); i != TREE_NONE; i = t->parent[i])
        if (i == idx)
            return true;
    return false;
}

static void push(Arena *a, DatabaseList *l, size_t *cap, Database d)
{
    if (l->len == *cap)
        l->items = arena_grow(a, l->items, l->len, cap, sizeof *l->items);
    l->items[l->len++] = d;
}

DatabaseList db_find(Arena *a, const Tree *t, const ServiceList *services,
                     const ContainerList *containers, const PortList *ports)
{
    DatabaseList list = { 0 };
    size_t cap = 0;
    PortSummary *sums = arena_alloc(a, (ports->len + 1) * sizeof *sums);
    size_t nsums = ports_summarize(ports, sums);
    bool *port_used = arena_alloc(a, (nsums + 1) * sizeof *port_used);

    // native: topmost matching process (postgres has many children)
    for (size_t i = 0; i < t->procs->len; i++) {
        const Proc *p = &t->procs->items[i];
        const Engine *e = match_process(p);
        size_t up = t->parent[i];
        if (!e || tree_in_container(p) || (up != TREE_NONE && match_process(&t->procs->items[up]) == e))
            continue;

        Database d = { .engine = e->engine, .pid = p->pid, .container = -1 };
        for (size_t s = 0; s < services->len && !d.service; s++)
            if (members_has(&services->items[s].members, p->pid))
                d.service = &services->items[s];
        // its port: one it visibly owns, else its standard port if nobody visible owns that
        for (size_t k = 0; k < nsums && !d.port; k++)
            if ((sums[k].pid >= 0 && in_subtree(t, sums[k].pid, i)) ||
                (sums[k].pid < 0 && sums[k].port == e->port)) {
                d.port = sums[k].port;
                port_used[k] = true;
            }
        push(a, &list, &cap, d);
    }

    // containers: by image, else by a standard database port inside it
    for (size_t c = 0; c < containers->len; c++) {
        const Container *ct = &containers->items[c];
        const Engine *e = match_image(ct->image);
        Database d = { .pid = -1, .container = (int)c };
        if (e) {
            d.engine = e->engine;
            d.port = container_port(ct->ports, e->port, &d.port_internal);
        } else {
            for (size_t k = 0; k < NENGINES && !d.engine; k++) {
                int port = container_port(ct->ports, engines[k].port, &d.port_internal);
                if (port) {
                    d.engine = engines[k].engine;
                    d.port = port;
                    d.guessed = true;
                }
            }
            if (!d.engine)
                continue;
        }
        for (size_t k = 0; k < nsums; k++)
            if (sums[k].port == d.port && !d.port_internal)
                port_used[k] = true;
        push(a, &list, &cap, d);
    }

    // a renamed binary on a standard port, owned by a process we can see
    for (size_t k = 0; k < nsums; k++) {
        const Engine *e = engine_for_port(sums[k].port);
        if (port_used[k] || !e || sums[k].pid < 0 || docker_find_port(containers, sums[k].port) >= 0)
            continue;
        Database d = { .engine = e->engine, .pid = sums[k].pid, .port = sums[k].port,
                       .guessed = true, .container = -1 };
        for (size_t s = 0; s < services->len && !d.service; s++)
            if (members_has(&services->items[s].members, d.pid))
                d.service = &services->items[s];
        push(a, &list, &cap, d);
    }
    return list;
}
