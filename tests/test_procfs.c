#include "procfs.h"
#include "test.h"

static const Proc *find(const ProcList *l, int pid)
{
    for (size_t i = 0; i < l->len; i++)
        if (l->items[i].pid == pid)
            return &l->items[i];
    return NULL;
}

void test_procfs(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);

    CHECK(procs.len == 21);                         // "net" and "uptime" are not pids

    const Proc *app = find(&procs, 200);
    CHECK(app != NULL);
    if (app) {
        CHECK(app->ppid == 100);
        CHECK(app->uid == 1000);
        CHECK(app->rss_kb == 1000);
        CHECK(app->start == 20);
        CHECK(app->state == 'S');
        CHECK_STR(app->comm, "myapp");
        CHECK_STR(app->cmdline, "myapp --flag");    // '\0' separators -> spaces
        CHECK_STR(app->exe, "/usr/bin/myapp");
        CHECK_STR(app->cgroup, "/user.slice/user-1000.slice/session-1.scope");
    }

    const Proc *weird = find(&procs, 201);          // name with spaces and ')'
    CHECK(weird != NULL);
    if (weird) {
        CHECK_STR(weird->comm, "My (weird) name");
        CHECK(weird->ppid == 200);
    }

    const Proc *zsh = find(&procs, 301);            // the terminal's shell leads its session
    CHECK(zsh && zsh->sid == 301);

    const Proc *kworker = find(&procs, 10);         // kernel thread: no memory, no cmdline
    CHECK(kworker != NULL);
    if (kworker) {
        CHECK(kworker->rss_kb == 0);
        CHECK_STR(kworker->cmdline, "");
        CHECK_STR(kworker->exe, "");
    }

    CHECK(procfs_uptime(FIXTURE_PROC) == 1000.0);
    CHECK(procfs_alive(FIXTURE_PROC, 200, 20));
    CHECK(!procfs_alive(FIXTURE_PROC, 200, 99));    // same pid, different process
    CHECK(!procfs_alive(FIXTURE_PROC, 12345, 20));  // gone
    CHECK(procfs_scan(a, "tests/fixtures/missing").len == 0);
    arena_free(a);
}
