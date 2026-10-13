#include "apps.h"

AppList apps_build(Arena *a, const ProcList *procs)
{
    (void)a;
    (void)procs;
    // TODO: link parents/children, find roots, roll up, filter
    return (AppList){ 0 };
}
