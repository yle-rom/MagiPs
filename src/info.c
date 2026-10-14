#include "info.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_NAMES     4             // "procs  9: spotify ×8, zygote"

/* ---- a growing string in the arena (arena_printf stops at 1 KB) ---- */

typedef struct {
    Arena *a;
    char *buf;
    size_t len, cap;
} Text;

static void add(Text *t, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void add(Text *t, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (t->len + (size_t)n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap : 1024;
        while (t->len + (size_t)n + 1 > cap)
            cap *= 2;
        char *buf = arena_alloc(t->a, cap);
        if (t->len)
            memcpy(buf, t->buf, t->len);
        t->buf = buf;
        t->cap = cap;
    }
    va_start(ap, format);
    vsnprintf(t->buf + t->len, t->cap - t->len, format, ap);
    va_end(ap);
    t->len += (size_t)n;
}

/* "  label     value" with the label dimmed; skipped when value is empty. */
static void field(Text *t, const char *label, const char *value)
{
    if (value && value[0])
        add(t, "  %s %s\n", paint(t->a, STYLE_DIM, arena_printf(t->a, "%-9s", label)), value);
}

/* A command to copy: highlighted so it stands out from the facts above. */
static void command(Text *t, const char *label, const char *cmd)
{
    if (cmd && cmd[0])
        add(t, "  %s %s\n", paint(t->a, STYLE_DIM, arena_printf(t->a, "%-9s", label)),
            paint(t->a, STYLE_INFO, cmd));
}

static void title(Text *t, const char *name, const char *kind)
{
    add(t, "%s  %s\n", paint(t->a, STYLE_BOLD, name), paint(t->a, STYLE_DIM, kind));
}

/* ---- facts about a process, read on demand ---- */

static const char *home_short(Arena *a, const char *path)
{
    const char *home = getenv("HOME");
    size_t h = home ? strlen(home) : 0;
    if (h > 1 && strncmp(path, home, h) == 0 && (path[h] == '/' || path[h] == '\0'))
        return arena_printf(a, "~%s", path + h);
    return path;
}

/* Working directory; NULL when we may not look (another user's process). */
static const char *proc_cwd(Arena *a, int pid)
{
    char path[512], dir[4096];
    snprintf(path, sizeof path, "%s/%d/cwd", proc_root(), pid);
    ssize_t n = readlink(path, dir, sizeof dir - 1);
    if (n <= 0)
        return NULL;
    dir[n] = '\0';
    return home_short(a, arena_strdup(a, dir));
}

static const Proc *find_proc(const World *w, int pid)
{
    size_t i = tree_find(&w->tree, pid);
    return i == TREE_NONE ? NULL : &w->procs.items[i];
}

/* Who started it: "Hyprland → alacritty → zsh" (up to 4 ancestors, PID 1 left out). */
static const char *ancestors(Arena *a, const World *w, int pid)
{
    size_t i = tree_find(&w->tree, pid);
    if (i == TREE_NONE)
        return NULL;
    const char *chain[4];
    int n = 0;
    bool more = false;
    for (size_t p = w->tree.parent[i]; p != TREE_NONE; p = w->tree.parent[p]) {
        const Proc *pp = &w->procs.items[p];
        if (pp->pid <= 1)
            break;
        if (n == 4) {
            more = true;
            break;
        }
        chain[n++] = pp->comm;
    }
    if (n == 0)
        return "systemd (PID 1)";
    const char *out = more ? "… → " : "";
    for (int k = n - 1; k >= 0; k--)
        out = arena_printf(a, "%s%s%s", out, chain[k], k ? " → " : "");
    return out;
}

/* "9: spotify ×8, zygote": how many processes and what they are. */
static const char *proc_names(Arena *a, const World *w, const Members *m)
{
    const char *name[MAX_NAMES + 1];
    size_t count[MAX_NAMES + 1] = { 0 };
    int distinct = 0;
    bool more = false;
    for (size_t i = 0; i < m->len; i++) {
        const Proc *p = find_proc(w, m->items[i].pid);
        if (!p)
            continue;
        int k = 0;
        while (k < distinct && strcmp(name[k], p->comm) != 0)
            k++;
        if (k == distinct) {
            if (distinct == MAX_NAMES) {
                more = true;
                continue;
            }
            name[distinct++] = p->comm;
        }
        count[k]++;
    }
    const char *out = arena_printf(a, "%zu:", m->len);
    for (int k = 0; k < distinct; k++)
        out = count[k] > 1 ? arena_printf(a, "%s%s %s ×%zu", out, k ? "," : "", name[k], count[k])
                           : arena_printf(a, "%s%s %s", out, k ? "," : "", name[k]);
    return more ? arena_printf(a, "%s, …", out) : out;
}

/* Ports the group listens on: ":3131 local, :8080 network". */
static const char *group_ports(Arena *a, const World *w, const Members *m)
{
    PortSummary *sums = arena_alloc(a, (w->ports.len + 1) * sizeof *sums);
    size_t n = ports_summarize(&w->ports, sums);
    const char *out = "";
    for (size_t i = 0; i < n; i++)
        if (sums[i].pid >= 0 && members_has(m, sums[i].pid))
            out = arena_printf(a, "%s%s:%d %s", out, out[0] ? ", " : "", sums[i].port,
                               port_reach_label(&sums[i]));
    return out;
}

static long group_mem(const World *w, const Members *m)
{
    long kb = 0;
    for (size_t i = 0; i < m->len; i++) {
        const Proc *p = find_proc(w, m->items[i].pid);
        if (p)
            kb += p->rss_kb;
    }
    return kb;
}

/* Everything we know about a process group from /proc: what it runs, from where, who started it. */
static void process_facts(Text *t, const World *w, int pid, const Members *m)
{
    Arena *a = t->a;
    const Proc *p = find_proc(w, pid);
    if (!p)
        return;
    field(t, "command", p->cmdline[0] ? p->cmdline : p->comm);
    if (p->exe[0] && strcmp(p->exe, p->cmdline) != 0)
        field(t, "program", p->exe);
    field(t, "folder", proc_cwd(a, pid));
    field(t, "started", arena_printf(a, "%s ago by %s", world_age_text(a, p->start, w->uptime),
                                     ancestors(a, w, pid)));
    if (m && m->len) {
        long kb = group_mem(w, m);
        const char *style = mem_style(kb);
        const char *mem = style ? paint(a, style, format_mem(a, kb)) : format_mem(a, kb);
        field(t, "procs", arena_printf(a, "%s  (%s)", proc_names(a, w, m), mem));
        field(t, "listens", group_ports(a, w, m));
    }
}

/* The usual "check / stop" footer for a plain process group. */
static void process_commands(Text *t, int pid, const char *stop)
{
    add(t, "\n");
    command(t, "status", arena_printf(t->a, "ps -o pid,stat,etime,rss,args -p %d", pid));
    command(t, "tree", arena_printf(t->a, "pstree -ap %d", pid));
    command(t, "files", arena_printf(t->a, "lsof -p %d", pid));
    command(t, "stop", stop);
}

/* ---- systemd and docker details: asked for only when you open the panel ---- */

/* Run cmd and return its first line, or NULL. Never with a fake /proc: nothing real to ask. */
static char *first_line(Arena *a, const char *cmd)
{
    if (proc_is_fake())
        return NULL;
    FILE *out = popen(cmd, "r");
    if (!out)
        return NULL;
    char line[4096];
    char *got = fgets(line, sizeof line, out) ? line : NULL;
    pclose(out);
    if (!got)
        return NULL;
    line[strcspn(line, "\n")] = '\0';
    return arena_strdup(a, line);
}

/* "Description\tFragmentPath\tUnitFileState" of a unit, split into out[3]. */
static bool unit_details(Arena *a, const Service *s, char *out[3])
{
    char *line = first_line(a, arena_printf(a,
        "systemctl %sshow -p Description -p FragmentPath -p UnitFileState '%s.service' 2>/dev/null"
        " | sort | cut -d= -f2- | paste -sd '\\t'",          // sorted: Description, Fragment, State
        s->scope == SCOPE_USER ? "--user " : "", s->name));
    if (!line)
        return false;
    for (int i = 0; i < 3; i++)
        out[i] = strsep(&line, "\t");
    return out[2] != NULL;
}

static void unit_facts(Text *t, const Service *s)
{
    Arena *a = t->a;
    char *d[3];
    if (unit_details(a, s, d)) {
        field(t, "unit", arena_printf(a, "%s.service — %s", s->name, d[0]));
        field(t, "file", home_short(a, d[1]));
        field(t, "on boot", strcmp(d[2], "enabled") == 0 ? "starts automatically (enabled)" : d[2]);
    } else {
        field(t, "unit", arena_printf(a, "%s.service", s->name));
    }
}

/* ---- one per section ---- */

const char *info_app(Arena *a, const World *w, const App *app, const char *stop)
{
    Text t = { .a = a };
    title(&t, app->name, "app");
    process_facts(&t, w, app->pid, &app->members);
    process_commands(&t, app->pid, stop);
    return t.buf;
}

const char *info_service(Arena *a, const World *w, const Service *s, const char *stop)
{
    Text t = { .a = a };
    const char *user = s->scope == SCOPE_USER ? "--user " : "";
    title(&t, s->name, s->unit ? (s->scope == SCOPE_USER ? "user service" : "system service")
                               : "background process");
    if (s->unit)
        unit_facts(&t, s);
    process_facts(&t, w, s->pid, &s->members);
    if (!s->unit) {
        process_commands(&t, s->pid, stop);
        return t.buf;
    }
    add(&t, "\n");
    command(&t, "status", arena_printf(a, "systemctl %sstatus %s", user, s->name));
    command(&t, "logs", arena_printf(a, "journalctl %s-u %s -e", user, s->name));
    command(&t, "stop", stop);
    command(&t, "for good", arena_printf(a, "%ssystemctl %sdisable --now %s",
                                         s->scope == SCOPE_USER ? "" : "sudo ", user, s->name));
    return t.buf;
}

const char *info_dev(Arena *a, const World *w, const DevServer *d, const char *stop)
{
    Text t = { .a = a };
    title(&t, d->project, d->orphaned ? "dev server, orphaned (its terminal is gone)" : "dev server");
    process_facts(&t, w, d->pid, &d->members);
    process_commands(&t, d->pid, stop);
    return t.buf;
}

static void container_facts(Text *t, const Container *c)
{
    Arena *a = t->a;
    field(t, "image", c->image);
    field(t, "state", c->status);
    field(t, "ports", c->ports[0] ? c->ports : "none published");
    char *line = first_line(a, arena_printf(a,
        "docker inspect -f '{{index .Config.Labels \"com.docker.compose.project.working_dir\"}}\t"
        "{{index .Config.Labels \"com.docker.compose.service\"}}\t{{.HostConfig.RestartPolicy.Name}}' "
        "'%s' 2>/dev/null", c->name));
    if (!line)
        return;
    char *dir = strsep(&line, "\t"), *svc = strsep(&line, "\t"), *restart = line;
    if (dir && dir[0])
        field(t, "compose", arena_printf(a, "%s/docker-compose.yml  (service %s)",
                                         home_short(a, dir), svc && svc[0] ? svc : "?"));
    if (restart && restart[0] && strcmp(restart, "no") != 0)
        field(t, "restart", strcmp(restart, "always") == 0
                                ? "always (comes back when docker restarts, even after a stop)"
                                : restart);
}

static void container_commands(Text *t, const Container *c, const char *stop)
{
    Arena *a = t->a;
    add(t, "\n");
    command(t, "status", arena_printf(a, "docker ps -a --filter name=^%s$", c->name));
    command(t, "logs", arena_printf(a, "docker logs --tail 50 -f %s", c->name));
    command(t, "inside", arena_printf(a, "docker top %s", c->name));
    command(t, "stop", stop);
}

const char *info_container(Arena *a, const World *w, const Container *c, const char *stop)
{
    (void)w;
    Text t = { .a = a };
    title(&t, c->name, "container");
    container_facts(&t, c);
    container_commands(&t, c, stop);
    return t.buf;
}

const char *info_database(Arena *a, const World *w, const Database *d, const char *stop)
{
    Text t = { .a = a };
    title(&t, d->engine, d->guessed ? "database? (only guessed from its port)" : "database");
    if (d->port)
        field(&t, "port", arena_printf(a, ":%d%s", d->port,
                                       d->port_internal ? " (inside docker only, not published)" : ""));
    if (d->container >= 0) {
        const Container *c = &w->containers.items[d->container];
        field(&t, "runs in", arena_printf(a, "docker container %s", c->name));
        container_facts(&t, c);
        container_commands(&t, c, stop);
        return t.buf;
    }
    const Service *s = d->service;
    if (s && s->unit) {
        field(&t, "runs as", arena_printf(a, "%s service %s",
                                          s->scope == SCOPE_USER ? "user" : "system", s->name));
        unit_facts(&t, s);
    }
    process_facts(&t, w, d->pid, s ? &s->members : NULL);
    if (s && s->unit) {
        const char *user = s->scope == SCOPE_USER ? "--user " : "";
        add(&t, "\n");
        command(&t, "status", arena_printf(a, "systemctl %sstatus %s", user, s->name));
        command(&t, "logs", arena_printf(a, "journalctl %s-u %s -e", user, s->name));
        command(&t, "stop", stop);
    } else {
        process_commands(&t, d->pid, stop);
    }
    return t.buf;
}

const char *info_port(Arena *a, const World *w, const PortSummary *p, const char *stop)
{
    Text t = { .a = a };
    title(&t, arena_printf(a, ":%d", p->port), "port");

    const char *addrs = "";                         // every address it's bound to
    for (size_t i = 0; i < w->ports.len; i++)
        if (w->ports.items[i].port == p->port && !strstr(addrs, w->ports.items[i].addr))
            addrs = arena_printf(a, "%s%s%s", addrs, addrs[0] ? ", " : "", w->ports.items[i].addr);
    field(&t, "open to", arena_printf(a, "%s  (%s)", p->reach == REACH_NETWORK
                                          ? paint(a, STYLE_WARN, "network: other machines can connect")
                                          : port_reach_label(p), addrs));

    if (p->pid >= 0) {
        const Members *m = NULL;
        const char *owner = NULL;
        for (size_t i = 0; i < w->devservers.len && !m; i++)    // before apps: see port_action
            if (members_has(&w->devservers.items[i].members, p->pid)) {
                m = &w->devservers.items[i].members;
                owner = arena_printf(a, "%s in %s (dev server)", w->devservers.items[i].command,
                                     w->devservers.items[i].project);
            }
        for (size_t i = 0; i < w->apps.len && !m; i++)
            if (members_has(&w->apps.items[i].members, p->pid)) {
                m = &w->apps.items[i].members;
                owner = arena_printf(a, "%s (app)", w->apps.items[i].name);
            }
        for (size_t i = 0; i < w->services.len && !m; i++)
            if (members_has(&w->services.items[i].members, p->pid)) {
                m = &w->services.items[i].members;
                owner = arena_printf(a, "%s (service)", w->services.items[i].name);
            }
        const Proc *pr = find_proc(w, p->pid);
        field(&t, "owner", owner ? owner : pr ? pr->comm : NULL);
        process_facts(&t, w, p->pid, m);
        add(&t, "\n");
        command(&t, "check", arena_printf(a, "ss -ltnp 'sport = :%d'", p->port));
        command(&t, "tree", arena_printf(a, "pstree -ap %d", p->pid));
        command(&t, "stop", stop);
        return t.buf;
    }
    int c = docker_find_port(&w->containers, p->port);
    if (c >= 0) {
        field(&t, "owner", arena_printf(a, "docker container %s", w->containers.items[c].name));
        container_facts(&t, &w->containers.items[c]);
    }
    add(&t, "\n");
    command(&t, "check", arena_printf(a, "ss -ltnp 'sport = :%d'", p->port));
    if (c >= 0)
        command(&t, "logs", arena_printf(a, "docker logs --tail 50 -f %s", w->containers.items[c].name));
    command(&t, "stop", stop);
    return t.buf;
}
