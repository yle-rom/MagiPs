#include "table.h"
#include <stdio.h>
#include <string.h>

#define INDENT "  "
#define GAP 2

struct Table {
    Arena *a;
    const char *title;
    int ncols;
    const char **headers;
    const char ***rows;         // rows[r][c]
    size_t nrows, cap;
    size_t *width;              // widest cell per column, in characters
};

/* Characters, not bytes: "→" is 3 bytes in UTF-8 but 1 column on screen. */
static size_t text_width(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)     // skip UTF-8 continuation bytes
            n++;
    return n;
}

Table *table_new(Arena *a, const char *title, int ncols, const char **headers)
{
    Table *t = arena_alloc(a, sizeof *t);
    t->a = a;
    t->title = title;
    t->ncols = ncols;
    t->headers = headers;
    t->width = arena_alloc(a, (size_t)ncols * sizeof *t->width);
    for (int c = 0; c < ncols; c++)
        t->width[c] = text_width(headers[c]);
    return t;
}

void table_row(Table *t, const char **cells)
{
    const char **row = arena_alloc(t->a, (size_t)t->ncols * sizeof *row);
    for (int c = 0; c < t->ncols; c++) {
        row[c] = arena_strdup(t->a, cells[c]);
        size_t w = text_width(row[c]);
        if (w > t->width[c])
            t->width[c] = w;
    }
    if (t->nrows == t->cap)
        t->rows = arena_grow(t->a, t->rows, t->nrows, &t->cap, sizeof *t->rows);
    t->rows[t->nrows++] = row;
}

static void print_line(const Table *t, const char **cells)
{
    fputs(INDENT, stdout);
    for (int c = 0; c < t->ncols; c++) {
        fputs(cells[c], stdout);
        if (c < t->ncols - 1) {
            size_t pad = t->width[c] - text_width(cells[c]) + GAP;
            printf("%*s", (int)pad, "");
        }
    }
    putchar('\n');
}

void table_print(const Table *t)
{
    table_print_note(t, NULL);
}

void table_print_note(const Table *t, const char *note)
{
    printf("%s\n", t->title);
    if (t->nrows == 0) {
        printf(INDENT "(none)\n");
    } else {
        print_line(t, t->headers);
        for (size_t r = 0; r < t->nrows; r++)
            print_line(t, t->rows[r]);
    }
    if (note)
        printf(INDENT "%s\n", note);
    putchar('\n');
}
