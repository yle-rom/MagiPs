#include "docker.h"
#include <stdio.h>
#include <string.h>

#define FIELDS 5

/* Tab-separated so names/ports with spaces or commas stay in one field. */
static const char *const command =
    "docker ps --format '{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.Ports}}\t{{.Status}}' 2>/dev/null";

ContainerList docker_list(Arena *a)
{
    ContainerList list = { 0 };
    size_t cap = 0;

    FILE *out = popen(command, "r");                // run it, read its stdout
    if (!out)
        return list;

    char line[4096];
    while (fgets(line, sizeof line, out)) {
        line[strcspn(line, "\n")] = '\0';

        char *field[FIELDS];
        char *rest = line;
        int n = 0;
        while (n < FIELDS && rest)
            field[n++] = strsep(&rest, "\t");      // splits in place on each tab
        if (n != FIELDS)
            continue;

        if (list.len == cap)
            list.items = arena_grow(a, list.items, list.len, &cap, sizeof *list.items);
        list.items[list.len++] = (Container){
            .id = arena_strdup(a, field[0]),
            .name = arena_strdup(a, field[1]),
            .image = arena_strdup(a, field[2]),
            .ports = arena_strdup(a, field[3]),
            .status = arena_strdup(a, field[4]),
        };
    }
    pclose(out);
    return list;
}

int docker_find_port(const ContainerList *list, int port)
{
    char needle[32];
    snprintf(needle, sizeof needle, ":%d->", port);
    for (size_t i = 0; i < list->len; i++)
        if (strstr(list->items[i].ports, needle))
            return (int)i;
    return -1;
}
