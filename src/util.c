#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
