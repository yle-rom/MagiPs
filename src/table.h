#ifndef TABLE_H
#define TABLE_H

#include "util.h"

typedef struct Table Table;     // opaque: fields live in table.c

/* headers must have ncols entries; the table keeps the pointers. */
Table *table_new(Arena *a, const char *title, int ncols, const char **headers);

/* Copies the ncols cells, so callers can reuse their buffers. */
void   table_row(Table *t, const char **cells);

/* Title, then headers and rows padded to the widest cell of each column. */
void   table_print(const Table *t);

#endif
