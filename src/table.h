#ifndef TABLE_H
#define TABLE_H

#include "util.h"

typedef struct Table Table;     // opaque: fields live in table.c

Table *table_new(Arena *a, const char *title, int ncols, const char **headers);
void   table_row(Table *t, const char **cells);
void   table_print(const Table *t);

#endif
