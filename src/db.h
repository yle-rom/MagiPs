#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include <stddef.h>
#include "docker.h"
#include "ports.h"
#include "services.h"
#include "tree.h"
#include "util.h"

typedef struct {
    const char *engine;         // "mongodb", "postgres"...
    int pid;                    // native: main process; container: -1
    int port;                   // 0 = unknown
    bool port_internal;         // container port not published to the host
    bool guessed;               // matched only by its standard port
    const Service *service;     // native: the service group it belongs to (or NULL)
    int container;              // index into containers, or -1
} Database;

typedef struct {
    Database *items;
    size_t len;
} DatabaseList;

/*
 * Database servers, native or in containers, matched by process name,
 * container image, or (as a guess) their standard port. Built-in list:
 * mongodb, mariadb, mysql, postgres, redis, valkey, memcached,
 * elasticsearch, clickhouse, couchdb.
 */
DatabaseList db_find(Arena *a, const Tree *t, const ServiceList *services,
                     const ContainerList *containers, const PortList *ports);

#endif
