#ifndef PICKER_H
#define PICKER_H

#include <stdbool.h>
#include <stddef.h>

#define PICK_COLS 5

/* One selectable row. tab is 1..ntabs-1 (tab 0 is "All"). */
typedef struct {
    int tab;
    const char *cols[PICK_COLS];
    const char *style[PICK_COLS];   // STYLE_* color per cell, or NULL
} PickItem;

/*
 * Picker drawn under the prompt, with vim-style keys:
 *   j/k move, h/l switch section, g/G top/bottom, d picks, q quits,
 *   / search (type to filter; Enter keeps the filter, Esc clears it).
 * Arrows, Tab/Shift-Tab and Ctrl-n/p work too. Erases itself when done.
 * *tab is the section to start on, and is updated to where the user left it.
 * Returns the chosen item's index, or -1 if cancelled / not a terminal.
 */
int picker_run(const PickItem *items, size_t n, const char *const *tabs, int ntabs, int *tab);

/* Ask a y/N question with a single keypress. */
bool picker_confirm(const char *question);

#endif
