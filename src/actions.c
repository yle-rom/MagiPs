#include "actions.h"
#include "info.h"
#include "picker.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_KILL_PIDS 6              // info panel: list this many pids in a kill command, at most
#define TERM_WAIT_MS 3000           // how long to wait after SIGTERM before offering SIGKILL

enum {
    TAB_ALL, TAB_APPS, TAB_SERVICES, TAB_DEV, TAB_DATABASES, TAB_CONTAINERS, TAB_PORTS, NTABS
};
static const char *const tabs[NTABS] = {
    "All", "Apps", "Services", "Dev", "Databases", "Containers", "Ports",
};
static const char *const heads[NTABS][PICK_COLS] = {
    [TAB_ALL]        = { "NAME", "KIND", "PID", "INFO", "DETAIL" },
    [TAB_APPS]       = { "APP", NULL, "PID", "PROCS", "MEM" },
    [TAB_SERVICES]   = { "SERVICE", "SCOPE", "PID", "PROCS", "MEM" },
    [TAB_DEV]        = { "PROJECT", NULL, "PID", "PORT", "COMMAND" },
    [TAB_DATABASES]  = { "DATABASE", NULL, "PID", "PORT", "WHERE" },
    [TAB_CONTAINERS] = { "CONTAINER", NULL, "PID", "STATUS", "IMAGE" },
    [TAB_PORTS]      = { "PORT", NULL, "PID", "OPEN TO", "OWNER" },
};

typedef enum {
    ACT_KILL,       // signal every process of a group ourselves
    ACT_RUN,        // run a command for the user (docker stop, systemctl --user stop)
    ACT_SHOW,       // needs root: print the command instead of running it
} ActKind;

typedef struct {
    ActKind kind;
    const char *what;               // "Spotify", "docker (system service)"
    const Members *members;         // ACT_KILL
    const char *argv[5];            // ACT_RUN
    const char *command;            // ACT_RUN / ACT_SHOW: shown to the user
    const char *note;               // ACT_SHOW: why we don't do it ourselves
} Action;

/* What a menu item is, so its info panel can be built when asked for. */
typedef struct {
    int tab;                        // TAB_APPS...: which list idx points into
    size_t idx;
    PortSummary port;               // TAB_PORTS: the port itself
} Ref;

typedef struct {
    PickItem *items;
    Action *acts;
    Ref *refs;
    size_t len, cap, acts_cap, refs_cap;
} Menu;

static void menu_add(Arena *a, Menu *m, PickItem item, Action act, Ref ref)
{
    if (m->len == m->cap)
        m->items = arena_grow(a, m->items, m->len, &m->cap, sizeof *m->items);
    if (m->len == m->acts_cap)
        m->acts = arena_grow(a, m->acts, m->len, &m->acts_cap, sizeof *m->acts);
    if (m->len == m->refs_cap)
        m->refs = arena_grow(a, m->refs, m->len, &m->refs_cap, sizeof *m->refs);
    m->items[m->len] = item;
    m->acts[m->len] = act;
    m->refs[m->len] = ref;
    m->len++;
}

/* ---- what "stop" means for each kind of thing ---- */

static Action app_action(const App *app)
{
    return (Action){ .kind = ACT_KILL, .what = app->name, .members = &app->members };
}

static Action container_action(Arena *a, const Container *c)
{
    return (Action){
        .kind = ACT_RUN,
        .what = c->name,
        .argv = { "docker", "stop", c->name, NULL },
        .command = arena_printf(a, "docker stop %s", c->name),
    };
}

static Action service_action(Arena *a, const Service *s)
{
    if (s->unit && s->scope == SCOPE_USER)
        return (Action){
            .kind = ACT_RUN,
            .what = s->name,
            .argv = { "systemctl", "--user", "stop", s->name, NULL },
            .command = arena_printf(a, "systemctl --user stop %s", s->name),
        };
    if (s->unit)
        return (Action){
            .kind = ACT_SHOW,
            .what = s->name,
            .command = arena_printf(a, "sudo systemctl stop %s", s->name),
            .note = "a system service: killing it would just make systemd restart it.",
        };
    if (s->scope == SCOPE_SYSTEM)
        return (Action){
            .kind = ACT_SHOW,
            .what = s->name,
            .command = arena_printf(a, "sudo kill %d", s->pid),
            .note = "owned by another user, so it needs root.",
        };
    return (Action){ .kind = ACT_KILL, .what = s->name, .members = &s->members };
}

/* A port is stopped by stopping whatever owns it. false = owner unknown. */
static Action dev_action(Arena *a, const DevServer *d)
{
    return (Action){ .kind = ACT_KILL, .what = arena_printf(a, "%s in %s", d->command, d->project),
                     .members = &d->members };
}

static bool port_action(Arena *a, const World *s, const PortSummary *p, Action *out,
                        const char **owner)
{
    if (p->pid >= 0) {
        // dev servers first: one in a terminal is also part of that terminal's app,
        // and stopping the port shouldn't close the whole window
        for (size_t i = 0; i < s->devservers.len; i++)
            if (members_has(&s->devservers.items[i].members, p->pid)) {
                *out = dev_action(a, &s->devservers.items[i]);
                *owner = arena_printf(a, "dev: %s", s->devservers.items[i].command);
                return true;
            }
        for (size_t i = 0; i < s->apps.len; i++)
            if (members_has(&s->apps.items[i].members, p->pid)) {
                *out = app_action(&s->apps.items[i]);
                *owner = s->apps.items[i].name;
                return true;
            }
        for (size_t i = 0; i < s->services.len; i++)
            if (members_has(&s->services.items[i].members, p->pid)) {
                *out = service_action(a, &s->services.items[i]);
                *owner = s->services.items[i].name;
                return true;
            }
        return false;
    }
    int c = docker_find_port(&s->containers, p->port);
    if (c < 0)
        return false;
    *out = container_action(a, &s->containers.items[c]);
    *owner = arena_printf(a, "docker: %s", s->containers.items[c].name);
    return true;
}

/* A database is stopped like the thing running it. */
static Action db_action(Arena *a, const World *s, const Database *d)
{
    if (d->container >= 0)
        return container_action(a, &s->containers.items[d->container]);
    if (d->service)
        return service_action(a, d->service);

    Members *one = arena_alloc(a, sizeof *one);     // a lone process: just that pid
    size_t i = tree_find(&s->tree, d->pid);
    if (i != TREE_NONE)
        members_add(a, one, &s->tree.procs->items[i]);
    return (Action){ .kind = ACT_KILL, .what = d->engine, .members = one };
}

static Menu build_menu(Arena *a, const World *s)
{
    Menu m = { 0 };

    for (size_t i = 0; i < s->apps.len; i++) {
        const App *app = &s->apps.items[i];
        PickItem item = { TAB_APPS, "app", { app->name, NULL, arena_printf(a, "%d", app->pid),
                          arena_printf(a, "%3zu procs", app->members.len),
                          format_mem(a, app->mem_kb) },
                          { STYLE_BOLD, NULL, NULL, NULL, mem_style(app->mem_kb) } };
        menu_add(a, &m, item, app_action(app), (Ref){ TAB_APPS, i, { 0 } });
    }
    for (size_t i = 0; i < s->services.len; i++) {
        const Service *sv = &s->services.items[i];
        PickItem item = { TAB_SERVICES, "service", { sv->name, sv->scope == SCOPE_SYSTEM ? "system" : "user",
                          arena_printf(a, "%d", sv->pid),
                          arena_printf(a, "%3zu procs", sv->members.len),
                          format_mem(a, sv->mem_kb) },
                          { STYLE_BOLD, sv->scope == SCOPE_SYSTEM ? STYLE_INFO : STYLE_DIM, NULL,
                            NULL, mem_style(sv->mem_kb) } };
        menu_add(a, &m, item, service_action(a, sv), (Ref){ TAB_SERVICES, i, { 0 } });
    }
    for (size_t i = 0; i < s->devservers.len; i++) {
        const DevServer *d = &s->devservers.items[i];
        PickItem item = { TAB_DEV, d->orphaned ? "dev, orphaned" : "dev", { d->project, NULL,
                          arena_printf(a, "%d", d->pid), arena_printf(a, ":%s", d->ports),
                          d->command },
                          { d->orphaned ? STYLE_WARN : STYLE_BOLD, NULL, NULL, NULL, NULL } };
        menu_add(a, &m, item, dev_action(a, d), (Ref){ TAB_DEV, i, { 0 } });
    }
    for (size_t i = 0; i < s->databases.len; i++) {
        const Database *d = &s->databases.items[i];
        const char *where = d->container >= 0 ? s->containers.items[d->container].name
                            : d->service      ? d->service->name
                                              : "process";
        PickItem item = { TAB_DATABASES, "database", {
            d->guessed ? arena_printf(a, "%s?", d->engine) : d->engine, NULL,
            d->pid >= 0 ? arena_printf(a, "%d", d->pid) : "-",
            !d->port ? "" : d->port_internal ? arena_printf(a, ":%d internal", d->port)
                                             : arena_printf(a, ":%d", d->port),
            where },
            { d->guessed ? STYLE_WARN : STYLE_BOLD, NULL, NULL, NULL, NULL } };
        menu_add(a, &m, item, db_action(a, s, d), (Ref){ TAB_DATABASES, i, { 0 } });
    }
    for (size_t i = 0; i < s->containers.len; i++) {
        const Container *c = &s->containers.items[i];
        bool trouble = strstr(c->status, "unhealthy") || strstr(c->status, "Restarting");
        PickItem item = { TAB_CONTAINERS, "container", { c->name, NULL, "-", c->status, c->image },
                          { STYLE_BOLD, NULL, NULL, trouble ? STYLE_BAD : STYLE_GOOD, NULL } };
        menu_add(a, &m, item, container_action(a, c), (Ref){ TAB_CONTAINERS, i, { 0 } });
    }

    PortSummary *sums = arena_alloc(a, (s->ports.len + 1) * sizeof *sums);
    size_t n = ports_summarize(&s->ports, sums);
    for (size_t i = 0; i < n; i++) {
        Action act;
        const char *owner;
        if (!port_action(a, s, &sums[i], &act, &owner))
            continue;                               // system plumbing: not listed
        PickItem item = { TAB_PORTS, "port", { arena_printf(a, ":%d", sums[i].port), NULL,
                          sums[i].pid >= 0 ? arena_printf(a, "%d", sums[i].pid) : "-",
                          port_reach_label(&sums[i]), owner },
                          { STYLE_BOLD, NULL, NULL,
                            sums[i].reach == REACH_NETWORK ? STYLE_WARN : NULL, NULL } };
        menu_add(a, &m, item, act, (Ref){ TAB_PORTS, i, sums[i] });
    }
    return m;
}

/* ---- doing it ---- */

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Does the group contain the terminal/shell we're running in? */
static bool includes_my_terminal(const World *s, const Members *m)
{
    size_t i = tree_find(&s->tree, getpid());
    for (i = i != TREE_NONE ? s->tree.parent[i] : TREE_NONE; i != TREE_NONE; i = s->tree.parent[i])
        if (members_has(m, s->tree.procs->items[i].pid))
            return true;
    return false;
}

static size_t count_alive(const Members *m, const bool *skip)
{
    size_t alive = 0;
    for (size_t i = 0; i < m->len; i++)
        if (!skip[i] && procfs_alive(proc_root(), m->items[i].pid, m->items[i].start))
            alive++;
    return alive;
}

/*
 * kill(), except on a fake /proc (demo/tests): a fake pid could be a real
 * process here, so instead the process is only marked dead in the fake tree.
 */
static int send_signal(int pid, int sig)
{
    if (!proc_is_fake())
        return kill(pid, sig);
    procfs_mark_dead(proc_root(), pid);
    return 0;
}

static void signal_all(const Members *m, const bool *skip, int sig)
{
    for (size_t i = 0; i < m->len; i++)
        if (!skip[i] && procfs_alive(proc_root(), m->items[i].pid, m->items[i].start))
            send_signal(m->items[i].pid, sig);
}

/*
 * SIGTERM asks politely (apps can save and clean up). If something is still
 * there after a few seconds, offer SIGKILL, which can't be ignored.
 */
static int do_kill(Arena *a, const Action *act)
{
    const Members *m = act->members;
    bool *skip = arena_alloc(a, (m->len + 1) * sizeof *skip);
    char denied[512] = "";
    size_t ndenied = 0;

    for (size_t i = 0; i < m->len; i++) {
        const Member *mb = &m->items[i];
        if (mb->pid == getpid() || !procfs_alive(proc_root(), mb->pid, mb->start)) {
            skip[i] = true;
            continue;
        }
        if (send_signal(mb->pid, SIGTERM) < 0) {
            skip[i] = true;
            if (errno == EPERM) {                   // e.g. a root helper inside the app
                size_t len = strlen(denied);
                snprintf(denied + len, sizeof denied - len, " %d", mb->pid);
                ndenied++;
            }
        }
    }

    size_t alive = count_alive(m, skip);
    for (long waited = 0; alive > 0 && waited < TERM_WAIT_MS; waited += 100) {
        sleep_ms(100);
        alive = count_alive(m, skip);
    }
    if (alive > 0 && picker_confirm(arena_printf(a, "%zu process%s still running. Force kill?",
                                                 alive, alive == 1 ? "" : "es"))) {
        signal_all(m, skip, SIGKILL);
        sleep_ms(200);
        alive = count_alive(m, skip);
    }

    if (alive == 0)
        printf("Stopped %s.\n", act->what);
    else
        printf("%zu process%s of %s still running.\n", alive, alive == 1 ? "" : "es", act->what);
    if (ndenied)
        printf("%zu process%s belong%s to another user. To stop %s:\n  sudo kill%s\n",
               ndenied, ndenied == 1 ? "" : "es", ndenied == 1 ? "s" : "",
               ndenied == 1 ? "it" : "them", denied);
    return alive || ndenied ? 1 : 0;
}

static int do_run(const Action *act)
{
    printf("$ %s\n", act->command);
    fflush(stdout);

    pid_t pid = fork();                             // copy of this process...
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        execvp(act->argv[0], (char *const *)act->argv);     // ...replaced by the command
        perror(act->argv[0]);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* The command that does what stopping act does, for the info panel. */
static const char *stop_text(Arena *a, const World *s, const Action *act)
{
    if (act->kind == ACT_RUN)
        return act->command;
    if (act->kind == ACT_SHOW)
        return arena_printf(a, "%s   (%s)", act->command, act->note);
    const Members *m = act->members;
    if (m->len == 0)
        return NULL;
    if (m->len > MAX_KILL_PIDS) {                   // too many to list: let pstree find them
        int top = m->items[0].pid;
        for (size_t i = 0; i < m->len; i++) {       // the one whose parent isn't in the group
            size_t t = tree_find(&s->tree, m->items[i].pid);
            if (t != TREE_NONE && !members_has(m, s->procs.items[t].ppid)) {
                top = m->items[i].pid;
                break;
            }
        }
        return arena_printf(a, "kill $(pstree -p %d | grep -o '([0-9]*)' | tr -d '()')   "
                               "(%zu processes)", top, m->len);
    }
    const char *pids = "";
    for (size_t i = 0; i < m->len; i++)
        pids = arena_printf(a, "%s %d", pids, m->items[i].pid);
    return arena_printf(a, "kill%s", pids);
}

typedef struct {
    Arena *a;
    const World *s;
    const Menu *m;
} InfoCtx;

static const char *describe(void *ctx, size_t i)
{
    const InfoCtx *c = ctx;
    const World *s = c->s;
    const Ref *r = &c->m->refs[i];
    const char *stop = stop_text(c->a, s, &c->m->acts[i]);
    switch (r->tab) {
    case TAB_APPS:       return info_app(c->a, s, &s->apps.items[r->idx], stop);
    case TAB_SERVICES:   return info_service(c->a, s, &s->services.items[r->idx], stop);
    case TAB_DEV:        return info_dev(c->a, s, &s->devservers.items[r->idx], stop);
    case TAB_DATABASES:  return info_database(c->a, s, &s->databases.items[r->idx], stop);
    case TAB_CONTAINERS: return info_container(c->a, s, &s->containers.items[r->idx], stop);
    default:             return info_port(c->a, s, &r->port, stop);
    }
}

PickResult actions_pick(Arena *a, const World *s, int *tab)
{
    Menu m = build_menu(a, s);
    InfoCtx ctx = { a, s, &m };
    int choice = picker_run(m.items, m.len, tabs, heads, NTABS, tab, describe, &ctx);
    if (choice < 0)
        return PICK_QUIT;                           // Esc
    const Action *act = &m.acts[choice];

    switch (act->kind) {
    case ACT_SHOW:
        printf("%s is %s\nRun:\n  %s\n", act->what, act->note, act->command);
        break;

    case ACT_RUN:
        if (picker_confirm(arena_printf(a, "Stop %s with `%s`?", act->what, act->command)))
            do_run(act);
        break;

    case ACT_KILL: {
        size_t n = act->members->len;
        const char *warn = includes_my_terminal(s, act->members)
                               ? "\n  ⚠ this includes the terminal you're typing in." : "";
        if (picker_confirm(arena_printf(a, "Stop %s (%zu process%s)?%s", act->what, n,
                                        n == 1 ? "" : "es", warn)))
            do_kill(a, act);
        break;
    }
    }
    putchar('\n');                                 // gap before the picker comes back
    return PICK_AGAIN;
}
