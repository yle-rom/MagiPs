#include "ports.h"
#include "test.h"

void test_ports(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    PortList ports = ports_scan(a, FIXTURE_PROC, &procs);

    CHECK(ports.len == 2);                          // port 22 is ESTABLISHED, not listening
    if (ports.len == 2) {
        CHECK(ports.items[0].port == 8080);
        CHECK_STR(ports.items[0].proto, "tcp");
        CHECK_STR(ports.items[0].addr, "127.0.0.1");
        CHECK(ports.items[0].pid == 200);           // found via fd -> socket:[5555]

        CHECK(ports.items[1].port == 9000);
        CHECK_STR(ports.items[1].proto, "tcp6");
        CHECK_STR(ports.items[1].addr, "::1");
        CHECK(ports.items[1].pid == 200);
    }
    arena_free(a);
}
