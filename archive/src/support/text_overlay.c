#include "support/text_overlay.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    char *path; /* normalized */
    char *text;
    unsigned long long revision;
} overlay;

static overlay *overlays;
static unsigned long long revisions;

/* An absolute path without "." and ".." parts or repeated separators, with
 * its folder's symbolic links resolved when the folder exists. */
static void normalize(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    char folder[4096], resolved[PATH_MAX];
    snprintf(folder, sizeof folder, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
    if ((!slash || slash > path) && realpath(folder, resolved)) {
        snprintf(out, size, "%s/%s", resolved, slash ? slash + 1 : path);
        return;
    }
    char joined[4096];
    char cwd[2048] = "";
    if (path[0] != '/' && !getcwd(cwd, sizeof cwd)) {
        cwd[0] = '\0';
    }
    snprintf(joined, sizeof joined, "%s/%s", cwd, path);
    size_t used = 0;
    out[0] = '\0';
    for (char *part = strtok(joined, "/"); part; part = strtok(NULL, "/")) {
        if (!strcmp(part, ".")) {
            continue;
        }
        if (!strcmp(part, "..")) {
            char *slash = strrchr(out, '/');
            used = slash ? (size_t)(slash - out) : 0;
            out[used] = '\0';
            continue;
        }
        used += (size_t)snprintf(out + used, size - used, "/%s", part);
    }
    if (!used) {
        snprintf(out, size, "/");
    }
}

static overlay *find(const char *path)
{
    if (!acount(overlays)) {
        return NULL;
    }
    char key[4096];
    normalize(path, key, sizeof key);
    for (int i = 0; i < acount(overlays); i++) {
        if (!strcmp(overlays[i].path, key)) {
            return &overlays[i];
        }
    }
    return NULL;
}

void text_overlay_set(const char *path, const char *text)
{
    overlay *o = find(path);
    if (!text) {
        if (!o) {
            return;
        }
        free(o->path);
        free(o->text);
        *o = alast(overlays);
        asetlen(overlays, acount(overlays) - 1);
        revisions++;
        return;
    }
    if (!o) {
        char key[4096];
        normalize(path, key, sizeof key);
        apush(overlays, ((overlay){ .path = xstrdup(key) }));
        o = &alast(overlays);
    }
    char *copy = xstrdup(text);
    free(o->text);
    o->text = copy;
    o->revision = ++revisions;
}

const char *text_overlay_text(const char *path)
{
    const overlay *o = find(path);
    return o ? o->text : NULL;
}

unsigned long long text_overlay_revision(const char *path)
{
    const overlay *o = find(path);
    return o ? o->revision : 0;
}

unsigned long long text_overlay_generation()
{
    return revisions;
}

void text_overlay_clear()
{
    for (int i = 0; i < acount(overlays); i++) {
        free(overlays[i].path);
        free(overlays[i].text);
    }
    afree(overlays);
    revisions++;
}
