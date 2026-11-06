#include "support/ui_style.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    char *rcss;
    rcss_map map;
} entry;

static entry *entries;

static entry *find(const char *sheet)
{
    for (int i = 0; i < acount(entries); i++) {
        if (!strcmp(entries[i].name, sheet)) {
            return &entries[i];
        }
    }
    return NULL;
}

static rcss_map copy_map(const rcss_map *map)
{
    rcss_map copy = { 0 };
    for (int i = 0; map && i < acount(map->files); i++) {
        apush(copy.files, xstrdup(map->files[i]));
    }
    if (map) {
        acopy(copy.lines, map->lines);
    }
    return copy;
}

void ui_style_set(const char *sheet, const char *rcss, const rcss_map *map)
{
    rcss_map copy = copy_map(map); /* first: `map` may be a recorded one */
    entry *e = find(sheet);
    if (e) {
        free(e->name);
        free(e->rcss);
        rcss_map_free(&e->map);
        adel(entries, e - entries);
    }
    if (!rcss) {
        rcss_map_free(&copy);
        return;
    }
    apush(entries, ((entry){ .name = xstrdup(sheet), .rcss = xstrdup(rcss), .map = copy }));
}

const rcss_map *ui_style_map(const char *rcss)
{
    for (int i = 0; i < acount(entries); i++) {
        if (!strcmp(entries[i].rcss, rcss)) {
            return &entries[i].map;
        }
    }
    return NULL;
}

bool ui_style_origin(const char *sheet, int line, char *path, size_t size, int *source_line)
{
    const entry *e = find(sheet);
    if (!e || line < 1 || line > acount(e->map.lines)) {
        return false;
    }
    rcss_origin o = e->map.lines[line - 1];
    if (o.file < 0) {
        return false;
    }
    snprintf(path, size, "%s", e->map.files[o.file]);
    *source_line = o.line;
    return true;
}
