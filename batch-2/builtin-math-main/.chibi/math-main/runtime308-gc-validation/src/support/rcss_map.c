#include "support/rcss_map.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void put(char **out, const char *text, size_t length)
{
    if (!length) {
        return;
    }
    int start = acount(*out);
    asetlen(*out, start + (int)length);
    memcpy(*out + start, text, length);
}

/* ---------- Line maps ---------- */

/* The index of `file` among the map's files, added when it is missing. */
static int file_index(rcss_map *map, const char *file)
{
    for (int i = 0; i < acount(map->files); i++) {
        if (!strcmp(map->files[i], file)) {
            return i;
        }
    }
    apush(map->files, xstrdup(file));
    return acount(map->files) - 1;
}

void rcss_map_own(rcss_map *map, const char *file)
{
    for (int i = 0; i < acount(map->lines); i++) {
        rcss_origin *o = &map->lines[i];
        if (o->file < 0 && o->line) {
            o->file = file_index(map, file);
        }
    }
}

static void free_strings(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

void rcss_map_free(rcss_map *map)
{
    free_strings(map->files);
    afree(map->lines);
    *map = (rcss_map){ 0 };
}

void rcss_join(char **sheet, rcss_map *map, const char *text, const rcss_map *part)
{
    assert(!acount(*sheet) || alast(*sheet) == '\n');
    for (int line = 0; *text; line++) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) + 1 : strlen(text);
        put(sheet, text, length);
        if (!end) {
            put(sheet, "\n", 1);
        }
        text += length;
        rcss_origin o = part && line < acount(part->lines) ? part->lines[line]
                                                           : (rcss_origin){ -1, 0 };
        if (o.file >= 0) {
            o.file = file_index(map, part->files[o.file]);
        }
        apush(map->lines, o);
    }
}
