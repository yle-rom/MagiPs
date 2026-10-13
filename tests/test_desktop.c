#include "desktop.h"
#include "test.h"

void test_desktop(void)
{
    Arena *a = arena_new(4096);
    const char *dirs[] = { FIXTURE_APPS, NULL };
    DesktopIndex d = desktop_load(a, dirs);

    // myapp + term; helper is NoDisplay, script is "python3" (a wrapper),
    // and the [Desktop Action] Exec= line is ignored
    CHECK(d.len == 2);

    Proc myapp = { .comm = "something", .exe = "/usr/bin/myapp" };     // by exe
    const DesktopEntry *e = desktop_match(&d, &myapp);
    CHECK(e != NULL);
    if (e) {
        CHECK_STR(e->name, "My App");
        CHECK(!e->terminal);
    }

    Proc term = { .comm = "term", .exe = "" };                          // by comm
    e = desktop_match(&d, &term);
    CHECK(e != NULL);
    if (e)
        CHECK(e->terminal);

    Proc helper = { .comm = "compositor", .exe = "/usr/bin/compositor" };
    CHECK(desktop_match(&d, &helper) == NULL);
    Proc other = { .comm = "other-program", .exe = "/usr/bin/other-program" };
    CHECK(desktop_match(&d, &other) == NULL);
    arena_free(a);
}
