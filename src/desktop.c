#include "desktop.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Launchers/interpreters: "python3 app.py" says nothing about which app it is. */
static const char *const wrappers[] = {
    "sh", "bash", "zsh", "dash", "python", "python3", "perl", "ruby", "node",
    "java", "flatpak", "snap", "gtk-launch", "xdg-open", "electron", NULL,
};

static bool is_wrapper(const char *name)
{
    for (size_t i = 0; wrappers[i]; i++)
        if (strcmp(name, wrappers[i]) == 0)
            return true;
    return false;
}

/* First real program of an Exec= line. Skips "env" and "VAR=value", strips quotes. */
static bool exec_program(const char *exec, char *out, size_t size)
{
    const char *s = exec;
    for (;;) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            return false;

        size_t len;
        if (*s == '"') {
            s++;
            len = strcspn(s, "\"");
        } else {
            len = strcspn(s, " \t");
        }
        snprintf(out, size, "%.*s", (int)len, s);
        s += len;
        if (*s == '"')
            s++;

        bool is_env = strcmp(out, "env") == 0;
        bool is_var = strchr(out, '=') && !strchr(out, '/');
        if (!is_env && !is_var)
            return true;
    }
}

static void add_key(Arena *a, DesktopIndex *d, const char *key,
                    const char *name, bool terminal)
{
    if (d->len == d->cap)
        d->items = arena_grow(a, d->items, d->len, &d->cap, sizeof *d->items);
    d->items[d->len++] = (DesktopEntry){
        .key = arena_strdup(a, key),
        .name = arena_strdup(a, name),
        .terminal = terminal,
    };
}

/* If path is a symlink to a differently named binary, add that name too. */
static void add_real_key(Arena *a, DesktopIndex *d, const char *path,
                         const char *base, const char *name, bool terminal)
{
    char real[PATH_MAX];
    if (access(path, X_OK) != 0 || !realpath(path, real))
        return;
    const char *real_base = path_base(real);
    if (strcmp(real_base, base) != 0 && !is_wrapper(real_base))
        add_key(a, d, real_base, name, terminal);
}

/*
 * Keys for one app: the Exec= program name, plus the real binary behind it.
 * "mongodb-compass" -> symlink -> ".../MongoDB Compass". A bare name is looked
 * up in every $PATH dir, since a wrapper script earlier in $PATH can hide the
 * symlink.
 */
static void add_app(Arena *a, DesktopIndex *d, const char *exec,
                    const char *name, bool terminal)
{
    char prog[PATH_MAX], full[PATH_MAX];
    if (!exec_program(exec, prog, sizeof prog))
        return;

    const char *base = path_base(prog);
    if (is_wrapper(base))
        return;
    add_key(a, d, base, name, terminal);

    if (strchr(prog, '/')) {
        add_real_key(a, d, prog, base, name, terminal);
        return;
    }

    const char *path = getenv("PATH");
    if (!path)
        path = "/usr/local/bin:/usr/bin:/bin";
    while (*path) {
        size_t len = strcspn(path, ":");
        int n = snprintf(full, sizeof full, "%.*s/%s", (int)len, path, prog);
        if (len > 0 && n > 0 && (size_t)n < sizeof full)   // skip if cut short
            add_real_key(a, d, full, base, name, terminal);
        path += len;
        if (*path == ':')
            path++;
    }
}

/* Parse the [Desktop Entry] section of one file. */
static void load_file(Arena *a, DesktopIndex *d, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[2048], name[256] = "", exec[2048] = "";
    bool in_entry = false, is_app = false, hidden = false, terminal = false;
    // NoDisplay: not shown in menus -> helpers (portals, Xwayland, URL handlers)

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[') {
            if (in_entry)
                break;                      // past [Desktop Entry]: done
            in_entry = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry)
            continue;

        if (strncmp(line, "Name=", 5) == 0)
            snprintf(name, sizeof name, "%.255s", line + 5);    // cut long names
        else if (strncmp(line, "Exec=", 5) == 0)
            snprintf(exec, sizeof exec, "%s", line + 5);
        else if (strcmp(line, "Type=Application") == 0)
            is_app = true;
        else if (strcmp(line, "Hidden=true") == 0 || strcmp(line, "NoDisplay=true") == 0)
            hidden = true;
        else if (strncmp(line, "Categories=", 11) == 0)
            terminal = strstr(line, "TerminalEmulator") != NULL;
    }
    fclose(f);

    if (is_app && !hidden && exec[0] && name[0])
        add_app(a, d, exec, name, terminal);
}

DesktopIndex desktop_load(Arena *a, const char **dirs)
{
    DesktopIndex d = { 0 };

    for (size_t i = 0; dirs[i]; i++) {
        DIR *dir = opendir(dirs[i]);
        if (!dir)
            continue;
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            size_t len = strlen(e->d_name);
            if (len < 8 || strcmp(e->d_name + len - 8, ".desktop") != 0)
                continue;
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s", dirs[i], e->d_name);
            load_file(a, &d, path);
        }
        closedir(dir);
    }
    return d;
}

const char **desktop_default_dirs(Arena *a)
{
    const char *home = getenv("HOME");
    const char *data_home = getenv("XDG_DATA_HOME");
    const char *data_dirs = getenv("XDG_DATA_DIRS");
    if (!data_dirs || !*data_dirs)
        data_dirs = "/usr/local/share:/usr/share";

    size_t max = 2;                                 // data_home + NULL
    for (const char *s = data_dirs; *s; s++)
        max += *s == ':';
    max += 1;
    const char **dirs = arena_alloc(a, max * sizeof *dirs);

    size_t n = 0;
    char buf[PATH_MAX];
    if (data_home && *data_home)
        snprintf(buf, sizeof buf, "%s/applications", data_home);
    else
        snprintf(buf, sizeof buf, "%s/.local/share/applications", home ? home : "");
    dirs[n++] = arena_strdup(a, buf);               // user's own first: overrides

    const char *s = data_dirs;
    while (*s) {
        size_t len = strcspn(s, ":");
        if (len > 0) {
            snprintf(buf, sizeof buf, "%.*s/applications", (int)len, s);
            dirs[n++] = arena_strdup(a, buf);
        }
        s += len;
        if (*s == ':')
            s++;
    }
    dirs[n] = NULL;
    return dirs;
}

void desktop_add(Arena *a, DesktopIndex *d, const char *name)
{
    add_key(a, d, name, name, false);
}

/* comm is the process name cut to 15 chars, so only compare that much. */
static bool comm_eq(const char *comm, const char *key)
{
    if (strlen(comm) < 15)
        return strcmp(comm, key) == 0;
    return strncmp(comm, key, 15) == 0;
}

const DesktopEntry *desktop_match(const DesktopIndex *d, const Proc *p)
{
    const char *exe = p->exe[0] ? path_base(p->exe) : NULL;
    for (size_t i = 0; i < d->len; i++) {
        const char *key = d->items[i].key;
        if ((exe && strcmp(exe, key) == 0) || comm_eq(p->comm, key))
            return &d->items[i];
    }
    return NULL;
}
