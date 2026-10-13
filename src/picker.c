#include "picker.h"
#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ANSI escape codes: the terminal reads these as commands, not text. */
#define CSI         "\x1b["
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"
#define CLEAR_DOWN  CSI "J"         // erase from cursor to end of screen
#define REVERSE     CSI "7m"
#define BOLD        CSI "1m"
#define DIM         CSI "2m"
#define RESET       CSI "0m"

#define MAX_VISIBLE 12
#define MAX_QUERY   64
#define MAX_COL     28          // longer cells end in "…"

enum {
    KEY_NONE = 0,
    KEY_UP = 1000, KEY_DOWN, KEY_TAB, KEY_BTAB, KEY_ENTER, KEY_ESC,
    KEY_BACKSPACE, KEY_CLEAR,
};

/* ---- raw mode: get every keypress immediately, without echo ---- */

static struct termios saved;
static bool raw_on;

static void raw_disable(void)
{
    if (!raw_on)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    fputs(SHOW_CURSOR, stdout);
    fflush(stdout);
    raw_on = false;
}

static void on_signal(int sig)
{
    raw_disable();                  // never leave the terminal broken
    signal(sig, SIG_DFL);
    raise(sig);
}

static bool raw_enable(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return false;
    if (tcgetattr(STDIN_FILENO, &saved) < 0)
        return false;

    struct termios raw = saved;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);  // no line buffering/echo; Ctrl-C is a key
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);          // Ctrl-S/Q as keys; Enter stays '\r'
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return false;

    static bool hooked;
    if (!hooked) {
        atexit(raw_disable);
        signal(SIGTERM, on_signal);
        signal(SIGHUP, on_signal);
        hooked = true;
    }
    raw_on = true;
    return true;
}

static bool byte_ready(int ms)
{
    struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
    return poll(&p, 1, ms) > 0;
}

/*
 * Arrow keys arrive as several bytes: ESC [ A. A lone ESC (no bytes right
 * after it) is the Esc key itself.
 */
static int read_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return KEY_ESC;

    if (c == 27) {
        unsigned char seq[2];
        if (!byte_ready(30) || read(STDIN_FILENO, &seq[0], 1) != 1)
            return KEY_ESC;
        if (seq[0] != '[' && seq[0] != 'O')
            return KEY_NONE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return KEY_NONE;
        if (isdigit(seq[1])) {                      // e.g. PgUp = ESC [ 5 ~ : skip
            while (byte_ready(30) && read(STDIN_FILENO, &c, 1) == 1 && c != '~')
                ;
            return KEY_NONE;
        }
        switch (seq[1]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'Z': return KEY_BTAB;                  // Shift-Tab
        default:  return KEY_NONE;
        }
    }
    switch (c) {
    case '\r': case '\n': return KEY_ENTER;
    case 127: case 8:     return KEY_BACKSPACE;
    case '\t':            return KEY_TAB;
    case 3:               return KEY_ESC;           // Ctrl-C
    case 16:              return KEY_UP;            // Ctrl-P
    case 14:              return KEY_DOWN;          // Ctrl-N
    case 21:              return KEY_CLEAR;         // Ctrl-U
    }
    return c >= 32 && c < 127 ? c : KEY_NONE;
}

/* ---- drawing ---- */

static int term_width(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
}

static size_t text_width(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

/* Print at most max characters of s (lines must never wrap, or redraw breaks). */
static void put_cut(const char *s, size_t max)
{
    size_t n = 0;
    for (; *s; s++) {
        bool starts_char = ((unsigned char)*s & 0xC0) != 0x80;
        if (starts_char && n++ == max)
            break;
        putchar(*s);
    }
}

/* Append cell to row, padded or cut to exactly width characters. */
static size_t put_cell(char *row, size_t len, size_t size, const char *cell, size_t width)
{
    size_t chars = 0, w = text_width(cell);
    for (const char *s = cell; *s && len + 4 < size; s++) {
        bool starts_char = ((unsigned char)*s & 0xC0) != 0x80;
        if (starts_char && w > width && chars == width - 1)
            break;                                  // leave room for the ellipsis
        if (starts_char)
            chars++;
        row[len++] = *s;
    }
    if (w > width && len + 4 < size) {
        memcpy(row + len, "…", 3);                  // 3 bytes, 1 character
        len += 3;
        chars++;
    }
    while (chars++ < width && len + 1 < size)
        row[len++] = ' ';
    row[len] = '\0';
    return len;
}

typedef struct {
    const PickItem *items;
    size_t n;
    const char *const *tabs;
    int ntabs;
    size_t width[PICK_COLS];        // column widths over all items

    int tab;
    char query[MAX_QUERY];
    size_t *shown;                  // indexes of items passing the filter
    size_t nshown, sel, scroll;
    int drawn;                      // lines on screen from the last draw
} Picker;

static bool contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return true;
    }
    return n == 0;
}

static void refilter(Picker *p)
{
    p->nshown = 0;
    for (size_t i = 0; i < p->n; i++) {
        const PickItem *it = &p->items[i];
        if (p->tab != 0 && it->tab != p->tab)
            continue;
        bool hit = false;
        for (int c = 0; c < PICK_COLS && !hit; c++)
            hit = it->cols[c] && contains_ci(it->cols[c], p->query);
        if (hit)
            p->shown[p->nshown++] = i;
    }
    p->sel = 0;
    p->scroll = 0;
}

static void draw(Picker *p)
{
    size_t max = (size_t)term_width() - 1;

    if (p->drawn > 1)
        printf(CSI "%dA", p->drawn - 1);            // back to our first line
    printf("\r" CLEAR_DOWN);
    int lines = 0;

    // tabs:  All  [Apps]  Services ...
    for (int t = 0; t < p->ntabs; t++)
        printf(t == p->tab ? REVERSE " %s " RESET " " : DIM " %s " RESET " ", p->tabs[t]);
    printf("\n" BOLD "> " RESET "%s\n", p->query);
    lines += 2;

    size_t visible = p->nshown < MAX_VISIBLE ? p->nshown : MAX_VISIBLE;
    if (p->sel < p->scroll)
        p->scroll = p->sel;
    if (p->sel >= p->scroll + visible)
        p->scroll = p->sel - visible + 1;

    char row[512];
    for (size_t v = 0; v < visible; v++) {
        size_t idx = p->shown[p->scroll + v];
        const PickItem *it = &p->items[idx];
        size_t len = 0;
        row[0] = '\0';
        for (int c = 0; c < PICK_COLS; c++) {
            const char *cell = it->cols[c] ? it->cols[c] : "";
            bool last = c == PICK_COLS - 1;
            len = put_cell(row, len, sizeof row, cell, last ? text_width(cell) : p->width[c]);
            if (!last)
                len = put_cell(row, len, sizeof row, "", 2);        // gap
        }
        bool selected = p->scroll + v == p->sel;
        fputs(selected ? BOLD "▶ " : "  ", stdout);
        put_cut(row, max - 2);
        fputs(RESET "\n", stdout);
        lines++;
    }
    if (p->nshown == 0) {
        fputs(DIM "  (no matches)" RESET "\n", stdout);
        lines++;
    }
    printf(DIM "  %zu/%zu  ↑↓ move  tab section  enter select  esc quit" RESET, p->nshown, p->n);
    lines++;

    fflush(stdout);
    p->drawn = lines;
}

static void erase(const Picker *p)
{
    if (p->drawn > 1)
        printf(CSI "%dA", p->drawn - 1);
    printf("\r" CLEAR_DOWN);
    fflush(stdout);
}

int picker_run(const PickItem *items, size_t n, const char *const *tabs, int ntabs)
{
    if (!raw_enable())
        return -1;

    size_t *shown = malloc((n ? n : 1) * sizeof *shown);
    if (!shown) {
        raw_disable();
        return -1;
    }
    Picker p = { .items = items, .n = n, .tabs = tabs, .ntabs = ntabs, .shown = shown };
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < PICK_COLS; c++) {
            size_t w = items[i].cols[c] ? text_width(items[i].cols[c]) : 0;
            if (w > p.width[c])
                p.width[c] = w < MAX_COL ? w : MAX_COL;
        }

    fputs(HIDE_CURSOR, stdout);
    refilter(&p);
    int result = -1;
    for (;;) {
        draw(&p);
        int key = read_key();
        size_t qlen = strlen(p.query);

        if (key == KEY_ESC)
            break;
        if (key == KEY_ENTER) {
            if (p.nshown > 0) {
                result = (int)p.shown[p.sel];
                break;
            }
        } else if (key == KEY_UP) {
            if (p.sel > 0)
                p.sel--;
        } else if (key == KEY_DOWN) {
            if (p.sel + 1 < p.nshown)
                p.sel++;
        } else if (key == KEY_TAB || key == KEY_BTAB) {
            p.tab = (p.tab + (key == KEY_TAB ? 1 : ntabs - 1)) % ntabs;
            refilter(&p);
        } else if (key == KEY_BACKSPACE) {
            if (qlen > 0) {
                p.query[qlen - 1] = '\0';
                refilter(&p);
            }
        } else if (key == KEY_CLEAR) {
            p.query[0] = '\0';
            refilter(&p);
        } else if (key > 0 && key < 127 && qlen < MAX_QUERY - 1) {
            p.query[qlen] = (char)key;
            p.query[qlen + 1] = '\0';
            refilter(&p);
        }
    }

    erase(&p);
    raw_disable();
    free(shown);
    return result;
}

bool picker_confirm(const char *question)
{
    printf("%s [y/N] ", question);
    fflush(stdout);
    if (!raw_enable()) {                            // not a terminal: read a line
        char line[16];
        return fgets(line, sizeof line, stdin) && (line[0] == 'y' || line[0] == 'Y');
    }
    int key = read_key();
    raw_disable();
    bool yes = key == 'y' || key == 'Y';
    printf("%s\n", yes ? "y" : "n");
    return yes;
}
