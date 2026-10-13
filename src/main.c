#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "actions.h"
#include "report.h"
#include "world.h"

static void usage(FILE *out)
{
    fputs("usage: whatsup [-i] [--json] [--no-color]\n"
          "  (no option)  print apps, services, dev servers, databases, containers, ports\n"
          "  -i           interactive: pick things and stop them (esc to quit)\n"
          "  --json       everything as JSON, for scripts (includes hidden system ports)\n"
          "  --no-color   plain output (also: NO_COLOR=1, or piping to a file)\n"
          "  -h           this help\n"
          "config: ~/.config/whatsup/config (created on first run)\n", out);
}

/* Pick, act, rescan, repeat until Esc. A fresh arena per round. */
static int interactive_loop(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "whatsup: interactive mode needs a terminal\n");
        return 2;
    }
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
    bool interactive = false, color = true, json = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            color = false;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "whatsup: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    style_init(color && !json);
    if (interactive && json) {
        fprintf(stderr, "whatsup: -i and --json don't go together\n");
        return 2;
    }
    if (interactive)
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
