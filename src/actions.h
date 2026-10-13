#ifndef ACTIONS_H
#define ACTIONS_H

#include "world.h"


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
PickResult actions_pick(Arena *a, const World *s, int *tab);

#endif
