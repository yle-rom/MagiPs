#ifndef TREE_H
#define TREE_H

#include <stdbool.h>
#include <stddef.h>
#include "procfs.h"
#include "util.h"

#define TREE_NONE ((size_t)-1)      // "no such process" index

/* Which section a process ended up in. Each process belongs to at most one. */
typedef enum {
    OWNER_FREE = 0,     // not claimed yet
    OWNER_APP,
    OWNER_SERVICE,
    OWNER_HIDDEN,       // containers, essentials, kernel: shown nowhere (yet)
} Owner;

typedef struct {
    int pid;
    size_t idx;
} TreePid;

/*
 * Parent/child links over a ProcList, all by index into procs->items.
 * Children of i: for (c = first_child[i]; c != TREE_NONE; c = next_sibling[c])
 */
typedef struct {
    const ProcList *procs;
    size_t *parent;
    size_t *first_child;
    size_t *next_sibling;
    Owner *owner;
    TreePid *by_pid;            // sorted by pid, for tree_find
} Tree;

Tree tree_build(Arena *a, const ProcList *procs);

/* Index of pid, or TREE_NONE. */
size_t tree_find(const Tree *t, int pid);

/* kthreadd (pid 2) and its children: kernel workers, not programs. */
bool tree_is_kernel(const Proc *p);

/*
 * Give i and all its still-free descendants to owner.
 * Adds the number of processes and their memory to *nprocs / *mem_kb.
 */
void tree_claim(Tree *t, size_t i, Owner owner, size_t *nprocs, long *mem_kb);

#endif
