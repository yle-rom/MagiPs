#include "procfs.h"

ProcList procfs_scan(Arena *a, const char *root)
{
    (void)a;
    (void)root;
    // TODO: opendir(root), for each numeric entry parse stat/status/cmdline
    return (ProcList){ 0 };
}
