#include "editor/documents/asset_code.h"

#include "assets/scene_document.h"
#include "assets/system_document.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct asset_code
{
    char *path, *disk, *valid;
    asset_source *source;
    kit_code_editor *editor;
    kit_code_span *spans;
    kit_code_marker *markers;
    asset_source_completions completions;
    kit_code_completion *items;
    char *hover;
    char **history;
    char **names;
    int step;
    bool completing;
    char error[256];
    const game_prototype_registry *registry;
    uintptr_t state_identity;
    unsigned long long overlay_revision;
};

static void follow(kit_code_editor *editor, const char *text, bool land)
{
    const char *shown = kit_code_text(editor, NULL);
    size_t start = 0, a = strlen(shown), b = strlen(text);
    while (start < a && start < b && shown[start] == text[start]) {
        start++;
    }
    if (start == a && start == b) {
        return;
    }
    while (a > start && b > start && shown[a - 1] == text[b - 1]) {
        a--;
        b--;
    }
    char *replacement = xmalloc(b - start + 1);
    memcpy(replacement, text + start, b - start);
    replacement[b - start] = 0;
    text_edit edit = { (int)start, (int)a, replacement };
    kit_code_apply(editor, &edit, 1);
    if (land) {
        kit_code_set_cursor(editor, (int)b);
    }
    free(replacement);
}

static const ecs_component_desc *resolve_component(const char *name, void *user)
{
    return game_prototype_registry_find(user, name);
}

static void check_asset(asset_code *c)
{
    if (!c->source->document || acount(c->source->diagnostics)) {
        return;
    }
    av_value *root = av_doc_root(c->source->document);
    char error[256] = "";
    bool ok = true;
    if (strstr(c->path, "/scenes/")) {
        game_scene_document *scene = game_scene_document_load_value(root, error, sizeof error);
        ok = scene != NULL;
        game_scene_document_free(scene);
    } else if (strstr(c->path, "/prototypes/") && c->registry) {
        game_prototype *prototype = game_prototype_load_value(root, c->registry, error, sizeof error);
        ok = prototype != NULL;
        game_prototype_free(prototype);
    } else if (strstr(c->path, "/systems/") && c->registry) {
        game_system_document *system = game_system_document_from_value(
            root,
            resolve_component,
            (void *)c->registry,
            error,
            sizeof error
        );
        ok = system != NULL;
        game_system_document_free(system);
    } else if (av_obj_get(root, "graphs")) {
        behavior_asset *asset = behavior_asset_from_value(root);
        ok = asset != NULL;
        if (!ok) {
            snprintf(error, sizeof error, "%s", graph_last_error());
        }
        behavior_asset_free(asset);
    }
    if (ok) {
        return;
    }
    asset_source_diagnostic d = { .line = 1, .column = 1 };
    snprintf(d.message, sizeof d.message, "%s", *error ? error : "Invalid asset document");
    apush(c->source->diagnostics, d);
}

static void understand(asset_code *c, const char *text)
{
    asset_source_free(c->source);
    c->source = asset_source_parse(text);
    check_asset(c);
    asetlen(c->spans, 0);
    asetlen(c->markers, 0);
    for (int i = 0; i < acount(c->source->spans); i++) {
        asset_source_span span = c->source->spans[i];
        apush(c->spans, ((kit_code_span){ span.start, span.end, span.cls }));
    }
    for (int i = 0; i < acount(c->source->diagnostics); i++) {
        asset_source_diagnostic *d = &c->source->diagnostics[i];
        apush(
            c->markers,
            ((kit_code_marker){ .line = d->line,
                                .severity = KIT_CODE_ERROR,
                                .message = d->message,
                                .start = d->start,
                                .end = d->end })
        );
    }
    c->error[0] = 0;
    if (acount(c->source->diagnostics)) {
        snprintf(c->error, sizeof c->error, "%s", c->source->diagnostics[0].message);
    }
}

static void valid_overlay(asset_code *c, bool live)
{
    if (!c->source->document || acount(c->source->diagnostics)) {
        return;
    }
    free(c->valid);
    c->valid = xstrdup(c->source->text);
    if (live) {
        text_overlay_set(c->path, c->valid);
        c->overlay_revision = text_overlay_revision(c->path);
    }
}

static void record(asset_code *c, const char *text)
{
    while (acount(c->history) > c->step + 1) {
        free(alast(c->history));
        apop(c->history);
    }
    apush(c->history, xstrdup(text));
    c->step++;
}

asset_code *asset_code_open(const char *path, char *error, size_t size)
{
    char *disk = text_file_read_disk(path);
    if (!disk) {
        snprintf(error, size, "Could not read %s", path);
        return NULL;
    }
    asset_code *c = xcalloc(1, sizeof *c);
    c->path = xstrdup(path);
    c->disk = disk;
    const char *text = text_overlay_text(path);
    text = text ? text : disk;
    c->editor = kit_code_editor_create(text);
    c->overlay_revision = text_overlay_revision(path);
    understand(c, text);
    valid_overlay(c, false);
    apush(c->history, xstrdup(text));
    return c;
}

av_value *asset_code_capture(asset_code *c)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_str(doc, root, "path", c->path);
    av_obj_add_str(doc, root, "text", c->source->text);
    av_obj_add_str(doc, root, "disk", c->disk);
    if (c->valid) {
        av_obj_add_str(doc, root, "valid", c->valid);
    }
    av_obj_add_bool(doc, root, "overlay", text_overlay_text(c->path) != NULL);
    av_obj_add_int(doc, root, "step", c->step);
    av_value *history = av_obj_add_arr(doc, root, "history");
    for (int i = 0; i < acount(c->history); i++) {
        av_arr_add_str(doc, history, c->history[i]);
    }
    int cursor = kit_code_cursor(c->editor), start, end;
    int anchor = cursor;
    if (kit_code_selection(c->editor, &start, &end)) {
        anchor = cursor == start ? end : start;
    }
    av_obj_add_int(doc, root, "cursor", cursor);
    av_obj_add_int(doc, root, "anchor", anchor);
    av_value *state = av_value_copy(root);
    av_doc_free(doc);
    c->state_identity = (uintptr_t)state;
    return state;
}

bool asset_code_has_state(const asset_code *c, const av_value *state)
{
    return c->state_identity == (uintptr_t)state;
}

asset_code *asset_code_restore(const av_value *state, const game_prototype_registry *registry)
{
    const char *path = av_get_str(av_obj_get(state, "path"));
    const char *text = av_get_str(av_obj_get(state, "text"));
    const char *disk = av_get_str(av_obj_get(state, "disk"));
    av_value *history = av_obj_get(state, "history");
    int step = av_get_int(av_obj_get(state, "step"));
    if (!path || !text || !disk || !av_is_arr(history) || step < 0 ||
        (size_t)step >= av_arr_size(history)) {
        return NULL;
    }
    for (size_t i = 0; i < av_arr_size(history); i++) {
        if (!av_is_str(av_arr_get(history, i))) {
            return NULL;
        }
    }
    asset_code *c = xcalloc(1, sizeof *c);
    c->path = xstrdup(path);
    c->disk = xstrdup(disk);
    c->registry = registry;
    c->state_identity = (uintptr_t)state;
    const char *valid = av_get_str(av_obj_get(state, "valid"));
    c->valid = valid ? xstrdup(valid) : NULL;
    c->editor = kit_code_editor_create(text);
    understand(c, text);
    for (size_t i = 0; i < av_arr_size(history); i++) {
        apush(c->history, xstrdup(av_get_str(av_arr_get(history, i))));
    }
    c->step = step;
    int cursor = av_get_int(av_obj_get(state, "cursor")),
        anchor = av_get_int(av_obj_get(state, "anchor"));
    int length = (int)strlen(text);
    if (cursor < 0 || cursor > length) {
        cursor = length;
    }
    if (anchor < 0 || anchor > length) {
        anchor = cursor;
    }
    kit_code_set_selection(c->editor, anchor, cursor);
    if (c->valid && av_get_bool(av_obj_get(state, "overlay"))) {
        text_overlay_set(path, c->valid);
        c->overlay_revision = text_overlay_revision(path);
    }
    return c;
}

void asset_code_adopt_overlay(asset_code *c)
{
    c->overlay_revision = text_overlay_revision(c->path);
}

void asset_code_free(asset_code *c)
{
    if (!c) {
        return;
    }
    if (c->overlay_revision && c->overlay_revision == text_overlay_revision(c->path)) {
        text_overlay_set(c->path, NULL);
    }
    free(c->path);
    free(c->disk);
    free(c->valid);
    free(c->hover);
    asset_source_free(c->source);
    kit_code_editor_destroy(c->editor);
    asset_source_completions_free(&c->completions);
    afree(c->items);
    afree(c->spans);
    afree(c->markers);
    for (int i = 0; i < acount(c->history); i++) {
        free(c->history[i]);
    }
    afree(c->history);
    for (int i = 0; i < acount(c->names); i++) {
        free(c->names[i]);
    }
    afree(c->names);
    free(c);
}

const char *asset_code_path(const asset_code *c)
{
    return c->path;
}
bool asset_code_dirty(const asset_code *c)
{
    return strcmp(c->disk, c->source->text) != 0;
}
const char *asset_code_error(const asset_code *c)
{
    return c->error;
}
kit_code_editor *asset_code_editor(asset_code *c)
{
    return c->editor;
}

void asset_code_set_registry(asset_code *c, const game_prototype_registry *registry)
{
    if (c->registry == registry) {
        return;
    }
    c->registry = registry;
    char *text = xstrdup(c->source->text);
    understand(c, text);
    free(text);
}

void asset_code_set_names(asset_code *c, const char *const *names, int count)
{
    bool same = count == acount(c->names);
    for (int i = 0; same && i < count; i++) {
        same = !strcmp(c->names[i], names[i]);
    }
    if (same) {
        return;
    }
    for (int i = 0; i < acount(c->names); i++) {
        free(c->names[i]);
    }
    asetlen(c->names, 0);
    for (int i = 0; i < count; i++) {
        apush(c->names, xstrdup(names[i]));
    }
}

/* Merge independent semantic changes from visual controls or another editor.
 * A conflicting field is left for the user; incomplete text stays untouched. */
static av_value *merge(
    av_doc *doc,
    const av_value *base,
    const av_value *current,
    const av_value *incoming,
    bool *ok
)
{
    if (av_equals(base, incoming) || av_equals(current, incoming)) {
        return av_clone(doc, current);
    }
    if (av_equals(base, current)) {
        return av_clone(doc, incoming);
    }
    if (!av_is_obj(base) || !av_is_obj(current) || !av_is_obj(incoming)) {
        *ok = false;
        return NULL;
    }
    av_value *result = av_obj(doc);
    for (size_t i = 0; i < av_obj_size(current); i++) {
        const char *key = av_get_str(av_obj_key_at(current, i));
        const av_value *a = av_obj_get(base, key), *b = av_obj_get(current, key),
                       *c = av_obj_get(incoming, key);
        if (!c) {
            if (a && !av_equals(a, b)) {
                *ok = false;
                return NULL;
            }
            if (!a) {
                av_obj_add_val(doc, result, key, av_clone(doc, b));
            }
            continue;
        }
        av_value *value = merge(doc, a, b, c, ok);
        if (!*ok) {
            return NULL;
        }
        av_obj_add_val(doc, result, key, value);
    }
    for (size_t i = 0; i < av_obj_size(incoming); i++) {
        const char *key = av_get_str(av_obj_key_at(incoming, i));
        if (av_obj_get(current, key)) {
            continue;
        }
        const av_value *a = av_obj_get(base, key), *c = av_obj_get(incoming, key);
        if (a && !av_equals(a, c)) {
            *ok = false;
            return NULL;
        }
        if (!a) {
            av_obj_add_val(doc, result, key, av_clone(doc, c));
        }
    }
    return result;
}

void asset_code_refresh(asset_code *c)
{
    char *disk = text_file_read_disk(c->path);
    if (!disk) {
        return;
    }
    if (!strcmp(disk, c->disk)) {
        free(disk);
        return;
    }
    char *text = NULL;
    if (asset_code_dirty(c)) {
        char error[256];
        av_doc *base = av_doc_read(c->disk, error, sizeof error);
        av_doc *incoming = av_doc_read(disk, error, sizeof error);
        av_doc *merged = av_doc_new();
        bool ok = base && incoming && c->source->document;
        if (ok) {
            av_value *value = merge(
                merged,
                av_doc_root(base),
                av_doc_root(c->source->document),
                av_doc_root(incoming),
                &ok
            );
            if (ok) {
                text = asset_source_reconcile(c->source->text, value, error, sizeof error);
            }
        }
        av_doc_free(base);
        av_doc_free(incoming);
        av_doc_free(merged);
        if (!text) {
            snprintf(c->error, sizeof c->error, "Source conflicts with a disk or visual edit; the current text is retained");
            free(disk);
            return;
        }
    } else {
        text = xstrdup(disk);
    }
    free(c->disk);
    c->disk = disk;
    follow(c->editor, text, false);
    understand(c, text);
    valid_overlay(c, true);
    record(c, text);
    free(text);
}

static void complete(asset_code *c)
{
    asset_source_completions_free(&c->completions);
    asetlen(c->items, 0);
    asset_source_complete(
        c->source,
        kit_code_cursor(c->editor),
        c->path,
        (const char *const *)c->names,
        acount(c->names),
        &c->completions
    );
    for (int i = 0; i < acount(c->completions.items); i++) {
        asset_source_completion *item = &c->completions.items[i];
        apush(
            c->items,
            ((kit_code_completion){ .label = item->label,
                                    .detail = item->detail,
                                    .insert = item->insert,
                                    .kind = item->kind })
        );
    }
    c->completing = acount(c->items) > 0;
}

kit_code_result asset_code_draw(asset_code *c, kit *k, const char *key, const kit_code_desc *preferences, bool live)
{
    asset_code_refresh(c);
    free(c->hover);
    c->hover = asset_source_hover(c->source, kit_code_hover(c->editor));
    kit_code_desc desc = *preferences;
    desc.spans = c->spans;
    desc.span_count = acount(c->spans);
    desc.markers = c->markers;
    desc.marker_count = acount(c->markers);
    desc.completions = c->completing ? c->items : NULL;
    desc.completion_count = c->completing ? acount(c->items) : 0;
    desc.completion_start = c->completions.start;
    desc.hover = c->hover;
    desc.history = true;
    kit_code_result result = kit_code(k, key, c->editor, &desc);
    if (result.events & KIT_CODE_CHANGED) {
        const char *text = kit_code_text(c->editor, NULL);
        record(c, text);
        understand(c, text);
        valid_overlay(c, live);
        if (!(result.events & KIT_CODE_COMPLETED)) {
            complete(c);
        }
    }
    if (result.events & KIT_CODE_SUGGEST) {
        complete(c);
    }
    if (result.events & (KIT_CODE_COMPLETED | KIT_CODE_DISMISSED)) {
        c->completing = false;
    }
    if (result.events & (KIT_CODE_UNDO | KIT_CODE_REDO)) {
        int next = c->step + ((result.events & KIT_CODE_REDO) ? 1 : -1);
        if (next >= 0 && next < acount(c->history)) {
            c->step = next;
            follow(c->editor, c->history[next], true);
            understand(c, c->history[next]);
            valid_overlay(c, live);
        }
        c->completing = false;
    }
    if (result.events & KIT_CODE_CURSOR && c->completing) {
        complete(c);
    }
    return result;
}

bool asset_code_save(asset_code *c, char *error, size_t size)
{
    if (!c->source->document || acount(c->source->diagnostics)) {
        snprintf(error, size, "%s", c->error);
        return false;
    }
    char *disk = text_file_read_disk(c->path);
    bool changed = !disk || strcmp(disk, c->disk);
    free(disk);
    if (changed) {
        snprintf(error, size, "Asset changed on disk; reopen it before saving");
        return false;
    }
    FILE *file = fopen(c->path, "wb");
    if (!file) {
        snprintf(error, size, "Could not save %s", c->path);
        return false;
    }
    size_t length = strlen(c->source->text);
    bool ok = fwrite(c->source->text, 1, length, file) == length;
    if (fclose(file)) {
        ok = false;
    }
    if (!ok) {
        snprintf(error, size, "Could not write %s", c->path);
        return false;
    }
    free(c->disk);
    c->disk = xstrdup(c->source->text);
    valid_overlay(c, true);
    c->error[0] = 0;
    return true;
}
