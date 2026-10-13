#include "config.h"
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char config_default_text[] =
    "# whatsup config. Edit freely; delete this file to get the defaults back.\n"
    "# One entry per line. * matches anything: \"systemd-*\" hides systemd-udevd,\n"
    "# systemd-logind... Names are systemd unit names (without .service) or\n"
    "# process names.\n"
    "\n"
    "# System services the machine needs to work: hidden from SERVICES.\n"
    "[hide-system]\n"
    "systemd-*\n"
    "dbus*\n"
    "NetworkManager*\n"
    "wpa_supplicant\n"
    "iwd\n"
    "polkit\n"
    "udisks2\n"
    "upower\n"
    "rtkit-daemon\n"
    "bluetooth\n"
    "accounts-daemon\n"
    "avahi-daemon\n"
    "power-profiles-daemon\n"
    "tlp\n"
    "thermald\n"
    "acpid\n"
    "ModemManager\n"
    "getty@*\n"
    "serial-getty@*\n"
    "user@*\n"
    "user-runtime-dir@*\n"
    "# display managers\n"
    "ly@*\n"
    "gdm\n"
    "sddm\n"
    "lightdm\n"
    "greetd\n"
    "\n"
    "# Your own desktop plumbing: hidden from SERVICES.\n"
    "[hide-user]\n"
    "dbus*\n"
    "at-spi*\n"
    "xdg-desktop-portal*\n"
    "xdg-document-portal\n"
    "xdg-permission-store\n"
    "dconf*\n"
    "gvfs*\n"
    "user@*\n"
    "\n"
    "# Programs without a .desktop file to list under APPS (process names).\n"
    "[apps]\n";

static void list_add(Arena *a, StrList *l, const char *s)
{
    if (l->len == l->cap)
        l->items = arena_grow(a, l->items, l->len, &l->cap, sizeof *l->items);
    l->items[l->len++] = arena_strdup(a, s);
}

/* Cut leading/trailing spaces in place. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = '\0';
    return s;
}

Config config_parse(Arena *a, const char *text, const char *origin)
{
    Config cfg = { 0 };
    StrList *section = NULL;
    char line[512];
    int lineno = 0;

    for (const char *s = text; *s;) {
        size_t len = strcspn(s, "\n");
        snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), s);
        s += len;
        if (*s == '\n')
            s++;
        lineno++;

        line[strcspn(line, "#")] = '\0';            // comments
        char *item = trim(line);
        if (!*item)
            continue;

        if (item[0] == '[') {
            if (strcmp(item, "[hide-system]") == 0)
                section = &cfg.hide_system;
            else if (strcmp(item, "[hide-user]") == 0)
                section = &cfg.hide_user;
            else if (strcmp(item, "[apps]") == 0)
                section = &cfg.apps;
            else {
                fprintf(stderr, "whatsup: %s:%d: unknown section %s (ignored)\n",
                        origin, lineno, item);
                section = NULL;
            }
        } else if (section) {
            list_add(a, section, item);
        } else {
            fprintf(stderr, "whatsup: %s:%d: '%s' is not in a section (ignored)\n",
                    origin, lineno, item);
        }
    }
    return cfg;
}

/* mkdir -p for the directory part of path. */
static void make_parents(const char *path)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(dir, 0755);                           // fails harmlessly if it exists
        *p = '/';
    }
}

static char *read_all(Arena *a, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = arena_alloc(a, cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 == cap)
            buf = arena_grow(a, buf, len, &cap, 1);
    }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

Config config_load(Arena *a)
{
    char path[PATH_MAX];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(path, sizeof path, "%s/whatsup/config", xdg);
    else
        snprintf(path, sizeof path, "%s/.config/whatsup/config", home ? home : ".");

    char *text = read_all(a, path);
    if (!text && errno == ENOENT) {                 // first run: write the defaults
        make_parents(path);
        FILE *f = fopen(path, "wx");                // x: never overwrite an existing file
        if (f) {
            fputs(config_default_text, f);
            fclose(f);
            text = read_all(a, path);
        }
    }
    if (!text) {
        fprintf(stderr, "whatsup: can't read or create %s, using built-in defaults\n", path);
        return config_parse(a, config_default_text, "defaults");
    }

    Config cfg = config_parse(a, text, path);
    cfg.path = arena_strdup(a, path);
    return cfg;
}

bool config_match(const StrList *list, const char *name)
{
    for (size_t i = 0; i < list->len; i++)
        if (fnmatch(list->items[i], name, 0) == 0)
            return true;
    return false;
}
