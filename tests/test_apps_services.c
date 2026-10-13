#include "apps.h"
#include "services.h"
#include "test.h"

static const Service *find_service(const ServiceList *l, const char *name)
{
    for (size_t i = 0; i < l->len; i++)
        if (strcmp(l->items[i].name, name) == 0)
            return &l->items[i];
    return NULL;
}

void test_apps_services(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    Tree tree = tree_build(a, &procs);
    const char *dirs[] = { FIXTURE_APPS, NULL };
    DesktopIndex desk = desktop_load(a, dirs);

    AppList apps = apps_build(a, &tree, &desk, FIXTURE_UID);
    CHECK(apps.len == 2);
    if (apps.len == 2) {
        CHECK_STR(apps.items[0].name, "My App");    // biggest memory first
        CHECK(apps.items[0].pid == 200);
        CHECK(apps.items[0].members.len == 2);           // child with the weird name rolled up
        CHECK(apps.items[0].mem_kb == 1500);
        CHECK_STR(apps.items[1].name, "Term → nvim");
        CHECK(apps.items[1].members.len == 3);
    }

    Config cfg = config_parse(a, config_default_text, "defaults");
    ServiceList services = services_build(a, &tree, FIXTURE_UID, &cfg);
    CHECK(services.len == 5);

    const Service *s = find_service(&services, "docker");
    CHECK(s && s->scope == SCOPE_SYSTEM && s->pid == 600 && s->unit);
    s = find_service(&services, "pipewire");
    CHECK(s && s->scope == SCOPE_USER);
    s = find_service(&services, "bar");
    CHECK(s && s->members.len == 2 && !s->unit);                     // python3 child rolled up
    s = find_service(&services, "compositor");
    CHECK(s && s->members.len == 1);                     // launcher: children are separate
    CHECK(find_service(&services, "notify") != NULL);

    CHECK(find_service(&services, "dbus") == NULL);         // essential
    CHECK(find_service(&services, "mongod") == NULL);       // container
    CHECK(find_service(&services, "kworker/0:1") == NULL);  // kernel
    CHECK(find_service(&services, "systemd") == NULL);      // PID 1
    arena_free(a);
}
