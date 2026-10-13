#include "services.h"
#include <stdlib.h>
#include <string.h>

/*
 * "Needed for the laptop to work": hidden. Matched as name prefixes.
 * Edit these lists to taste.
 */
static const char *const system_essentials[] = {
    "systemd-", "dbus", "NetworkManager", "wpa_supplicant", "iwd", "polkit",
    "udisks2", "upower", "rtkit-daemon", "bluetooth", "accounts-daemon",
    "avahi-daemon", "power-profiles-daemon", "tlp", "thermald", "acpid",
    "ModemManager", "getty@", "serial-getty@", "ly@", "gdm", "sddm", "lightdm",
    "greetd", "user@", "user-runtime-dir@", NULL,
};

static const char *const user_essentials[] = {
    "dbus", "at-spi", "xdg-desktop-portal", "xdg-document-portal",
    "xdg-permission-store", "dconf", "gvfs", "user@", NULL,
};

static bool is_essential(const char *name, Scope scope)
{
    const char *const *list = scope == SCOPE_SYSTEM ? system_essentials : user_essentials;
    for (size_t i = 0; list[i]; i++)
        if (strncmp(name, list[i], strlen(list[i])) == 0)
            return true;
    return false;
}

/* Docker/podman containers get their own section. */
static bool is_container(const char *cgroup)
{
    return strstr(cgroup, "/docker-") || strstr(cgroup, "/libpod-");
}

/*
 * Last ".service" part of a cgroup path, without the suffix:
 *   /system.slice/docker.service                        -> "docker"
 *   /user.slice/.../user@1000.service/app.slice/x.service -> "x"
 *   /system.slice/systemd-udevd.service/udev            -> "systemd-udevd"
 * Writes into out; false if the process isn't in a service unit.
 */
static bool unit_name(const char *cgroup, char *out, size_t size)
{
    bool found = false;
    const char *s = cgroup;
    while (*s) {
        while (*s == '/')
            s++;
        size_t len = strcspn(s, "/");
        if (len > 8 && strncmp(s + len - 8, ".service", 8) == 0) {
            size_t n = len - 8 < size - 1 ? len - 8 : size - 1;
            memcpy(out, s, n);
            out[n] = '\0';
            found = true;
        }
        s += len;
    }
    return found;
}

/* Add a group to the list, or merge into an existing one with the same name. */
static void add(Arena *a, ServiceList *list, size_t *cap, const char *name, Scope scope,
                const Proc *main, size_t nprocs, long mem)
{
    for (size_t i = 0; i < list->len; i++) {
        Service *s = &list->items[i];
        if (s->scope == scope && strcmp(s->name, name) == 0) {
            s->nprocs += nprocs;
            s->mem_kb += mem;
            if (main->start < s->start) {
                s->pid = main->pid;
                s->start = main->start;
            }
            return;
        }
    }
    if (list->len == *cap)
        list->items = arena_grow(a, list->items, list->len, cap, sizeof *list->items);
    list->items[list->len++] = (Service){
        .name = arena_strdup(a, name),
        .scope = scope,
        .pid = main->pid,
        .nprocs = nprocs,
        .mem_kb = mem,
        .start = main->start,
    };
}

/* Claim only i itself (unit members are counted one by one). */
static void claim_one(Tree *t, size_t i, Owner owner, size_t *nprocs, long *mem)
{
    t->owner[i] = owner;
    *nprocs += 1;
    *mem += t->procs->items[i].rss_kb;
}

/* Claim i and descendants, but stop at processes that start their own group. */
static void claim_group(Tree *t, size_t i, const bool *is_root, size_t *nprocs, long *mem)
{
    claim_one(t, i, OWNER_SERVICE, nprocs, mem);
    for (size_t c = t->first_child[i]; c != TREE_NONE; c = t->next_sibling[c])
        if (t->owner[c] == OWNER_FREE && !is_root[c])
            claim_group(t, c, is_root, nprocs, mem);
}

static int cmp_service(const void *x, const void *y)
{
    const Service *a = x, *b = y;
    if (a->scope != b->scope)
        return (int)a->scope - (int)b->scope;
    return strcmp(a->name, b->name);
}

ServiceList services_build(Arena *a, Tree *t, int uid)
{
    const ProcList *procs = t->procs;
    size_t n = procs->len;
    ServiceList list = { 0 };
    size_t cap = 0;
    char unit[256];

    // pass 1: kernel, containers, and everything inside a systemd unit
    for (size_t i = 0; i < n; i++) {
        const Proc *p = &procs->items[i];
        if (t->owner[i] != OWNER_FREE)
            continue;
        if (p->pid == 1 || tree_is_kernel(p) || is_container(p->cgroup)) {
            t->owner[i] = OWNER_HIDDEN;
            continue;
        }
        if (!unit_name(p->cgroup, unit, sizeof unit))
            continue;

        Scope scope = strncmp(p->cgroup, "/system.slice/", 14) == 0 ? SCOPE_SYSTEM : SCOPE_USER;
        if (is_essential(unit, scope)) {
            t->owner[i] = OWNER_HIDDEN;
            continue;
        }
        size_t one = 0;
        long mem = 0;
        claim_one(t, i, OWNER_SERVICE, &one, &mem);
        add(a, &list, &cap, unit, scope, p, one, mem);
    }

    // pass 2: your remaining processes (session scope), grouped by tree.
    // A group starts at a process whose parent isn't a free process of yours,
    // or whose parent launches apps (the compositor): its children are
    // separate things (mako, waybar), not parts of one program.
    bool *launcher = arena_alloc(a, n * sizeof *launcher);
    for (size_t i = 0; i < n; i++) {
        size_t up = t->parent[i];
        if (up != TREE_NONE && t->owner[i] == OWNER_APP && t->owner[up] != OWNER_APP)
            launcher[up] = true;
    }

    bool *is_root = arena_alloc(a, n * sizeof *is_root);
    for (size_t i = 0; i < n; i++) {
        const Proc *p = &procs->items[i];
        if (t->owner[i] != OWNER_FREE || p->uid != uid)
            continue;
        size_t up = t->parent[i];
        is_root[i] = up == TREE_NONE || launcher[i] || launcher[up] ||
                     t->owner[up] != OWNER_FREE || procs->items[up].uid != uid;
    }

    for (size_t i = 0; i < n; i++) {
        if (!is_root[i] || t->owner[i] != OWNER_FREE)
            continue;
        const Proc *p = &procs->items[i];
        size_t nprocs = 0;
        long mem = 0;
        claim_group(t, i, is_root, &nprocs, &mem);
        if (!is_essential(p->comm, SCOPE_USER))
            add(a, &list, &cap, p->comm, SCOPE_USER, p, nprocs, mem);
    }

    // pass 3: no systemd (Alpine, Void...): other users' processes under PID 1
    for (size_t i = 0; i < n; i++) {
        const Proc *p = &procs->items[i];
        if (t->owner[i] != OWNER_FREE || p->uid == uid || p->ppid != 1 ||
            strstr(p->cgroup, ".slice"))
            continue;
        size_t nprocs = 0;
        long mem = 0;
        tree_claim(t, i, OWNER_SERVICE, &nprocs, &mem);
        if (!is_essential(p->comm, SCOPE_SYSTEM))
            add(a, &list, &cap, p->comm, SCOPE_SYSTEM, p, nprocs, mem);
    }

    qsort(list.items, list.len, sizeof *list.items, cmp_service);
    return list;
}
