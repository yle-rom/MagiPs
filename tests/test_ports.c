#include "ports.h"
#include "test.h"

static const Port *find_port(const PortList *l, int port)
{
    for (size_t i = 0; i < l->len; i++)
        if (l->items[i].port == port)
            return &l->items[i];
    return NULL;
}

void test_ports(void)
{
    Arena *a = arena_new(4096);
    ProcList procs = procfs_scan(a, FIXTURE_PROC);
    PortList ports = ports_scan(a, FIXTURE_PROC, &procs);

    CHECK(ports.len == 5);                          // port 22 is ESTABLISHED, not listening
    const int order[] = { 5173, 6379, 8000, 8080, 9000 };      // sorted by port number
    for (size_t i = 0; i < ports.len && i < 5; i++)
        CHECK(ports.items[i].port == order[i]);

    const Port *p = find_port(&ports, 8080);
    CHECK(p && p->pid == 200 && strcmp(p->proto, "tcp") == 0);  // via fd -> socket:[5555]
    CHECK(p && strcmp(p->addr, "127.0.0.1") == 0);
    p = find_port(&ports, 9000);
    CHECK(p && p->pid == 200 && strcmp(p->proto, "tcp6") == 0 && strcmp(p->addr, "::1") == 0);
    p = find_port(&ports, 6379);
    CHECK(p && p->pid == 900);
    p = find_port(&ports, 5173);
    CHECK(p && p->pid == 312);

    Port local = { .addr = "127.0.0.1" }, any = { .addr = "::" }, bridge = { .addr = "192.168.122.1" };
    CHECK(port_reach(&local) == REACH_LOCAL);
    CHECK(port_reach(&any) == REACH_NETWORK);
    CHECK(port_reach(&bridge) == REACH_ADDRESS);
    arena_free(a);
}
