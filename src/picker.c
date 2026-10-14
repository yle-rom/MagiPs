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
#include "util.h"

/* ANSI escape codes: the terminal reads these as commands, not text. */
#define CSI         "\x1b["
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"
#define CLEAR_DOWN  CSI "J"         // erase from cursor to end of screen
#define REVERSE     CSI "7m"
#define BOLD        CSI "1m"
#define DIM         CSI "2m"
#define RESET       CSI "0m"
#define CELL_END    CSI "22;39m"    // end a cell's color but keep the line's background
#define SEL_BG      CSI "48;5;236m" // selected line: dark grey background (256-color)
#define TAB_ON      CSI "1;30;46m"  // selected section: black on cyan, like the titles
#define HEAD        CSI "2;36m"     // column headers: faded cyan, quieter than the section bar

#define MAX_VISIBLE 12
#define MAX_QUERY   64

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

/*
 * Print at most max characters of s (lines must never wrap, or redraw
 * breaks). Color codes are copied through but take no space. Returns the
 * number of characters printed.
 */
static size_t put_cut(const char *s, size_t max)
{
    size_t n = 0;
    while (*s) {
        if (*s == '\x1b' && s[1] == '[') {
            const char *end = s + 2;
            while (*end && !((*end >= 'A' && *end <= 'Z') || (*end >= 'a' && *end <= 'z')))
                end++;
            if (*end)
                end++;
            fwrite(s, 1, (size_t)(end - s), stdout);
            s = end;
            continue;
        }
        bool starts_char = ((unsigned char)*s & 0xC0) != 0x80;
        if (starts_char && n++ == max) {
            n--;
            break;
        }
        putchar(*s++);
    }
    return n;
}

/* Pointer to the n-th character (not byte) of s, or its end. */
static const char *char_at(const char *s, size_t n)
{
    size_t chars = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80 && chars++ == n)
            return s;
    return s;
}

/* Append n bytes of s to row; row always stays a terminated string. */
static size_t put_bytes(char *row, size_t len, size_t size, const char *s, size_t n)
{
    if (len + n + 1 > size)
        n = size - len - 1;
    memcpy(row + len, s, n);
    row[len + n] = '\0';
    return len + n;
}

/*
 * Append cell to row, padded or cut to exactly width characters.
 * Cuts in the middle ("kotsistravel…app-demo-1"): the end of a name is
 * usually what tells similar ones apart.
 */
static size_t put_cell(char *row, size_t len, size_t size, const char *cell, size_t width)
{
    size_t w = text_width(cell), chars = w;
    if (w <= width) {
        len = put_bytes(row, len, size, cell, strlen(cell));
    } else if (width > 0) {
        size_t head = (width - 1) / 2, tail = width - 1 - head;
        const char *tail_start = char_at(cell, w - tail);
        len = put_bytes(row, len, size, cell, (size_t)(char_at(cell, head) - cell));
        len = put_bytes(row, len, size, "…", 3);   // 3 bytes, 1 character
        len = put_bytes(row, len, size, tail_start, strlen(tail_start));
        chars = width;
    } else {
        chars = 0;
    }
    for (; chars < width && len + 1 < size; chars++)
        row[len++] = ' ';
    row[len] = '\0';
    return len;
}

/*
 * Fit the columns into the terminal. The last column (image, owner...) just
 * gets cut at the screen edge; the others keep their natural width, except
 * the name column, which shrinks (down to 12) if even they don't fit.
 */
static void fit_widths(const size_t *natural, size_t *out, size_t term)
{
    const size_t prefix = 2, gap = 2, min_name = 12, min_last = 8;
    size_t total = prefix + min_last;
    for (int c = 0; c < PICK_COLS; c++) {
        out[c] = natural[c];
        if (c < PICK_COLS - 1 && natural[c])        // hidden columns take no gap either
            total += natural[c] + gap;
    }
    if (total > term && out[0] > min_name) {
        size_t over = total - term;
        out[0] = out[0] > over + min_name ? out[0] - over : min_name;
    }
}

typedef struct {
    const PickItem *items;
    size_t n;
    const char *const *tabs;
    const char *const (*heads)[PICK_COLS];
    int ntabs;

    int tab;
    bool searching;                 // after '/': keys type into the filter
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
        bool hit = it->kind && contains_ci(it->kind, p->query);
        for (int c = 0; c < PICK_COLS && !hit; c++)
            hit = it->cols[c] && contains_ci(it->cols[c], p->query);
        if (hit)
            p->shown[p->nshown++] = i;
    }
    p->sel = 0;
    p->scroll = 0;
}

/* What column c of an item shows in the current tab. */
static const char *cell_text(const Picker *p, const PickItem *it, int c)
{
    return p->tab == 0 && c == 1 ? it->kind : it->cols[c];
}

static void draw(Picker *p)
{
    size_t max = (size_t)term_width() - 1;
    bool color = style_on();

    if (p->drawn > 1)
        printf(CSI "%dA", p->drawn - 1);            // back to our first line
    printf("\r" CLEAR_DOWN);
    int lines = 0;

    // section bar:  All  [Apps]  Services ...
    for (int t = 0; t < p->ntabs; t++) {
        const char *on = color ? TAB_ON : REVERSE;
        printf(t == p->tab ? "%s %s " RESET " " : DIM " %s%s " RESET " ",
               t == p->tab ? on : "", p->tabs[t]);
    }
    // search line: "/ spo▌" while typing, the kept filter (dim) otherwise
    if (p->searching)
        printf("\n" BOLD "/ " RESET "%s▌\n", p->query);
    else if (p->query[0])
        printf("\n" DIM "/ %s" RESET "\n", p->query);
    else
        printf("\n\n");
    lines += 2;

    size_t visible = p->nshown < MAX_VISIBLE ? p->nshown : MAX_VISIBLE;
    if (p->sel < p->scroll)
        p->scroll = p->sel;
    if (p->sel >= p->scroll + visible)
        p->scroll = p->sel - visible + 1;

    // natural widths over this section's items (not just the filtered ones, so search
    // doesn't make columns jump); a NULL header hides that column in this section
    const char *const *heads = p->heads[p->tab];
    size_t natural[PICK_COLS], width[PICK_COLS];
    for (int c = 0; c < PICK_COLS; c++)
        natural[c] = heads[c] ? text_width(heads[c]) : 0;
    for (size_t i = 0; i < p->n; i++) {
        const PickItem *it = &p->items[i];
        if (p->tab != 0 && it->tab != p->tab)
            continue;
        for (int c = 0; c < PICK_COLS; c++) {
            const char *cell = cell_text(p, it, c);
            size_t w = heads[c] && cell ? text_width(cell) : 0;
            if (w > natural[c])
                natural[c] = w;
        }
    }
    fit_widths(natural, width, max);

    char row[2048];
    if (p->nshown) {                                // column headers for this section
        size_t len = 0;
        row[0] = '\0';
        for (int c = 0; c < PICK_COLS; c++) {
            const char *head = heads[c];
            if (!head)
                continue;
            bool last = c == PICK_COLS - 1;
            len = put_cell(row, len, sizeof row, head, last ? text_width(head) : width[c]);
            if (!last)
                len = put_cell(row, len, sizeof row, "", 2);        // gap
        }
        fputs(color ? "  " HEAD : "  " DIM, stdout);
        put_cut(row, max - 2);
        fputs(RESET "\n", stdout);
        lines++;
    }
    for (size_t v = 0; v < visible; v++) {
        const PickItem *it = &p->items[p->shown[p->scroll + v]];
        bool selected = p->scroll + v == p->sel;
        size_t len = 0;
        row[0] = '\0';
        for (int c = 0; c < PICK_COLS; c++) {
            if (!heads[c])
                continue;
            const char *cell = cell_text(p, it, c);
            bool last = c == PICK_COLS - 1;
            const char *style = !color ? NULL : p->tab == 0 && c == 1 ? STYLE_DIM : it->style[c];
            if (!cell)
                cell = "";
            if (style)
                len += (size_t)snprintf(row + len, sizeof row - len, CSI "%sm", style);
            len = put_cell(row, len, sizeof row, cell, last ? text_width(cell) : width[c]);
            if (style)
                len = put_bytes(row, len, sizeof row, CELL_END, strlen(CELL_END));
            if (!last)
                len = put_cell(row, len, sizeof row, "", 2);        // gap
        }

        if (selected)
            fputs(color ? SEL_BG BOLD CSI "36m▶ " CELL_END : REVERSE "▶ ", stdout);
        else
            fputs("  ", stdout);
        size_t used = 2 + put_cut(row, max - 2);
        if (selected)                               // background across the whole line
            printf("%*s", (int)(max > used ? max - used : 0), "");
        fputs(RESET "\n", stdout);
        lines++;
    }
    if (p->nshown == 0) {
        fputs(DIM "  (no matches)" RESET "\n", stdout);
        lines++;
    }
    printf(DIM "  %zu/%zu  %s" RESET, p->nshown, p->n,
           p->searching ? "type to filter  enter keep  esc clear"
                        : "j/k move  h/l section  / search  i info  d stop  q quit");
    lines++;

    fflush(stdout);
    p->drawn = lines;
}

/* Screen rows text takes at cols columns: its lines, plus wraps; escapes take no room. */
static int screen_rows(const char *s, size_t cols)
{
    int rows = 1;
    size_t col = 0;
    for (; *s; s++) {
        if (*s == '\x1b' && s[1] == '[') {
            while (s[1] && !((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')))
                s++;
            if (s[1])
                s++;
        } else if (*s == '\n') {
            rows++;
            col = 0;
        } else if (((unsigned char)*s & 0xC0) != 0x80 && ++col > cols) {
            rows++;
            col = 1;
        }
    }
    return rows;
}

/*
 * Show an item's description in place of the picker until d or Esc.
 * Erases it again and returns that key, so the picker comes back as it was.
 */
static int show_info(const char *text)
{
    const char *hint = DIM "  d stop  Esc back" RESET;
    fputs(text, stdout);
    fputs(hint, stdout);
    fflush(stdout);
    int rows = screen_rows(text, (size_t)term_width()) + screen_rows(hint, (size_t)term_width()) - 1;
    int key;
    while ((key = read_key()) != 'd' && key != KEY_ESC)
        ;                                           // other keys do nothing here
    if (rows > 1)
        printf(CSI "%dA", rows - 1);
    printf("\r" CLEAR_DOWN);
    fflush(stdout);
    return key;
}

static void erase(const Picker *p)
{
    if (p->drawn > 1)
        printf(CSI "%dA", p->drawn - 1);
    printf("\r" CLEAR_DOWN);
    fflush(stdout);
}

int picker_run(const PickItem *items, size_t n, const char *const *tabs,
               const char *const (*heads)[PICK_COLS], int ntabs, int *tab,
               PickDescribe describe, void *ctx)
{
    if (!raw_enable())
        return -1;

    size_t *shown = malloc((n ? n : 1) * sizeof *shown);
    if (!shown) {
        raw_disable();
        return -1;
    }
    Picker p = { .items = items, .n = n, .tabs = tabs, .heads = heads, .ntabs = ntabs,
                 .shown = shown, .tab = *tab >= 0 && *tab < ntabs ? *tab : 0 };

    fputs(HIDE_CURSOR, stdout);
    refilter(&p);
    int result = -1;
    for (;;) {
        draw(&p);
        int key = read_key();
        size_t qlen = strlen(p.query);

        // keys that work the same in both modes
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && p.sel > 0)
                p.sel--;
            if (key == KEY_DOWN && p.sel + 1 < p.nshown)
                p.sel++;
            continue;
        }
        if (key == KEY_TAB || key == KEY_BTAB) {
            p.tab = (p.tab + (key == KEY_TAB ? 1 : ntabs - 1)) % ntabs;
            refilter(&p);
            continue;
        }

        if (p.searching) {
            if (key == KEY_ENTER) {
                p.searching = false;                // keep the filter, back to moving
            } else if (key == KEY_ESC) {
                p.searching = false;
                p.query[0] = '\0';
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
            continue;
        }

        // normal mode: vim motions
        if (key == 'j') {
            if (p.sel + 1 < p.nshown)
                p.sel++;
        } else if (key == 'k') {
            if (p.sel > 0)
                p.sel--;
        } else if (key == 'l' || key == 'h') {
            p.tab = (p.tab + (key == 'l' ? 1 : ntabs - 1)) % ntabs;
            refilter(&p);
        } else if (key == 'g') {
            p.sel = 0;
        } else if (key == 'G') {
            p.sel = p.nshown ? p.nshown - 1 : 0;
        } else if (key == '/') {
            p.searching = true;
        } else if ((key == 'i' || key == KEY_ENTER) && describe && p.nshown > 0) {
            size_t item = p.shown[p.sel];           // i = what is this?
            erase(&p);
            p.drawn = 0;
            if (show_info(describe(ctx, item)) == 'd') {
                result = (int)item;                 // looked, now stop it
                break;
            }
        } else if (key == 'd') {                   // d = stop the selected thing
            if (p.nshown > 0) {
                result = (int)p.shown[p.sel];
                break;
            }
        } else if (key == KEY_ESC && p.query[0]) {
            p.query[0] = '\0';                     // Esc first drops a kept filter...
            refilter(&p);
        } else if (key == 'q' || key == KEY_ESC) {
            break;                                  // ...then quits
        }
    }

    erase(&p);
    raw_disable();
    free(shown);
    *tab = p.tab;
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
