#include "procfs.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "1234" -> 1234, anything else ("self", "net", ...) -> -1 */
static int parse_pid(const char *name)
{
    if (!isdigit((unsigned char)name[0]))
        return -1;
    char *end;
    long v = strtol(name, &end, 10);
    if (*end != '\0' || v <= 0 || v > INT_MAX)
        return -1;
    return (int)v;
}

/*
 * Fill pid, ppid, comm, start from <root>/<pid>/stat.
 * Format: "3037 (firefox) S 2571 ...". comm can contain spaces and ')',
 * so we take everything up to the LAST ')' as the name.
 * Returns 0 on success, -1 if the process is gone or the file is odd.
 */
static int parse_stat(const char *root, int pid, Proc *p)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%d/stat", root, pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;                      // process exited: skip it
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    char *open = strchr(buf, '(');
    char *close = strrchr(buf, ')');
    if (!open || !close || close < open)
        return -1;

    size_t len = (size_t)(close - open - 1);
    if (len >= sizeof p->comm)
        len = sizeof p->comm - 1;
    memcpy(p->comm, open + 1, len);
    p->comm[len] = '\0';

    // fields after ')': 3=state 4=ppid ... 22=starttime
    int got = sscanf(close + 2,
                     "%*c %d"                       // state, ppid
                     " %*s %*s %*s %*s %*s %*s"     // 5-10
                     " %*s %*s %*s %*s %*s %*s"     // 11-16
                     " %*s %*s %*s %*s %*s"         // 17-21
                     " %llu",                       // starttime
                     &p->ppid, &p->start);
    if (got != 2)
        return -1;

    p->pid = pid;
    return 0;
}

/*
 * Fill uid and rss_kb from <root>/<pid>/status (one "Key:\tvalue" per line).
 * Kernel threads have no VmRSS line, so rss_kb stays 0.
 */
static int parse_status(const char *root, int pid, Proc *p)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%d/status", root, pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Uid:", 4) == 0)
            sscanf(line + 4, "%d", &p->uid);        // first value = real uid
        else if (strncmp(line, "VmRSS:", 6) == 0)
            sscanf(line + 6, "%ld", &p->rss_kb);    // already in kB
    }
    fclose(f);
    return 0;
}

/*
 * Read <root>/<pid>/cmdline: args separated by '\0', e.g. "python3\0bar.py\0".
 * Turned into "python3 bar.py". Kernel threads have an empty cmdline -> "".
 * Long command lines are cut at 4 KB, plenty for display.
 */
static char *read_cmdline(Arena *a, const char *root, int pid)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%d/cmdline", root, pid);

    char buf[4096];
    size_t n = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }

    while (n > 0 && buf[n - 1] == '\0')        // drop trailing separators
        n--;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\0')
            buf[i] = ' ';
    buf[n] = '\0';

    return arena_strdup(a, buf);
}

/* Append an empty slot, doubling capacity when full. */
static Proc *push(Arena *a, ProcList *list, size_t *cap)
{
    if (list->len == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 256;
        Proc *items = arena_alloc(a, new_cap * sizeof *items);
        if (list->len)
            memcpy(items, list->items, list->len * sizeof *items);
        list->items = items;    // old array stays in the arena until exit
        *cap = new_cap;
    }
    return &list->items[list->len++];
}

ProcList procfs_scan(Arena *a, const char *root)
{
    ProcList list = { 0 };
    size_t cap = 0;

    DIR *dir = opendir(root);
    if (!dir)
        return list;

    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        int pid = parse_pid(e->d_name);
        if (pid < 0)
            continue;

        Proc p = { 0 };
        if (parse_stat(root, pid, &p) < 0 || parse_status(root, pid, &p) < 0)
            continue;                   // vanished mid-scan: skip
        p.cmdline = read_cmdline(a, root, pid);

        *push(a, &list, &cap) = p;
    }

    closedir(dir);
    return list;
}
