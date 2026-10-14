#ifndef PICKER_H
#define PICKER_H

#include <stdbool.h>
#include <stddef.h>

#define PICK_COLS 5

/*
 * One selectable row. tab is 1..ntabs-1 (tab 0 is "All").
 * kind ("app", "service") fills column 1 in the All tab, where rows of every
 * kind mix; in its own tab column 1 is cols[1] (a service's scope), if shown.
 */
typedef struct {
    int tab;
    const char *kind;
    const char *cols[PICK_COLS];
    const char *style[PICK_COLS];   // STYLE_* color per cell, or NULL
} PickItem;

/* Text describing item i, shown when the user presses i or Enter. */
typedef const char *(*PickDescribe)(void *ctx, size_t i);

/*
 * Picker drawn under the prompt, with vim-style keys:
 *   j/k move, h/l switch section, g/G top/bottom, d picks, q quits,
 *   i / Enter: describe the item (then d picks it, Esc goes back),
 *   / search (type to filter; Enter keeps the filter, Esc clears it).
 * Arrows, Tab/Shift-Tab and Ctrl-n/p work too. Erases itself when done.
 * heads holds one row of column headers per tab, shown above the items;
 * a NULL header hides that column in that tab.
 * *tab is the section to start on, and is updated to where the user left it.
 * Returns the chosen item's index, or -1 if cancelled / not a terminal.
 */
int picker_run(const PickItem *items, size_t n, const char *const *tabs,
               const char *const (*heads)[PICK_COLS], int ntabs, int *tab,
               PickDescribe describe, void *ctx);

/* Ask a y/N question with a single keypress. */
bool picker_confirm(const char *question);

#endif
