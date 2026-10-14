#ifndef INFO_H
#define INFO_H

#include "world.h"

/*
 * "What is this thing?": the text the picker shows for one item before you
 * stop it. What it runs, where from, who started it, what it holds, and the
 * commands to check on it or stop it yourself. Lines end in '\n'; colored.
 *
 * stop is the command magips would run (or tell you to run) for it.
 * The service and container ones ask systemctl / docker for a few details.
 */
const char *info_app(Arena *a, const World *w, const App *app, const char *stop);
const char *info_service(Arena *a, const World *w, const Service *s, const char *stop);
const char *info_dev(Arena *a, const World *w, const DevServer *d, const char *stop);
const char *info_database(Arena *a, const World *w, const Database *d, const char *stop);
const char *info_container(Arena *a, const World *w, const Container *c, const char *stop);
const char *info_port(Arena *a, const World *w, const PortSummary *p, const char *stop);

#endif
