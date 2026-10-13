#ifndef ACTIONS_H
#define ACTIONS_H

#include "apps.h"
#include "docker.h"
#include "ports.h"
#include "services.h"
#include "tree.h"
#include "util.h"

typedef struct {
    const AppList *apps;
    const ServiceList *services;
    const ContainerList *containers;
    const PortList *ports;
    const Tree *tree;
    const char *proc_root;
} Snapshot;

/*
 * Interactive mode: pick something from apps/services/containers/ports,
 * confirm, then stop it the right way:
 *   apps, your own services  -> SIGTERM the whole group (SIGKILL if it won't die)
 *   containers               -> docker stop
 *   systemd user units       -> systemctl --user stop
 *   system services          -> print the sudo command to run yourself
 *   ports                    -> whatever owns the port
 * Returns the process exit code.
 */
int actions_interactive(Arena *a, const Snapshot *s);

#endif
