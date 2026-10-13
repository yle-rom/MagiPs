#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "actions.h"
#include "report.h"
#include "world.h"

static void usage(FILE *out)
{
    fputs("usage: magips [-s] [--json] [--no-color]\n"
          "  (no option)  interactive: pick things and stop them\n"
          "               j/k move  h/l section  / search  enter select  q quit\n"
          "  -s           print apps, services, dev servers, databases, containers, ports\n"
          "               (also used automatically when output isn't a terminal)\n"
          "  --json       everything as JSON, for scripts (includes hidden system ports)\n"
          "  --no-color   plain output (also: NO_COLOR=1)\n"
          "  -h           this help\n"
          "config: ~/.config/magips/config (created on first run)\n", out);
}

/* Pick, act, rescan, repeat until quit. A fresh arena per round. */
static int interactive_loop(void)
{
    int tab = 0;
    for (;;) {
        Arena *a = arena_new(1 << 16);
        World w;
        world_scan(a, &w);
        PickResult r = actions_pick(a, &w, &tab);
        arena_free(a);
        if (r == PICK_QUIT)
            return 0;
    }
}

int main(int argc, char **argv)
{
    bool print = false, color = true, json = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--static") == 0) {
            print = true;
        } else if (strcmp(argv[i], "-i") == 0) {
            print = false;                          // old flag: interactive is the default now
        } else if (strcmp(argv[i], "--no-color") == 0) {
            color = false;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "magips: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    style_init(color && !json);

    // `magips | grep x` or `magips > file`: there's no one to pick, so print
    bool terminal = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (!print && !json && terminal)
        return interactive_loop();

    Arena *a = arena_new(1 << 16);     // 64 KB blocks
    World w;
    world_scan(a, &w);
    if (json)
        report_json(a, &w);
    else
        report_tables(a, &w);
    arena_free(a);
    return 0;
}
