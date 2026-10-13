#include "table.h"
#include <stdio.h>

struct Table {
    const char *title;
    int ncols;
    const char **headers;
    // TODO: rows + column widths
};

Table *table_new(Arena *a, const char *title, int ncols, const char **headers)
{
    Table *t = arena_alloc(a, sizeof *t);
    t->title = title;
    t->ncols = ncols;
    t->headers = headers;
    return t;
}

void table_row(Table *t, const char **cells)
{
    (void)t;
    (void)cells;
    // TODO: store a copy of the row, widen columns as needed
}

void table_print(const Table *t)
{
    printf("%s\n  (todo)\n\n", t->title);
    // TODO: print headers + rows padded to column widths
}
