#include "apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most recently started child of i, or TREE_NONE. */
static size_t newest_child(const Tree *t, size_t i)
{
    size_t best = TREE_NONE;
    for (size_t c = t->first_child[i]; c != TREE_NONE; c = t->next_sibling[c])
        if (best == TREE_NONE || t->procs->items[c].start > t->procs->items[best].start)
            best = c;
    return best;
}

/* Terminal -> shell -> job: "Alacritty → nvim" (or "→ zsh" when idle). */
static const char *terminal_label(Arena *a, const Tree *t, size_t root, const char *name)
{
    size_t shell = newest_child(t, root);
    if (shell == TREE_NONE)
        return name;
    size_t job = newest_child(t, shell);
    const Proc *p = &t->procs->items[job != TREE_NONE ? job : shell];

    size_t n = strlen(name) + strlen(p->comm) + 8;
    char *label = arena_alloc(a, n);
    snprintf(label, n, "%s → %s", name, p->comm);
    return label;
}

static int cmp_mem(const void *x, const void *y)
{
    const App *a = x, *b = y;
    return (a->mem_kb < b->mem_kb) - (a->mem_kb > b->mem_kb);   // descending
}

AppList apps_build(Arena *a, Tree *t, const DesktopIndex *d, int uid)
{
    const ProcList *procs = t->procs;
    const DesktopEntry **match = arena_alloc(a, procs->len * sizeof *match);
    for (size_t i = 0; i < procs->len; i++) {
        const Proc *p = &procs->items[i];
        if (p->uid == uid && p->pid != 1 && !tree_is_kernel(p))
            match[i] = desktop_match(d, p);
    }

    AppList list = { 0 };
    size_t cap = 0;
    for (size_t i = 0; i < procs->len; i++) {
        if (!match[i])
            continue;
        bool topmost = true;
        for (size_t up = t->parent[i]; up != TREE_NONE; up = t->parent[up])
            if (match[up]) {
                topmost = false;
                break;
            }
        if (!topmost)
            continue;

        const Proc *p = &procs->items[i];
        size_t nprocs = 0;
        long mem = 0;
        tree_claim(t, i, OWNER_APP, &nprocs, &mem);

        // same app started twice (spotify has 2 roots): one row.
        // terminals stay separate: each window runs something different.
        App *same = NULL;
        for (size_t j = 0; j < list.len && !match[i]->terminal; j++)
            if (list.items[j].entry == match[i])
                same = &list.items[j];
        if (same) {
            same->nprocs += nprocs;
            same->mem_kb += mem;
            if (p->start < same->start) {
                same->pid = p->pid;
                same->start = p->start;
            }
            continue;
        }

        if (list.len == cap)
            list.items = arena_grow(a, list.items, list.len, &cap, sizeof *list.items);
        list.items[list.len++] = (App){
            .name = match[i]->terminal ? terminal_label(a, t, i, match[i]->name)
                                       : match[i]->name,
            .pid = p->pid,
            .nprocs = nprocs,
            .mem_kb = mem,
            .start = p->start,
            .entry = match[i],
        };
    }

    qsort(list.items, list.len, sizeof *list.items, cmp_mem);
    return list;
}
