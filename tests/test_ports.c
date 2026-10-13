#include "ports.h"
#include "test.h"

void test_ports(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    PortList ports = ports_scan(a, FIXTURE_PROC, &procs);

    CHECK(ports.len == 3);                          // port 22 is ESTABLISHED, not listening
    if (ports.len == 3) {                           // sorted by port number
        CHECK(ports.items[0].port == 6379);
        CHECK(ports.items[0].pid == 900);           // redis-server, via socket:[8888]

        CHECK(ports.items[1].port == 8080);
        CHECK_STR(ports.items[1].proto, "tcp");
        CHECK_STR(ports.items[1].addr, "127.0.0.1");
        CHECK(ports.items[1].pid == 200);           // found via fd -> socket:[5555]

        CHECK(ports.items[2].port == 9000);
        CHECK_STR(ports.items[2].proto, "tcp6");
        CHECK_STR(ports.items[2].addr, "::1");
        CHECK(ports.items[2].pid == 200);
    }

    Port local = { .addr = "127.0.0.1" }, any = { .addr = "::" }, bridge = { .addr = "192.168.122.1" };
    CHECK(port_reach(&local) == REACH_LOCAL);
    CHECK(port_reach(&any) == REACH_NETWORK);
    CHECK(port_reach(&bridge) == REACH_ADDRESS);
    arena_free(a);
}
