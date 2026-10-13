#include "ports.h"

PortList ports_scan(Arena *a, const char *root, const ProcList *procs)
{
    (void)a;
    (void)root;
    (void)procs;
    // TODO: parse net/tcp*, then map socket inodes via each pid's fd/ dir
    return (PortList){ 0 };
}
