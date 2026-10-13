#include "tree.h"
#include <stdlib.h>
#include <string.h>

static int cmp_pid(const void *x, const void *y)
{
    const TreePid *a = x, *b = y;
    return (a->pid > b->pid) - (a->pid < b->pid);
}

Tree tree_build(Arena *a, const ProcList *procs)
{
    size_t n = procs->len;
    Tree t = {
        .procs = procs,
        .parent = arena_alloc(a, n * sizeof *t.parent),
        .first_child = arena_alloc(a, n * sizeof *t.first_child),
        .next_sibling = arena_alloc(a, n * sizeof *t.next_sibling),
        .owner = arena_alloc(a, n * sizeof *t.owner),
        .by_pid = arena_alloc(a, n * sizeof *t.by_pid),
    };

    for (size_t i = 0; i < n; i++) {
        t.by_pid[i] = (TreePid){ procs->items[i].pid, i };
        t.first_child[i] = TREE_NONE;
        t.next_sibling[i] = TREE_NONE;
    }
    qsort(t.by_pid, n, sizeof *t.by_pid, cmp_pid);

    // walk backwards and prepend, so each child list ends up in scan order
    for (size_t i = n; i-- > 0;) {
        size_t p = tree_find(&t, procs->items[i].ppid);
        t.parent[i] = p;
        if (p != TREE_NONE) {
            t.next_sibling[i] = t.first_child[p];
            t.first_child[p] = i;
        }
    }
    return t;
}

size_t tree_find(const Tree *t, int pid)
{
    TreePid key = { .pid = pid };
    const TreePid *hit = bsearch(&key, t->by_pid, t->procs->len, sizeof key, cmp_pid);
    return hit ? hit->idx : TREE_NONE;
}

bool tree_is_kernel(const Proc *p)
{
    return p->pid == 2 || p->ppid == 2;
}

bool tree_in_container(const Proc *p)
{
    return strstr(p->cgroup, "/docker-") || strstr(p->cgroup, "/libpod-");
}

void members_add(Arena *a, Members *m, const Proc *p)
{
    if (m->len == m->cap)
        m->items = arena_grow(a, m->items, m->len, &m->cap, sizeof *m->items);
    m->items[m->len++] = (Member){ p->pid, p->start };
}

void members_append(Arena *a, Members *dst, const Members *src)
{
    for (size_t i = 0; i < src->len; i++) {
        if (dst->len == dst->cap)
            dst->items = arena_grow(a, dst->items, dst->len, &dst->cap, sizeof *dst->items);
        dst->items[dst->len++] = src->items[i];
    }
}

bool members_has(const Members *m, int pid)
{
    for (size_t i = 0; i < m->len; i++)
        if (m->items[i].pid == pid)
            return true;
    return false;
}

void tree_claim(Arena *a, Tree *t, size_t i, Owner owner, Members *m, long *mem_kb)
{
    if (t->owner[i] != OWNER_FREE)
        return;
    t->owner[i] = owner;
    members_add(a, m, &t->procs->items[i]);
    *mem_kb += t->procs->items[i].rss_kb;
    for (size_t c = t->first_child[i]; c != TREE_NONE; c = t->next_sibling[c])
        tree_claim(a, t, c, owner, m, mem_kb);
}
