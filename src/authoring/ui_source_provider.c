#include "authoring/ui_source_provider.h"

#include "assets/ui_document.h"
#include "assets/ui_source.h"
#include "support/array.h"
#include "support/rooted_files.h"
#include "support/ui_style.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    FILE_MAX = 128,
    TEXT_MAX = 1024 * 1024,
    TOTAL_MAX = 8 * 1024 * 1024
};
typedef struct
{
    char *path, *text;
    size_t bytes;
    int document, revision;
} source_file;
struct ui_source_provider
{
    char *root, *content, *ui_root;
    rooted_files *files;
    workspace_documents *documents;
    source_file *overlays, *cached;
    size_t bytes;
    uint64_t generation;
    int states;
    char error[256];
};
typedef struct
{
    ui_source_provider *provider;
    char *name, *sheet;
    uint64_t generation;
    bool page;
} source_state;
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static void clear_files(source_file *files)
{
    for (int i = 0; i < acount(files); i++) {
        free(files[i].path);
        free(files[i].text);
    }
    afree(files);
}
static bool ui_path(const char *path)
{
    size_t n = strlen(path);
    return (n > 4 && !strcmp(path + n - 4, ".rml")) ||
           (n > 5 && !strcmp(path + n - 5, ".rcss"));
}
/* Lexically resolve imports, then enforce the rooted IO contract. No realpath
 * lookup is used, so missing overlays resolve and links cannot escape. */
static bool relative(ui_source_provider *s, const char *path, char out[4096])
{
    size_t root = strlen(s->root);
    if (strncmp(path, s->root, root) || path[root] != '/' ||
        strlen(path + root + 1) >= 4096) {
        return false;
    }
    char text[4096];
    snprintf(text, sizeof text, "%s", path + root + 1);
    int count = 0;
    size_t ends[256], bytes = 0;
    for (char *cursor = text; cursor;) {
        char *part = cursor;
        char *slash = strchr(part, '/');
        cursor = slash ? slash + 1 : NULL;
        if (slash) {
            *slash = 0;
        }
        if (!*part || !strcmp(part, ".")) {
            continue;
        }
        if (!strcmp(part, "..")) {
            if (!count) {
                return false;
            }
            bytes = ends[--count];
            continue;
        }
        if (strchr(part, '\\') || strchr(part, ':') || count == 256) {
            return false;
        }
        size_t n = strlen(part);
        if (bytes + n + 2 > 4096) {
            return false;
        }
        ends[count++] = bytes;
        if (bytes) {
            out[bytes++] = '/';
        }
        memcpy(out + bytes, part, n);
        bytes += n;
    }
    out[bytes] = 0;
    size_t content = strlen(s->content);
    return count && !strncmp(out, s->content, content) && out[content] == '/' &&
           ui_path(out);
}
static source_file *find_file(source_file *files, const char *path)
{
    for (int i = 0; i < acount(files); i++) {
        if (!strcmp(files[i].path, path)) {
            return &files[i];
        }
    }
    return NULL;
}
static char *read_source(void *context, const char *path)
{
    ui_source_provider *s = context;
    char key[4096];
    if (!relative(s, path, key)) {
        fail(s->error, sizeof s->error, "UI source reference escapes the configured content folder or is not RML/RCSS");
        return NULL;
    }
    source_file *file = find_file(s->cached, key);
    if (file) {
        return file->text ? xstrdup(file->text) : NULL;
    }
    if (acount(s->cached) >= FILE_MAX) {
        fail(s->error, sizeof s->error, "UI source closure exceeds 128 files");
        return NULL;
    }
    file = find_file(s->overlays, key);
    char *text = NULL;
    size_t bytes = 0;
    if (file) {
        text = xstrdup(file->text);
        bytes = file->bytes;
    } else {
        unsigned char *owned = NULL;
        if (!rooted_files_read(s->files, key, TEXT_MAX, &owned, &bytes)) {
            if (errno != ENOENT) {
                fail(s->error, sizeof s->error, "UI source file is unreadable, unsafe, changing or exceeds 1 MiB");
            }
        } else {
            text = (char *)owned;
        }
    }
    if (text && (!utf8_text_valid(text, bytes) || bytes > TOTAL_MAX - s->bytes)) {
        free(text);
        fail(s->error, sizeof s->error, "UI source closure must be UTF-8 and at most 8 MiB");
        return NULL;
    }
    if (text) {
        size_t n = strlen(key);
        ui_source *syntax = ui_source_parse(
            text,
            n > 4 && !strcmp(key + n - 4, ".rml") ? UI_SOURCE_RML : UI_SOURCE_RCSS
        );
        bool valid = !acount(syntax->diagnostics) &&
                     acount(syntax->elements) <= 4096 &&
                     acount(syntax->rules) <= 4096;
        if (!valid) {
            fail(s->error, sizeof s->error, "UI source has syntax errors or exceeds 4096 structural rows");
        }
        ui_source_free(syntax);
        if (!valid) {
            free(text);
            return NULL;
        }
    }
    if (!text) {
        bytes = 0;
    }
    s->bytes += bytes;
    apush(
        s->cached,
        ((source_file){ .path = xstrdup(key),
                        .text = text,
                        .bytes = bytes,
                        .document = file ? file->document : 0,
                        .revision = file ? file->revision : 0 })
    );
    return text ? xstrdup(text) : NULL;
}
bool ui_source_provider_refresh(ui_source_provider *s, char *error, size_t size)
{
    assert(s);
    if (s->generation == UINT64_MAX) {
        return fail(error, size, "UI source generation exhausted");
    }
    source_file *next = NULL;
    size_t bytes = 0;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(s->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(s->documents, handles[i], &view) !=
            WORKSPACE_DOCUMENT_OK) {
            continue;
        }
        if (!ui_path(view.path)) {
            continue;
        }
        size_t content = strlen(s->content);
        if (strncmp(view.path, s->content, content) || view.path[content] != '/') {
            continue;
        }
        if (view.bytes > TEXT_MAX || view.bytes > TOTAL_MAX - bytes ||
            acount(next) >= FILE_MAX) {
            clear_files(next);
            return fail(error, size, "Open UI source overlays exceed snapshot budget");
        }
        bytes += view.bytes;
        apush(
            next,
            ((source_file){ .path = xstrdup(view.path),
                            .text = xstrdup(view.text),
                            .bytes = view.bytes,
                            .document = view.document,
                            .revision = view.revision })
        );
    }
    clear_files(s->overlays);
    clear_files(s->cached);
    s->overlays = next;
    s->cached = NULL;
    s->bytes = 0;
    s->generation++;
    s->error[0] = 0;
    if (size) {
        error[0] = 0;
    }
    return true;
}
ui_source_provider *ui_source_provider_new(
    const char *root,
    const char *content,
    workspace_documents *documents,
    char *error,
    size_t size
)
{
    assert(root && content && documents);
    rooted_files *files = rooted_files_open(root);
    if (!files) {
        fail(error, size, "UI source workspace root is unreadable");
        return NULL;
    }
    rooted_files *folder = rooted_files_directory(files, content, false);
    if (!folder) {
        rooted_files_close(files);
        fail(error, size, "Configured content folder is unreadable or unsafe");
        return NULL;
    }
    rooted_files_close(folder);
    ui_source_provider *s = xcalloc(1, sizeof *s);
    s->files = files;
    s->documents = documents;
    s->root = xstrdup(root);
    s->content = xstrdup(content);
    size_t n = strlen(root) + strlen(content) + 5;
    s->ui_root = xmalloc(n);
    snprintf(s->ui_root, n, "%s/%s/ui", root, content);
    if (!ui_source_provider_refresh(s, error, size)) {
        ui_source_provider_free(s);
        return NULL;
    }
    return s;
}
void ui_source_provider_free(ui_source_provider *s)
{
    if (!s) {
        return;
    }
    assert(!s->states);
    clear_files(s->overlays);
    clear_files(s->cached);
    rooted_files_close(s->files);
    free(s->root);
    free(s->content);
    free(s->ui_root);
    free(s);
}
static void *open_source(void *context, const char *, const char *name, bool page)
{
    ui_source_provider *s = context;
    source_state *state = xcalloc(1, sizeof *state);
    state->provider = s;
    state->name = xstrdup(name);
    state->page = page;
    char key[256];
    snprintf(key, sizeof key, "source %p %s %.128s", (void *)state, page ? "page" : "mount", name);
    state->sheet = xstrdup(key);
    s->states++;
    return state;
}
static void close_source(void *context)
{
    source_state *state = context;
    assert(state->provider->states > 0);
    state->provider->states--;
    ui_style_set(state->sheet, NULL, NULL);
    free(state->sheet);
    free(state->name);
    free(state);
}
static int read_document(void *context, double, ui_document *out, char *error, size_t size)
{
    source_state *state = context;
    ui_source_provider *s = state->provider;
    if (state->generation == s->generation) {
        return 0;
    }
    state->generation = s->generation;
    if (!*state->name || strlen(state->name) > 127 ||
        strchr(state->name, '/') || strchr(state->name, '\\') ||
        strstr(state->name, "..") || strpbrk(state->name, "\"'<>&\r\n")) {
        fail(error, size, "UI asset name is invalid");
        return -1;
    }
    ui_document next = { 0 };
    text_reader reader = { .context = s, .read = read_source };
    char **uses = NULL;
    bool ok = ui_document_load_reader(s->ui_root, state->name, NULL, &next, error, size, &reader);
    if (ok && !state->page && next.body) {
        ok = fail(error, size, "A widget source cannot contain body attributes");
    }
    char *body = NULL;
    if (ok && next.body) {
        char *marked = ui_document_sources_at("<body>", state->name, next.body_line, next.body_start);
        size_t attributes = strlen(marked) - 7;
        size_t size = strlen(next.body) + attributes + 2;
        body = xmalloc(size);
        snprintf(body, size, "%.*s %s", (int)attributes, marked + 6, next.body);
        free(marked);
    }
    if (ok) {
        ok = ui_document_expand_reader(&next, state->name, s->ui_root, true, &uses, error, size, &reader);
    }
    if (ok && s->error[0]) {
        ok = fail(error, size, s->error);
    }
    for (int i = 0; i < acount(uses); i++) {
        free(uses[i]);
    }
    afree(uses);
    if (!ok) {
        free(body);
        ui_document_free(&next);
        return -1;
    }
    next.body = body;
    ui_style_set(state->sheet, next.rcss, &next.map);
    *out = next;
    if (size) {
        error[0] = 0;
    }
    return 1;
}
render_ui_loader ui_source_provider_loader(ui_source_provider *s)
{
    assert(s);
    return (render_ui_loader){ .context = s,
                               .open_context = open_source,
                               .read = read_document,
                               .close = close_source,
                               .resident_images = true };
}
bool ui_source_provider_contains(const ui_source_provider *s, const char *path)
{
    assert(s && path);
    for (int i = 0; i < acount(s->cached); i++) {
        if (!strcmp(s->cached[i].path, path) && s->cached[i].text) {
            return true;
        }
    }
    return false;
}
bool ui_source_provider_current(ui_source_provider *s)
{
    assert(s);
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(s->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < acount(s->cached); i++) {
        source_file *file = &s->cached[i];
        bool found = false;
        for (int j = 0; j < count; j++) {
            workspace_document_view view;
            if (workspace_document_read(s->documents, handles[j], &view) ||
                strcmp(view.path, file->path)) {
                continue;
            }
            found = true;
            if (file->document) {
                if (file->document != view.document || file->revision != view.revision) {
                    return false;
                }
            } else {
                if (!file->text || file->bytes != view.bytes ||
                    memcmp(file->text, view.text, view.bytes)) {
                    return false;
                }
                file->document = view.document;
                file->revision = view.revision;
            }
            break;
        }
        if (!found && file->document) {
            return false;
        }
    }
    return true;
}

bool ui_source_provider_styles(
    ui_source_provider *s,
    const char *page,
    ui_source_styles_visit visit,
    void *user,
    char *error,
    size_t size
)
{
    if (!page || !*page || strlen(page) > 127 || strchr(page, '/') ||
        strchr(page, '\\') || strstr(page, "..") || strpbrk(page, "\"'<>&\r\n")) {
        return fail(error, size, "UI page name is invalid");
    }
    text_reader reader = { .context = s, .read = read_source };
    ui_document document = { 0 };
    char **imports = NULL, **widgets = NULL;
    bool ok = ui_document_load_reader(s->ui_root, page, &imports, &document, error, size, &reader);
    if (ok) {
        ok = ui_document_expand_reader(&document, page, s->ui_root, true, &widgets, error, size, &reader);
    }
    /* Preserve predecessor section order: imported styles, page styles,
     * then each expanded widget's styles. Widget markup cannot declare RCSS. */
    apush(imports, ui_document_path(s->ui_root, page, true));
    for (int i = 0; i < acount(widgets); i++) {
        apush(imports, ui_document_path(s->ui_root, widgets[i], true));
    }
    char **seen = NULL;
    for (int i = 0; ok && i < acount(imports); i++) {
        char path[4096];
        if (!relative(s, imports[i], path)) {
            ok = fail(error, size, "Stylesheet escapes the workspace");
            break;
        }
        bool duplicate = false;
        for (int j = 0; j < acount(seen); j++) {
            duplicate |= !strcmp(path, seen[j]);
        }
        if (duplicate) {
            continue;
        }
        apush(seen, xstrdup(path));
        char *text = read_source(s, imports[i]);
        if (text) {
            ok = visit(user, path, text);
        }
        free(text);
    }
    if (ok && s->error[0]) {
        ok = fail(error, size, s->error);
    }
    if (!ok && size && !error[0]) {
        fail(error, size, "Stylesheet inventory exceeded its bound");
    }
    for (int i = 0; i < acount(imports); i++) {
        free(imports[i]);
    }
    for (int i = 0; i < acount(widgets); i++) {
        free(widgets[i]);
    }
    for (int i = 0; i < acount(seen); i++) {
        free(seen[i]);
    }
    afree(imports);
    afree(widgets);
    afree(seen);
    ui_document_free(&document);
    return ok;
}
