#include "db.h"
#include "test.h"

static const Database *find_db(const DatabaseList *l, const char *engine, int container)
{
    for (size_t i = 0; i < l->len; i++)
        if (strcmp(l->items[i].engine, engine) == 0 && l->items[i].container == container)
            return &l->items[i];
    return NULL;
}

void test_db(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    Tree tree = tree_build(a, &procs);
    Config cfg = config_parse(a, config_default_text, "defaults");
    ServiceList services = services_build(a, &tree, FIXTURE_UID, &cfg);
    PortList ports = ports_scan(a, FIXTURE_PROC, &procs);

    Container fake[] = {
        { .name = "app-mongo-1", .image = "mongo:7", .ports = "27017/tcp" },
        { .name = "pg", .image = "docker.io/library/postgres:16", .ports = "0.0.0.0:5433->5432/tcp" },
        { .name = "cache", .image = "myorg/cache:1", .ports = "6379/tcp" },
        { .name = "web", .image = "nginx:latest", .ports = "0.0.0.0:80->80/tcp" },
    };
    ContainerList containers = { fake, 4 };
    DatabaseList dbs = db_find(a, &tree, &services, &containers, &ports);

    CHECK(dbs.len == 4);                            // redis native + 3 containers, not nginx

    const Database *d = find_db(&dbs, "redis", -1);             // native, by process name
    CHECK(d && d->pid == 900 && d->port == 6379 && !d->guessed);
    CHECK(d && d->service && strcmp(d->service->name, "redis-server") == 0);

    CHECK(find_db(&dbs, "mongodb", -1) == NULL);    // mongod pid 700 is inside a container
    d = find_db(&dbs, "mongodb", 0);                            // by image
    CHECK(d && d->port == 27017 && d->port_internal);
    d = find_db(&dbs, "postgres", 1);                           // registry/owner/tag stripped
    CHECK(d && d->port == 5433 && !d->port_internal);           // published host port
    d = find_db(&dbs, "redis", 2);                              // unknown image, std port
    CHECK(d && d->guessed && d->port_internal);
    arena_free(a);
}
