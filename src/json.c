#include "json.h"

Json json_new(FILE *out)
{
    Json j = { .out = out };
    j.first[0] = true;
    return j;
}

/* Comma + newline + indent before a new value, unless it follows a key. */
static void before_value(Json *j)
{
    if (j->after_key) {
        j->after_key = false;
        return;
    }
    if (j->depth > 0) {
        fputs(j->first[j->depth] ? "\n" : ",\n", j->out);
        fprintf(j->out, "%*s", j->depth * 2, "");
    }
    j->first[j->depth] = false;
}

static void begin(Json *j, char opener, char closer)
{
    before_value(j);
    fputc(opener, j->out);
    if (j->depth < JSON_MAX_DEPTH - 1)
        j->depth++;
    j->first[j->depth] = true;
    j->closer[j->depth] = closer;
}

void json_obj(Json *j) { begin(j, '{', '}'); }
void json_arr(Json *j) { begin(j, '[', ']'); }

void json_end(Json *j)
{
    if (j->depth == 0)
        return;
    bool empty = j->first[j->depth];
    char closer = j->closer[j->depth];
    j->depth--;
    if (!empty)                                     // "[]" stays on one line
        fprintf(j->out, "\n%*s", j->depth * 2, "");
    fputc(closer, j->out);
}

static void write_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (c < 0x20)                           // other control chars
                fprintf(out, "\\u%04x", c);
            else
                fputc(c, out);                      // UTF-8 passes through as is
        }
    }
    fputc('"', out);
}

void json_key(Json *j, const char *key)
{
    before_value(j);
    write_string(j->out, key);
    fputs(": ", j->out);
    j->after_key = true;
}

void json_str(Json *j, const char *s)
{
    before_value(j);
    if (s)
        write_string(j->out, s);
    else
        fputs("null", j->out);
}

void json_int(Json *j, long long v)
{
    before_value(j);
    fprintf(j->out, "%lld", v);
}

void json_bool(Json *j, bool v)
{
    before_value(j);
    fputs(v ? "true" : "false", j->out);
}

void json_null(Json *j)
{
    before_value(j);
    fputs("null", j->out);
}

void json_done(Json *j)
{
    fputc('\n', j->out);
}
