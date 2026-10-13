#ifndef PICKER_H
#define PICKER_H

#include <stdbool.h>
#include <stddef.h>

#define PICK_COLS 5

/* One selectable row. tab is 1..ntabs-1 (tab 0 is "All"). */
typedef struct {
    int tab;
    const char *cols[PICK_COLS];
} PickItem;

/*
 * fzf-style picker drawn under the prompt: type to filter, Tab switches
 * section, arrows move, Enter picks, Esc quits. Erases itself when done.
 * Returns the chosen item's index, or -1 if cancelled / not a terminal.
 */
int picker_run(const PickItem *items, size_t n, const char *const *tabs, int ntabs);

/* Ask a y/N question with a single keypress. */
bool picker_confirm(const char *question);

#endif
