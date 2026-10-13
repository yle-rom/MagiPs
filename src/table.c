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

/*
 * Columns on screen, not bytes: "→" is 3 bytes in UTF-8 but 1 column, and
 * color codes ("\x1b[33m") take no space at all.
 */
static size_t text_width(const char *s)
{
    size_t n = 0;
    while (*s) {
        if (*s == '\x1b' && s[1] == '[') {           // skip ESC [ ... letter
            s += 2;
            while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
                s++;
            if (*s)
                s++;
            continue;
        }
        if (((unsigned char)*s & 0xC0) != 0x80)     // skip UTF-8 continuation bytes
            n++;
        s++;
    }
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

static void print_line(const Table *t, const char **cells, const char *style)
{
    fputs(INDENT, stdout);
    for (int c = 0; c < t->ncols; c++) {
        fputs(style ? paint(t->a, style, cells[c]) : cells[c], stdout);
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
    printf("%s\n", paint(t->a, STYLE_TITLE, t->title));
    if (t->nrows == 0) {
        printf(INDENT "%s\n", paint(t->a, STYLE_DIM, "(none)"));
    } else {
        print_line(t, t->headers, STYLE_DIM);
        for (size_t r = 0; r < t->nrows; r++)
            print_line(t, t->rows[r], NULL);
    }
    if (note)
        printf(INDENT "%s\n", paint(t->a, STYLE_DIM, note));
    putchar('\n');
}
