#ifndef ACTIONS_H
#define ACTIONS_H

#include "apps.h"
#include "db.h"
#include "devserver.h"
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
    const DatabaseList *databases;
    const DevServerList *devservers;
    const Tree *tree;
    const char *proc_root;
} Snapshot;

typedef enum { PICK_AGAIN, PICK_QUIT } PickResult;

/*
 * One round of interactive mode: pick something from apps/services/
 * containers/ports, confirm, then stop it the right way:
 *   apps, your own services  -> SIGTERM the whole group (SIGKILL if it won't die)
 *   containers               -> docker stop
 *   systemd user units       -> systemctl --user stop
 *   system services          -> print the sudo command to run yourself
 *   dev servers              -> SIGTERM the server and what it spawned
 *   ports, databases         -> whatever runs them
 * *tab: section to start on / where the user left off.
 * PICK_QUIT when the user pressed Esc; otherwise rescan and call again.
 */
PickResult actions_pick(Arena *a, const Snapshot *s, int *tab);

#endif
