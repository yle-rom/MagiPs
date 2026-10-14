#ifndef WORLD_H
#define WORLD_H

#include "apps.h"
#include "db.h"
#include "devserver.h"
#include "docker.h"
#include "ports.h"
#include "procfs.h"
#include "services.h"
#include "tree.h"
#include "util.h"

/*
 * Where processes are read from: /proc, or $MAGIPS_PROC for tests and demos
 * (a fake /proc). With a fake one, nothing is ever really killed.
 */
const char *proc_root(void);
bool proc_is_fake(void);

/* Everything one scan finds. Lives in one arena, freed together. */
typedef struct {
    ProcList procs;
    double uptime;
    Tree tree;
    AppList apps;
    ServiceList services;
    ContainerList containers;
    PortList ports;
    DatabaseList databases;
    DevServerList devservers;
} World;

/* Read /proc, docker, ports and the config, and work out every section. */
void world_scan(Arena *a, World *w);

/* Seconds since a process started (start in clock ticks since boot). */
long world_age(unsigned long long start, double uptime);

/* The same as text: "2d4h", "3h12m", "22m", "40s". */
const char *world_age_text(Arena *a, unsigned long long start, double uptime);

/* Who owns a port: the process if we can see it, else a container publishing it, else NULL. */
const char *world_port_owner(Arena *a, const World *w, const PortSummary *p);

#endif
