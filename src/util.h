#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Arena: hand out memory from big blocks, free it all at once. */
typedef struct Arena Arena;     // opaque: fields live in util.c

Arena *arena_new(size_t block_size);
void  *arena_alloc(Arena *a, size_t size);       // zeroed memory
char  *arena_strdup(Arena *a, const char *s);
void   arena_free(Arena *a);

#endif
