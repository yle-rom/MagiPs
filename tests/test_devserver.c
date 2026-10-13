#include "devserver.h"
#include "test.h"

static const DevServer *find_dev(const DevServerList *l, int pid)
{
    for (size_t i = 0; i < l->len; i++)
        if (l->items[i].pid == pid)
            return &l->items[i];
    return NULL;
}

void test_devserver(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    Tree tree = tree_build(a, &procs);
    const char *dirs[] = { FIXTURE_APPS, NULL };
    DesktopIndex desk = desktop_load(a, dirs);
    AppList apps = apps_build(a, &tree, &desk, FIXTURE_UID);
    PortList ports = ports_scan(a, FIXTURE_PROC, &procs);

    DevServerList devs = dev_find(a, &tree, &apps, &ports, FIXTURE_PROC, FIXTURE_UID, "/home/u");
    CHECK(devs.len == 2);       // not myapp (GUI app), not redis-server (not dev, not from a terminal)

    // node (listening) -> sh -c -> "npm run dev": the top is npm, stopped below the shell
    const DevServer *d = find_dev(&devs, 310);
    CHECK(d != NULL);
    if (d) {
        CHECK_STR(d->project, "~/code/web");
        CHECK_STR(d->command, "npm run dev");
        CHECK_STR(d->ports, "5173");
        CHECK(!d->orphaned);
        CHECK(d->members.len == 3);                 // npm, sh, node
    }

    d = find_dev(&devs, 950);
    CHECK(d != NULL);
    if (d) {
        CHECK_STR(d->project, "~/tmp");
        CHECK_STR(d->command, "python3 -m http.server 8000");
        CHECK(d->orphaned);                         // parent is PID 1: its terminal is gone
    }
    arena_free(a);
}
