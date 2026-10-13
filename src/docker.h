#ifndef DOCKER_H
#define DOCKER_H

#include <stddef.h>
#include "util.h"

typedef struct {
    char *id, *name, *image, *ports, *status;
} Container;

typedef struct {
    Container *items;
    size_t len;
} ContainerList;

/* Running containers via `docker ps`; empty list if docker is missing or not reachable. */
ContainerList docker_list(Arena *a);

/* Index of the container publishing host port `port` ("...:8085->8081/tcp"), or -1. */
int docker_find_port(const ContainerList *list, int port);

#endif
