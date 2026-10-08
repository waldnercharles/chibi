#include "authoring/ui_loader.h"

#include "assets/ui_document.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "support/ui_style.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    char directory[1024];
    file_watch asset;
    file_watch *uses;
    bool page;
} authored_ui;
static bool files_changed(file_watch *watches, double time)
{
    bool any = false;
    for (int i = 0; i < acount(watches); i++) {
        any |= file_watch_changed(&watches[i], watches[i].path, time);
    }
    return any;
}

/* Watches `paths` from now instead, taking them. */
static void watch_paths(file_watch **watches, char **paths, double time)
{
    asetlen(*watches, 0);
    for (int i = 0; i < acount(paths); i++) {
        file_watch w = { 0 };
        file_watch_changed(&w, paths[i], time);
        apush(*watches, w);
        free(paths[i]);
    }
    afree(paths);
}

/* The asset's styles, its widgets' files, then the stylesheets, taking both
 * lists. A file is watched while missing, so adding it counts. */
static char **used_files(const char *directory, const char *name, char **widgets, char **imports)
{
    apush(imports, ui_document_path(directory, name, true));
    for (int i = 0; i < acount(widgets); i++) {
        apush(imports, ui_document_path(directory, widgets[i], false));
        apush(imports, ui_document_path(directory, widgets[i], true));
        free(widgets[i]);
    }
    afree(widgets);
    return imports;
}

static void *open_ui(const char *root, const char *name, bool page)
{
    authored_ui *source = xcalloc(1, sizeof *source);
    source->name = xstrdup(name);
    snprintf(source->directory, sizeof source->directory, "%s/ui", root);
    source->page = page;
    return source;
}
static void close_ui(void *state)
{
    authored_ui *source = state;
    afree(source->uses);
    free(source->name);
    free(source);
}
static int read_ui(void *state, double time, ui_document *out, char *error, size_t size)
{
    authored_ui *source = state;
    char *path = ui_document_path(source->directory, source->name, false);
    bool changed = file_watch_changed(&source->asset, path, time);
    if (!files_changed(source->uses, time) && !changed) {
        free(path);
        return 0;
    }
    char diagnostic[200] = "";
    char **imports = NULL, **widgets = NULL;
    ui_document next = { 0 };
    bool valid = ui_document_load(source->directory, source->name, &imports, &next, diagnostic, sizeof diagnostic);
    if (valid && !source->page && next.body) {
        snprintf(diagnostic, sizeof diagnostic, "has a <body>, which only a scene's root document has");
        valid = false;
    }
    if (valid && !source->page) {
        valid = ui_document_expand(&next, source->name, source->directory, true, &widgets, diagnostic, sizeof diagnostic);
    }
    watch_paths(&source->uses, used_files(source->directory, source->name, widgets, imports), time);
    if (!valid) {
        ui_document_free(&next);
        snprintf(error, size, "ui/%s" UI_MARKUP_EXTENSION " %s", source->name, diagnostic);
        free(path);
        return -1;
    }
    if (source->page) {
        char *styles = ui_document_path(source->directory, source->name, true);
        rcss_map_own(&next.map, styles);
        free(styles);
        char *marked = ui_document_sources(next.rml, source->name, next.rml_line);
        free(next.rml);
        next.rml = marked;
    }
    char sheet[1100];
    snprintf(sheet, sizeof sheet, "%s %s", source->page ? "page" : "mount", path);
    ui_style_set(sheet, next.rcss, &next.map);
    free(path);
    if (size) {
        error[0] = 0;
    }
    *out = next;
    return 1;
}
const render_ui_loader AUTHORING_UI_LOADER = { open_ui, read_ui, close_ui };
ui_mount *authoring_ui_mount_new(const char *root, const char *name)
{
    ui_mount *mount = ui_mount_new(root, name);
    ui_mount_set_loader(mount, &AUTHORING_UI_LOADER);
    return mount;
}
bool authoring_ui_page_sync(ui_page *page, const char *root, const char *name, double time)
{
    ui_page_set_loader(page, &AUTHORING_UI_LOADER);
    return ui_page_sync(page, root, name, time);
}
render_ui *authoring_ui_new(const char *root, yyjson_mut_doc *data)
{
    render_ui *ui = render_ui_new(root, data);
    render_ui_set_loader(ui, &AUTHORING_UI_LOADER);
    return ui;
}

void authoring_viewport_set_ui(engine_viewport *viewport, const ui_world *world, const char *root)
{
    engine_viewport_set_ui(viewport, world, root);
    render_ui_set_loader(engine_viewport_ui(viewport), &AUTHORING_UI_LOADER);
}

const file_watch *authoring_ui_page_files(const ui_page *page)
{
    if (page->loader.open != open_ui || !page->source) {
        return NULL;
    }
    const authored_ui *source = page->source;
    return source->uses;
}
