#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Block Block;
struct Block {
    Block *next;
    size_t used, cap;
    _Alignas(max_align_t) unsigned char data[];     // block memory follows header
};

struct Arena {
    Block *head;            // newest block; older ones linked behind it
    size_t block_size;      // default size for new blocks
};

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    return p;
}

Arena *arena_new(size_t block_size)
{
    Arena *a = xmalloc(sizeof *a);
    a->head = NULL;
    a->block_size = block_size;
    return a;
}

void *arena_alloc(Arena *a, size_t size)
{
    const size_t align = _Alignof(max_align_t);
    size = (size + align - 1) & ~(align - 1);       // round up to alignment

    Block *b = a->head;
    if (!b || b->cap - b->used < size) {            // no room: new block
        size_t cap = size > a->block_size ? size : a->block_size;
        b = xmalloc(sizeof *b + cap);
        b->next = a->head;
        b->used = 0;
        b->cap = cap;
        a->head = b;
    }

    void *p = b->data + b->used;
    b->used += size;
    return memset(p, 0, size);
}

char *arena_strdup(Arena *a, const char *s)
{
    size_t n = strlen(s) + 1;                       // +1 for the '\0'
    return memcpy(arena_alloc(a, n), s, n);
}

void arena_free(Arena *a)
{
    Block *b = a->head;
    while (b) {
        Block *next = b->next;                      // save before freeing
        free(b);
        b = next;
    }
    free(a);
}

void *arena_grow(Arena *a, void *items, size_t len, size_t *cap, size_t elem_size)
{
    size_t new_cap = *cap ? *cap * 2 : 16;
    void *bigger = arena_alloc(a, new_cap * elem_size);
    if (len)
        memcpy(bigger, items, len * elem_size);
    *cap = new_cap;
    return bigger;
}

const char *path_base(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *arena_printf(Arena *a, const char *format, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof buf, format, args);
    va_end(args);
    return arena_strdup(a, buf);
}

const char *format_mem(Arena *a, long kb)
{
    if (kb >= 1024 * 1024)
        return arena_printf(a, "%.1fG", kb / (1024.0 * 1024.0));
    if (kb >= 1024)
        return arena_printf(a, "%ldM", kb / 1024);
    return arena_printf(a, "%ldK", kb);
}

static bool colors;

void style_init(bool allowed)
{
    const char *no_color = getenv("NO_COLOR");      // https://no-color.org
    const char *term = getenv("TERM");
    colors = allowed && isatty(STDOUT_FILENO) && !(no_color && *no_color) &&
             !(term && strcmp(term, "dumb") == 0);
}

const char *paint(Arena *a, const char *style, const char *text)
{
    if (!colors)
        return text;
    return arena_printf(a, "\x1b[%sm%s\x1b[0m", style, text);
}

bool style_on(void)
{
    return colors;
}

const char *mem_style(long kb)
{
    if (kb >= 4L * 1024 * 1024)
        return STYLE_BAD;
    if (kb >= 1024L * 1024)
        return STYLE_WARN;
    return NULL;
}
