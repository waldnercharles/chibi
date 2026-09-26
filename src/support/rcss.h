#ifndef CHIBI_RCSS_H
#define CHIBI_RCSS_H

#include <stddef.h>

/* ---------- Line maps ---------- */

/* Where one line of expanded RCSS came from: line `line` (1-based) of the
 * map's files[file], or of the text expanded itself when `file` is -1.
 * `line` is 0 for a line from no file, such as one a host adds. */
typedef struct rcss_origin
{
    int file, line;
} rcss_origin;

/* Where each line of expanded RCSS came from, as a source map records. A
 * flattened rule maps to its selector's line. Zero-initialize; rcss_map_free
 * frees it. */
typedef struct rcss_map
{
    char **files;       /* owned paths (support/array.h) */
    rcss_origin *lines; /* lines[i] is line i + 1's (support/array.h) */
} rcss_map;

/* Names `file` the source of the map's own lines, those with file -1. */
void rcss_map_own(rcss_map *map, const char *file);
void rcss_map_free(rcss_map *map);

/* Appends `text` to `sheet`, a stylesheet being built as a support/array.h
 * char array without a terminator, so that it starts and ends a line, and
 * the origins of its lines to `map`. `part` traces `text`'s lines; NULL, or
 * a line it does not trace, has no origin. Hosts join the stylesheets they
 * install this way and keep their maps in step. */
void rcss_join(char **sheet, rcss_map *map, const char *text, const rcss_map *part);

/* ---------- Expansion ---------- */

/* RCSS with the Sass conveniences RmlUi lacks, expanded to plain RCSS:
 *
 *   @import "controls.rcss";  the file beside the importing one, in place and
 *                             once per expansion
 *   .menu { .item { } }       nested rules flatten to `.menu .item`
 *   &:hover, .dark &          `&` stands for the enclosing selector
 *   .a { @media (x) { } }     a nested @media wraps the rules inside it
 *   // comment                line comments are dropped, like block comments
 *   :root { }                 names body, RmlUi's root, as CSS themes write it
 *
 * Each rule is one line, its declarations before its nested rules. @keyframes,
 * @decorator, @spritesheet and @font-face blocks pass through as written.
 * `directory` is where imports resolve; NULL leaves them as written, for
 * checking a stylesheet away from its files.
 *
 * `map`, when not NULL, must be empty. Its files become each file read, in
 * order, even when expansion fails, so a caller can watch a broken import;
 * on success its lines trace each output line. Returns owned RCSS, or NULL
 * with a message in `error`. */
char *rcss_expand(const char *text, const char *directory, rcss_map *map, char *error, size_t error_size);

/* ---------- Custom properties ---------- */

/* A custom property as written, for an editor that changes its value in
 * place. */
typedef struct rcss_variable
{
    char selector[128]; /* of the innermost rule declaring it, spaces collapsed */
    char name[64];      /* with its leading -- */
    int start, end;     /* the value's bytes in the text, trimmed */
} rcss_variable;

/* Appends each custom property `text` declares to `variables`
 * (support/array.h), in order. */
void rcss_variables(const char *text, rcss_variable **variables);

#endif
