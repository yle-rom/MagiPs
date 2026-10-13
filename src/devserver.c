#include "devserver.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_COMMAND 40

/*
 * Programs that run or launch dev servers. Matched against the program's
 * file name and the first word of its process name ("npm run dev" runs on
 * node). Ending in '*' = prefix: python3.12, php8.3.
 */
static const char *const dev_programs[] = {
    "node", "npm", "npx", "pnpm", "yarn", "bun", "deno", "vite", "next-server",
    "python*", "uv", "uvicorn", "gunicorn", "flask", "poetry", "pipenv",
    "ruby", "bundle", "rails", "puma", "php*", "java", "gradle", "mvn",
    "go", "air", "cargo", "dotnet", "mix", "beam.smp", "elixir",
    "make", "just", "nodemon", "watchexec", "entr", NULL,
};

/* Glue between a runner and the real server: "npm run dev" -> sh -c vite -> node. */
static const char *const glue[] = { "sh", "bash", "dash", "zsh", "env", NULL };

static bool name_in(const char *name, const char *const *list)
{
    for (size_t i = 0; list[i]; i++) {
        size_t n = strlen(list[i]);
        if (list[i][n - 1] == '*' ? strncmp(name, list[i], n - 1) == 0 : strcmp(name, list[i]) == 0)
            return true;
    }
    return false;
}

static bool is_dev_program(const Proc *p)
{
    char first[16];
    snprintf(first, sizeof first, "%.*s", (int)strcspn(p->comm, " "), p->comm);
    return name_in(first, dev_programs) || (p->exe[0] && name_in(path_base(p->exe), dev_programs));
}

/* The shell a terminal (or tmux, ssh) opened: it leads its own session. */
static bool is_session_shell(const Proc *p)
{
    return p->pid == p->sid && name_in(p->comm, glue);
}

/*
 * Walk up from the listening process to the top of the server: through dev
 * programs and glue shells, stopping below your interactive shell, PID 1,
 * or anything that isn't part of the dev tooling.
 */
static size_t server_top(const Tree *t, size_t i, int uid)
{
    for (;;) {
        size_t up = t->parent[i];
        if (up == TREE_NONE)
            return i;
        const Proc *p = &t->procs->items[up];
        if (p->uid != uid || p->pid == 1 || is_session_shell(p) ||
            !(is_dev_program(p) || name_in(p->comm, glue)))
            return i;
        i = up;
    }
}

/* Re-parented to PID 1 or to your systemd --user: the terminal it ran in is gone. */
static bool is_orphan(const Tree *t, size_t top)
{
    size_t up = t->parent[top];
    if (up == TREE_NONE)
        return true;
    const Proc *p = &t->procs->items[up];
    return p->pid == 1 || strcmp(p->comm, "systemd") == 0;
}

/* Inside a GUI app (Spotify's ports) rather than a terminal? */
static bool in_gui_app(const AppList *apps, int pid)
{
    for (size_t i = 0; i < apps->len; i++)
        if (!apps->items[i].entry->terminal && members_has(&apps->items[i].members, pid))
            return true;
    return false;
}

static const char *project_dir(Arena *a, const char *root, int pid, const char *home)
{
    char path[512], dir[4096];
    snprintf(path, sizeof path, "%s/%d/cwd", root, pid);
    ssize_t n = readlink(path, dir, sizeof dir - 1);
    if (n <= 0)
        return "?";
    dir[n] = '\0';

    size_t h = home ? strlen(home) : 0;
    if (h > 1 && strncmp(dir, home, h) == 0 && (dir[h] == '/' || dir[h] == '\0'))
        return arena_printf(a, "~%s", dir + h);
    return arena_strdup(a, dir);
}

/*
 * The readable part of a command line:
 *   "/usr/bin/node /home/u/web/node_modules/.bin/vite --port 5173" -> "vite --port 5173"
 *   "/usr/bin/python3 -m http.server"                              -> "python3 -m http.server"
 */
static const char *short_command(Arena *a, const char *cmdline)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", cmdline);
    char *arg0 = buf, *rest = strchr(buf, ' ');
    if (rest)
        *rest++ = '\0';

    const char *prog = path_base(arg0);
    if (rest && (strcmp(prog, "node") == 0 || strcmp(prog, "bun") == 0) && rest[0] == '/') {
        char *script_end = strchr(rest, ' ');       // node <script>: the script is the program
        if (script_end)
            *script_end++ = '\0';
        prog = path_base(rest);
        rest = script_end;
    }
    char out[4096];
    snprintf(out, sizeof out, "%s%s%s", prog, rest && *rest ? " " : "", rest ? rest : "");
    if (strlen(out) > MAX_COMMAND)
        strcpy(out + MAX_COMMAND - 3, "...");
    return arena_strdup(a, out);
}

static void collect(Arena *a, const Tree *t, size_t i, Members *m)
{
    members_add(a, m, &t->procs->items[i]);
    for (size_t c = t->first_child[i]; c != TREE_NONE; c = t->next_sibling[c])
        collect(a, t, c, m);
}

DevServerList dev_find(Arena *a, const Tree *t, const AppList *apps, const PortList *ports,
                       const char *root, int uid, const char *home)
{
    DevServerList list = { 0 };
    size_t cap = 0;
    PortSummary *sums = arena_alloc(a, (ports->len + 1) * sizeof *sums);
    size_t nsums = ports_summarize(ports, sums);

    for (size_t k = 0; k < nsums; k++) {           // sorted by port: lowest first
        if (sums[k].pid < 0)
            continue;
        size_t i = tree_find(t, sums[k].pid);
        if (i == TREE_NONE)
            continue;
        const Proc *p = &t->procs->items[i];
        if (p->uid != uid || strstr(p->cgroup, ".service") || in_gui_app(apps, p->pid))
            continue;

        size_t top = server_top(t, i, uid);
        const Proc *tp = &t->procs->items[top];
        bool from_terminal = t->parent[top] != TREE_NONE &&
                             is_session_shell(&t->procs->items[t->parent[top]]);
        bool dev = false;
        for (size_t j = i; !dev; j = t->parent[j]) {
            dev = is_dev_program(&t->procs->items[j]);
            if (j == top)
                break;
        }
        if (!dev && !from_terminal)
            continue;                               // e.g. ollama started by the compositor

        DevServer *same = NULL;                     // a second port of a known server
        for (size_t s = 0; s < list.len; s++)
            if (list.items[s].pid == tp->pid)
                same = &list.items[s];
        if (same) {
            same->ports = arena_printf(a, "%s, %d", same->ports, sums[k].port);
            continue;
        }

        if (list.len == cap)
            list.items = arena_grow(a, list.items, list.len, &cap, sizeof *list.items);
        DevServer *d = &list.items[list.len++];
        *d = (DevServer){
            .pid = tp->pid,
            .start = tp->start,
            .project = project_dir(a, root, tp->pid, home),
            .command = short_command(a, tp->cmdline[0] ? tp->cmdline : tp->comm),
            .ports = arena_printf(a, "%d", sums[k].port),
            .orphaned = is_orphan(t, top),
        };
        collect(a, t, top, &d->members);
    }
    return list;
}
