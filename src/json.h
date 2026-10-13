#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stdio.h>

#define JSON_MAX_DEPTH 16

/*
 * Streaming JSON writer: call in document order, it handles commas,
 * indentation and string escaping.
 *   json_obj(&j); json_key(&j, "pid"); json_int(&j, 42); json_end(&j);
 */
typedef struct {
    FILE *out;
    int depth;
    bool first[JSON_MAX_DEPTH];     // nothing written yet at this level
    char closer[JSON_MAX_DEPTH];    // '}' or ']' for each open level
    bool after_key;                 // next value follows "key": directly
} Json;

Json json_new(FILE *out);
void json_obj(Json *j);             // {
void json_arr(Json *j);             // [
void json_end(Json *j);             // } or ]
void json_key(Json *j, const char *key);
void json_str(Json *j, const char *s);      // NULL -> null
void json_int(Json *j, long long v);
void json_bool(Json *j, bool v);
void json_null(Json *j);
void json_done(Json *j);            // final newline

#endif
