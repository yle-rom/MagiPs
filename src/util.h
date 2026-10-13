#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* Arena: hand out memory from big blocks, free it all at once. */
typedef struct Arena Arena;     // opaque: fields live in util.c

Arena *arena_new(size_t block_size);
void  *arena_alloc(Arena *a, size_t size);       // zeroed memory
char  *arena_strdup(Arena *a, const char *s);
void   arena_free(Arena *a);

/*
 * Grow an array living in the arena. Call when len == *cap:
 *   if (list.len == cap)
 *       list.items = arena_grow(a, list.items, list.len, &cap, sizeof *list.items);
 * Doubles the capacity and copies; the old array stays in the arena until exit.
 */
void *arena_grow(Arena *a, void *items, size_t len, size_t *cap, size_t elem_size);

/* "/usr/bin/firefox" -> "firefox" */
const char *path_base(const char *path);

/* printf into a new arena string (cut at 1 KB). */
const char *arena_printf(Arena *a, const char *format, ...)
    __attribute__((format(printf, 2, 3)));   // lets the compiler check the arguments

/* 1536 -> "1M", 5400000 -> "5.1G" */
const char *format_mem(Arena *a, long kb);

/* ANSI color codes, used with paint(). */
#define STYLE_TITLE "1;36"      // bold cyan
#define STYLE_BOLD  "1"
#define STYLE_DIM   "2"
#define STYLE_GOOD  "32"        // green
#define STYLE_WARN  "33"        // yellow
#define STYLE_BAD   "31"        // red
#define STYLE_INFO  "34"        // blue

/* Colors on only for a terminal, unless NO_COLOR is set or TERM=dumb. */
void style_init(bool allowed);

/* text wrapped in the color, or text unchanged when colors are off. */
const char *paint(Arena *a, const char *style, const char *text);

/* Are colors on (after style_init)? */
bool style_on(void);

/* Color for a memory amount: yellow from 1 GB, red from 4 GB, else NULL. */
const char *mem_style(long kb);

#endif
