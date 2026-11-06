#ifndef CHIBI_RCSS_MAP_H
#define CHIBI_RCSS_MAP_H

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

#endif
