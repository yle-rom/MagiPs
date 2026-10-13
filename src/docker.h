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

/* Running containers; empty list if docker is missing or not reachable. */
ContainerList docker_list(Arena *a);

#endif
