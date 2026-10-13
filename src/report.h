#ifndef REPORT_H
#define REPORT_H

#include "world.h"

/* The colored tables: apps, services, dev servers, databases, containers, ports. */
void report_tables(Arena *a, const World *w);

/* Everything as JSON, including the system ports the tables hide. */
void report_json(Arena *a, const World *w);

#endif
