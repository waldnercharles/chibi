#include "editor/documents/asset_code.h"

#include "assets/code_source.h"
#include "assets/code_source_intelligence.h"
#include "assets/scene_document.h"
#include "assets/system_document.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/scene_internal.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct source_snapshot
{
    char *text, *metadata;
} source_snapshot;

struct asset_code
{
    char *path, *disk, *valid, *valid_metadata, *metadata, *disk_metadata;
    const behavior_asset **libraries, **events;
    code_reference *references;
    code_prototype *prototypes;
    const char **owner_prototypes;
    code_source_context context;
    uint64_t catalog_revision;
    asset_source *source;
    code_source *native, *valid_native, *execution_source;
    behavior_asset *candidate, *shared;
    const behavior_asset *shared_asset;
    unsigned shared_revision, system_revision;
    bool system_initialized;
    bool source_pending, replaying;
    bool runtime_held;
    bool graph_draft;
    bool baseline_pending;
    bool native_view;
    uint64_t execution_graph, execution_node;
    double time, apply_due;
    kit_code_editor *editor;
    kit_code_span *spans;
    kit_code_marker *markers, *breakpoints;
    asset_source_completions completions;
    kit_code_completion *items;
    char *hover;
    source_snapshot *history;
    char **names;
    int step;
    bool completing;
    char error[256];
    const game_prototype_registry *registry;
    uintptr_t state_identity;
    unsigned long long overlay_revision;
};

static void complete(asset_code *c);

static void free_prototypes(code_prototype *prototypes, const char **owners)
{
    for (int i = 0; i < acount(prototypes); i++) {
        free((char *)prototypes[i].name);
        for (int j = 0; j < prototypes[i].component_count; j++) {
            free((char *)prototypes[i].components[j]);
        }
        free((void *)prototypes[i].components);
    }
    afree(prototypes);
    for (int i = 0; i < acount(owners); i++) {
        free((char *)owners[i]);
    }
    afree(owners);
}

static size_t visible_length(const asset_code *, const char *text)
{
    return strlen(text);
}

static char *visible_text(asset_code *, const char *text)
{
    return xstrdup(text);
}

static kit_code_editor *create_view(asset_code *c, const char *text)
{
    c->native_view = code_source_is_structured(text) ||
                     (strlen(c->path) >= 5 &&
                      !strcmp(c->path + strlen(c->path) - 5, ".code"));
    char *shown = visible_text(c, text);
    kit_code_editor *editor = kit_code_editor_create(shown);
    free(shown);
    return editor;
}

static char *edited_text(asset_code *c)
{
    return xstrdup(kit_code_text(c->editor, NULL));
}

static void follow(asset_code *c, const char *source, bool land)
{
    kit_code_editor *editor = c->editor;
    char *text = visible_text(c, source);
    int cursor = kit_code_cursor(editor), anchor = cursor, selection_start,
        selection_end;
    if (kit_code_selection(editor, &selection_start, &selection_end)) {
        anchor = cursor == selection_start ? selection_end : selection_start;
    }
    const char *shown = kit_code_text(editor, NULL);
    size_t start = 0, a = strlen(shown), b = strlen(text);
    while (start < a && start < b && shown[start] == text[start]) {
        start++;
    }
    if (start == a && start == b) {
        free(text);
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
    } else {
        int positions[] = { anchor, cursor };
        for (int i = 0; i < 2; i++) {
            if (positions[i] > (int)a) {
                positions[i] += (int)b - (int)a;
            } else if (positions[i] > (int)b) {
                positions[i] = (int)b;
            }
        }
        kit_code_set_selection(editor, positions[0], positions[1]);
    }
    free(replacement);
    free(text);
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
    } else if (
        av_obj_get(root, "graphs") || strstr(c->path, "/behaviors/") ||
        strstr(c->path, "/libraries/") || strstr(c->path, "/events/") ||
        strstr(c->path, "/materials/") || strstr(c->path, "/shapes/") ||
        strstr(c->path, "/fields/") || strstr(c->path, "/models/")
    ) {
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

static void native_markers(asset_code *c)
{
    asetlen(c->markers, 0);
    c->error[0] = 0;
    for (int i = 0; i < acount(c->native->diagnostics); i++) {
        code_source_diagnostic *d = &c->native->diagnostics[i];
        apush(
            c->markers,
            ((kit_code_marker){ .line = d->line,
                                .severity = d->severity == CMPL_ERROR ? KIT_CODE_ERROR
                                          : d->severity == CMPL_WARNING ? KIT_CODE_WARNING
                                                                        : KIT_CODE_INFO,
                                .message = d->message,
                                .start = d->start,
                                .end = d->end })
        );
        if (d->severity == CMPL_ERROR && !c->error[0]) {
            snprintf(c->error, sizeof c->error, "%s", d->message);
        }
    }
}

static void compile_native(asset_code *c)
{
    if (!c->native->asset) {
        return;
    }
    behavior_asset *checked = code_source_compile_behavior(c->native);
    behavior_asset_free(checked);
    native_markers(c);
}

static void understand_with_metadata(asset_code *c, const char *text, const char *metadata)
{
    bool previous_valid = c->native && c->valid && !strcmp(c->native->text, c->valid);
    const code_source *previous = previous_valid ? c->native : c->valid_native;
    if (!previous && c->native && c->native->asset) {
        previous = c->native;
    }
    code_source *native = code_source_parse_context(text, previous, &c->context);
    if (metadata) {
        code_source_metadata_restore(native, metadata);
    }
    bool loaded_metadata = false;
    if (!metadata && !c->metadata && !strcmp(text, c->disk) &&
        code_source_is_behavior(native)) {
        loaded_metadata = code_source_metadata_load(native, c->path);
    }
    if (previous_valid) {
        code_source_free(c->valid_native);
        c->valid_native = c->native;
    } else {
        code_source_free(c->native);
    }
    c->native = native;
    free(c->metadata);
    /* Declarative documents have no native graph metadata. A checkpoint may
       need its library catalog before its exact metadata can be restored. */
    c->metadata = native->asset && code_source_is_structured(native->text)
                    ? code_source_metadata_export_source(native)
                : metadata && code_source_is_structured(native->text)
                    ? xstrdup(metadata)
                    : NULL;
    if (loaded_metadata) {
        free(c->disk_metadata);
        c->disk_metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    }
    behavior_asset_free(c->candidate);
    c->candidate = code_source_build_behavior(native);
    if (c->candidate && c->native->document && strstr(c->path, "/systems/")) {
        char error[256];
        game_system_document *system = game_system_document_from_value(
            av_doc_root(c->native->document),
            resolve_system_component,
            NULL,
            error,
            sizeof error
        );
        if (!system) {
            code_source_diagnostic diagnostic = { .line = 1,
                                                  .column = 1,
                                                  .severity = CMPL_ERROR };
            snprintf(diagnostic.message, sizeof diagnostic.message, "%s", error);
            apush(c->native->diagnostics, diagnostic);
        }
        game_system_document_free(system);
    }
    asset_source_free(c->source);
    c->source = asset_source_parse(text);
    if (!c->native_view && !code_source_is_behavior(native)) {
        check_asset(c);
    }
    c->source_pending = true;
    c->replaying = false;
    c->graph_draft = false;
    asetlen(c->spans, 0);
    asetlen(c->markers, 0);
    asetlen(c->breakpoints, 0);
    asset_source_span *spans = (c->native_view || code_source_is_behavior(native))
                                 ? native->spans
                                 : c->source->spans;
    asset_source_span *semantic = c->native->intelligence
                                    ? code_intelligence_highlights(c->native)
                                    : NULL;
    for (int i = 0; i < acount(spans); i++) {
        asset_source_span span = spans[i];
        int at = span.start;
        while (at < span.end) {
            int found = -1;
            for (int j = 0; j < acount(semantic); j++) {
                if (semantic[j].end <= at || semantic[j].start >= span.end) {
                    continue;
                }
                if (found < 0 || semantic[j].start < semantic[found].start ||
                    (semantic[j].start == semantic[found].start &&
                     semantic[j].end < semantic[found].end)) {
                    found = j;
                }
            }
            if (found < 0) {
                apush(c->spans, ((kit_code_span){ at, span.end, span.cls }));
                break;
            }
            if (semantic[found].start > at) {
                apush(c->spans, ((kit_code_span){ at, semantic[found].start, span.cls }));
                at = semantic[found].start;
            }
            int end = semantic[found].end < span.end ? semantic[found].end : span.end;
            apush(c->spans, ((kit_code_span){ at, end, semantic[found].cls }));
            at = end;
        }
    }
    afree(semantic);
    if (c->native_view || code_source_is_behavior(native)) {
        for (int i = 0; i < acount(native->diagnostics); i++) {
            code_source_diagnostic *d = &native->diagnostics[i];
            apush(
                c->markers,
                ((kit_code_marker){ .line = d->line,
                                    .severity = d->severity == CMPL_ERROR ? KIT_CODE_ERROR
                                              : d->severity == CMPL_WARNING ? KIT_CODE_WARNING
                                                                            : KIT_CODE_INFO,
                                    .message = d->message,
                                    .start = d->start,
                                    .end = d->end })
            );
        }
    } else {
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
    }
    int start, end;
    for (int g = 0; c->candidate && g < acount(c->candidate->graphs); g++) {
        graph *gr = c->candidate->graphs[g];
        for (int n = 0; n < acount(gr->nodes); n++) {
            node *at = gr->nodes[n];
            if (!at->breakpoint ||
                !asset_code_location(c, gr->graph_id, at->id, &start, &end)) {
                continue;
            }
            int line, column;
            code_source_position(c->native, start, &line, &column);
            apush(
                c->breakpoints,
                ((kit_code_marker){ .line = line,
                                    .severity = KIT_CODE_ERROR,
                                    .message = "Breakpoint (F9 to remove)",
                                    .start = start,
                                    .end = end,
                                    .indicator_only = true })
            );
        }
    }
    c->error[0] = 0;
    for (int i = 0; i < acount(c->markers); i++) {
        if (c->markers[i].severity == KIT_CODE_ERROR) {
            snprintf(c->error, sizeof c->error, "%s", c->markers[i].message);
            break;
        }
    }
}

static void understand(asset_code *c, const char *text)
{
    /* Catalog setup can reparse a newly opened source before it has a valid
       runtime generation. Retain its loaded presentation for identical text. */
    char *metadata = c->metadata && c->native && !strcmp(text, c->native->text)
                       ? xstrdup(c->metadata)
                       : NULL;
    understand_with_metadata(c, text, metadata);
    free(metadata);
}

static void snapshot_free(source_snapshot snapshot)
{
    free(snapshot.text);
    free(snapshot.metadata);
}

static source_snapshot snapshot_create(asset_code *c, const char *text)
{
    return (source_snapshot){ xstrdup(text), c->metadata ? xstrdup(c->metadata) : NULL };
}

static void valid_overlay(asset_code *c, bool live)
{
    if (code_source_is_behavior(c->native) && c->source_pending) {
        return;
    }
    /* An incomplete executable draft can parse as an empty declarative
       document. It must keep the last valid executable identity snapshot. */
    if (c->error[0] || (!c->candidate && (c->native_view || !c->source->document))) {
        return;
    }
    free(c->valid);
    c->valid = xstrdup(c->source->text);
    free(c->valid_metadata);
    c->valid_metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    if (live) {
        text_overlay_set(c->path, c->valid);
        c->overlay_revision = text_overlay_revision(c->path);
    }
}

static void record(asset_code *c, const char *text)
{
    while (acount(c->history) > c->step + 1) {
        snapshot_free(alast(c->history));
        apop(c->history);
    }
    apush(c->history, snapshot_create(c, text));
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
    c->editor = create_view(c, text);
    c->overlay_revision = text_overlay_revision(path);
    understand(c, text);
    free(c->disk_metadata);
    c->disk_metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    valid_overlay(c, false);
    apush(c->history, snapshot_create(c, text));
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
    if (c->disk_metadata) {
        av_obj_add_str(doc, root, "disk_metadata", c->disk_metadata);
    }
    if (c->metadata) {
        av_obj_add_str(doc, root, "metadata", c->metadata);
    }
    if (c->valid) {
        av_obj_add_str(doc, root, "valid", c->valid);
        if (c->valid_metadata) {
            av_obj_add_str(doc, root, "valid_metadata", c->valid_metadata);
        }
    }
    av_obj_add_bool(doc, root, "overlay", text_overlay_text(c->path) != NULL);
    av_obj_add_int(doc, root, "step", c->step);
    if (c->runtime_held) {
        av_obj_add_bool(doc, root, "runtime_hold", true);
    }
    if (c->graph_draft) {
        av_obj_add_bool(doc, root, "graph_draft", true);
    }
    av_value *history = av_obj_add_arr(doc, root, "history");
    for (int i = 0; i < acount(c->history); i++) {
        av_value *snapshot = av_arr_add_obj(doc, history);
        av_obj_add_str(doc, snapshot, "text", c->history[i].text);
        if (c->history[i].metadata) {
            av_obj_add_str(doc, snapshot, "metadata", c->history[i].metadata);
        }
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
        if (!av_get_str(av_obj_get(av_arr_get(history, i), "text"))) {
            return NULL;
        }
    }
    asset_code *c = xcalloc(1, sizeof *c);
    c->path = xstrdup(path);
    c->disk = xstrdup(disk);
    const char *disk_metadata = av_get_str(av_obj_get(state, "disk_metadata"));
    c->disk_metadata = disk_metadata ? xstrdup(disk_metadata) : NULL;
    c->registry = registry;
    c->runtime_held = av_get_bool(av_obj_get(state, "runtime_hold"));
    c->state_identity = (uintptr_t)state;
    const char *valid = av_get_str(av_obj_get(state, "valid"));
    c->valid = valid ? xstrdup(valid) : NULL;
    c->valid_native = valid ? code_source_parse(valid) : NULL;
    const char *valid_metadata = av_get_str(av_obj_get(state, "valid_metadata"));
    c->valid_metadata = valid_metadata ? xstrdup(valid_metadata) : NULL;
    if (c->valid_native && valid_metadata) {
        code_source_metadata_restore(c->valid_native, valid_metadata);
    }
    c->editor = create_view(c, text);
    understand_with_metadata(c, text, av_get_str(av_obj_get(state, "metadata")));
    c->graph_draft = av_get_bool(av_obj_get(state, "graph_draft"));
    for (size_t i = 0; i < av_arr_size(history); i++) {
        const av_value *snapshot = av_arr_get(history, i);
        const char *metadata = av_get_str(av_obj_get(snapshot, "metadata"));
        apush(
            c->history,
            ((source_snapshot){
                xstrdup(av_get_str(av_obj_get(snapshot, "text"))),
                metadata ? xstrdup(metadata) : NULL,
            })
        );
    }
    c->step = step;
    int cursor = av_get_int(av_obj_get(state, "cursor")),
        anchor = av_get_int(av_obj_get(state, "anchor"));
    int length = (int)visible_length(c, text);
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
    free(c->valid_metadata);
    free(c->metadata);
    free(c->disk_metadata);
    free(c->hover);
    asset_source_free(c->source);
    code_source_free(c->native);
    code_source_free(c->valid_native);
    code_source_free(c->execution_source);
    behavior_asset_free(c->candidate);
    behavior_asset_free(c->shared);
    kit_code_editor_destroy(c->editor);
    asset_source_completions_free(&c->completions);
    afree(c->items);
    afree(c->spans);
    afree(c->markers);
    afree(c->breakpoints);
    afree(c->libraries);
    afree(c->events);
    for (int i = 0; i < acount(c->references); i++) {
        free((char *)c->references[i].name);
    }
    afree(c->references);
    free_prototypes(c->prototypes, c->owner_prototypes);
    for (int i = 0; i < acount(c->history); i++) {
        snapshot_free(c->history[i]);
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
    return strcmp(c->disk, c->source->text) != 0 ||
           strcmp(c->metadata ? c->metadata : "", c->disk_metadata ? c->disk_metadata : "") !=
               0;
}
const char *asset_code_error(const asset_code *c)
{
    return c->error;
}
const char *asset_code_text(const asset_code *c)
{
    return c->source->text;
}

kit_code_editor *asset_code_editor(asset_code *c)
{
    return c->editor;
}

bool asset_code_can_replay(const asset_code *c, bool redo)
{
    int next = c->step + (redo ? 1 : -1);
    return next >= 0 && next < acount(c->history);
}

bool asset_code_replay(asset_code *c, bool redo, bool live)
{
    if (!asset_code_can_replay(c, redo)) {
        return false;
    }
    c->step += redo ? 1 : -1;
    source_snapshot snapshot = c->history[c->step];
    follow(c, snapshot.text, true);
    understand_with_metadata(c, snapshot.text, snapshot.metadata);
    c->replaying = true;
    valid_overlay(c, live);
    c->completing = false;
    return true;
}

static bool validate_candidate(asset_code *c, editor *e)
{
    c->source_pending = false;
    if (!c->candidate || c->error[0]) {
        return false;
    }
    editor_source_diagnostics(e, c->candidate, c->native);
    /* Diagnostics own the strings borrowed by gutter markers. */
    native_markers(c);
    if (c->error[0]) {
        return false;
    }
    return true;
}

bool asset_code_validate(asset_code *c, editor *e)
{
    if (c->source_pending && c->time >= c->apply_due) {
        if (!validate_candidate(c, e)) {
            return false;
        }
        valid_overlay(c, false);
    }
    return !c->source_pending && !c->error[0] && c->candidate;
}

static void retain_layout(behavior_asset *candidate, const behavior_asset *working)
{
    for (int i = 0; i < acount(candidate->graphs); i++) {
        graph *target = candidate->graphs[i];
        const graph *previous = behavior_asset_find_graph(working, target->graph_id);
        if (!previous) {
            continue;
        }
        for (int j = 0; j < acount(target->nodes); j++) {
            node *n = target->nodes[j];
            const node *old = graph_node_by_id(previous, n->id);
            if (old) {
                n->x = old->x;
                n->y = old->y;
                n->placed = old->placed;
            }
        }
    }
}

static bool refresh_graph_context(asset_code *c, const behavior_asset *working)
{
    char error[256];
    behavior_asset *refreshed = code_source_refresh_imports(c->valid_native, working, error, sizeof error);
    if (!refreshed) {
        snprintf(c->error, sizeof c->error, "%s", error);
        c->source_pending = false;
        apush(
            c->markers,
            ((kit_code_marker){ .line = 1, .severity = KIT_CODE_ERROR, .message = c->error })
        );
        return false;
    }
    /* Commit graph edits against the current signature before adopting a
       reparse. The shared frontend still verifies exact native equivalence. */
    if (!behavior_asset_equal(c->shared, working)) {
        char *metadata = NULL;
        char *text = code_source_reconcile_with_metadata(c->native, refreshed, &metadata, error, sizeof error);
        if (!text) {
            behavior_asset_free(refreshed);
            snprintf(c->error, sizeof c->error, "%s", error);
            c->source_pending = false;
            apush(
                c->markers,
                ((kit_code_marker){ .line = 1, .severity = KIT_CODE_ERROR, .message = c->error })
            );
            return false;
        }
        follow(c, text, false);
        understand_with_metadata(c, text, metadata);
        free(metadata);
        record(c, text);
        c->graph_draft = true;
        free(text);
    }
    behavior_asset_free(refreshed);
    if (c->candidate) {
        retain_layout(c->candidate, working);
    }
    return true;
}

/* A graph may have been edited before its source view was opened. Transfer
 * those complete snapshots before shared source history takes ownership. */
static bool import_graph_history(asset_code *c, const editor *e, const behavior_asset *working)
{
    if (!acount(e->undo) && !acount(e->redo)) {
        return true;
    }
    source_snapshot *texts = NULL;
    char error[256];
    int undo = acount(e->undo), redo = acount(e->redo);
    for (int i = 0; i < undo + 1 + redo; i++) {
        const behavior_asset *snapshot = i < undo  ? e->undo[i]
                                       : i == undo ? working
                                                   : e->redo[redo - (i - undo)];
        char *metadata = NULL;
        char *text = code_source_reconcile_with_metadata(c->native, snapshot, &metadata, error, sizeof error);
        if (!text) {
            for (int j = 0; j < acount(texts); j++) {
                snapshot_free(texts[j]);
            }
            afree(texts);
            snprintf(c->error, sizeof c->error, "%s", error);
            return false;
        }
        apush(texts, ((source_snapshot){ text, metadata }));
    }
    for (int i = 0; i < acount(c->history); i++) {
        snapshot_free(c->history[i]);
    }
    afree(c->history);
    c->history = texts;
    c->step = undo;
    follow(c, texts[undo].text, false);
    understand_with_metadata(c, texts[undo].text, texts[undo].metadata);
    c->graph_draft = true;
    return true;
}

bool asset_code_sync_graph(asset_code *c, editor *e, bool live)
{
    const behavior_asset *working = editor_behavior_asset(e);
    /* Opening a clean source view must not replace the loaded graph or turn
       its initial automatic arrangement into an authored edit. */
    if (!c->shared && !asset_code_dirty(c)) {
        if (!import_graph_history(c, e, working)) {
            return false;
        }
        if (c->source_pending) {
            validate_candidate(c, e);
        }
        c->source_pending = false;
        c->baseline_pending = e->arrange_pending;
        c->shared = behavior_asset_clone(working);
        c->shared_asset = working;
        c->shared_revision = editor_edit_revision(e);
        valid_overlay(c, false);
        return false;
    }
    if (c->baseline_pending && !asset_code_dirty(c) && !editor_can_undo(e)) {
        if (e->arrange_pending) {
            return false;
        }
        c->baseline_pending = false;
        behavior_asset_free(c->shared);
        c->shared = behavior_asset_clone(working);
        c->shared_asset = working;
        c->shared_revision = editor_edit_revision(e);
        return false;
    }
    c->baseline_pending = false;
    /* Source edits publish only a complete candidate. The current graph and
       runtime keep the last valid generation while the draft is incomplete. */
    if (c->source_pending) {
        if (c->time < c->apply_due) {
            return false;
        }
        bool context_changed = c->shared && c->valid_native &&
                               !strcmp(c->valid_native->text, c->native->text);
        if (context_changed && !c->error[0] && !refresh_graph_context(c, working)) {
            return false;
        }
        if (!validate_candidate(c, e)) {
            return false;
        }
        bool changed = !behavior_asset_equal(c->candidate, working);
        if (changed && editor_source_behavior_asset(e, c->candidate)) {
            snprintf(c->error, sizeof c->error, "Could not apply source to the graph");
            return false;
        }
        /* Replayed source snapshots already own their node placement. An
           automatic arrangement here would become a new edit and erase Redo. */
        if (c->replaying) {
            e->arrange_pending = false;
        }
        c->replaying = false;
        behavior_asset_free(c->shared);
        c->shared = behavior_asset_clone(editor_behavior_asset(e));
        c->shared_asset = editor_behavior_asset(e);
        c->shared_revision = editor_edit_revision(e);
        valid_overlay(c, live);
        return changed;
    }
    if (c->shared && c->shared_asset == working &&
        c->shared_revision == editor_edit_revision(e) &&
        !editor_has_pending_edits(e)) {
        return false;
    }
    if (!c->shared) {
        return false;
    }
    if (behavior_asset_equal(c->shared, working)) {
        c->shared_asset = working;
        c->shared_revision = editor_edit_revision(e);
        return false;
    }
    if (c->error[0] && !c->graph_draft) {
        snprintf(c->error, sizeof c->error, "Fix the source errors before editing its graph");
        return false;
    }
    char error[256];
    const code_source *base = c->native->asset ? c->native : c->valid_native;
    char *metadata = NULL;
    char *text = code_source_reconcile_with_metadata(base, working, &metadata, error, sizeof error);
    if (!text) {
        snprintf(c->error, sizeof c->error, "%s", error);
        return false;
    }
    follow(c, text, false);
    understand_with_metadata(c, text, metadata);
    free(metadata);
    c->graph_draft = true;
    c->apply_due = c->time;
    record(c, text);
    free(text);
    behavior_asset_free(c->shared);
    c->shared = behavior_asset_clone(working);
    c->shared_asset = working;
    c->shared_revision = editor_edit_revision(e);
    asset_code_sync_graph(c, e, live);
    return true;
}

bool asset_code_sync_system(asset_code *c, editor *e, game_system_document *system, bool live)
{
    bool query_source_changed = false;
    unsigned revision = game_system_document_revision(system);
    if (!c->system_initialized) {
        c->system_initialized = true;
        c->system_revision = revision;
    }
    if (!c->source_pending && !c->error[0] && c->system_revision != revision) {
        av_doc *document = av_doc_new();
        av_value *root = game_system_document_to_value(
            document,
            system,
            resolve_system_component,
            system_component_name,
            NULL
        );
        av_value *query = root ? av_obj_get(root, "query") : NULL;
        av_value *old_query = c->native->document
                                ? av_obj_get(av_doc_root(c->native->document), "query")
                                : NULL;
        if (!av_equals(query, old_query)) {
            char error[256];
            char *text = code_source_reconcile_query(c->native, query, error, sizeof error);
            if (!text) {
                snprintf(c->error, sizeof c->error, "%s", error);
                av_doc_free(document);
                return false;
            }
            follow(c, text, false);
            understand(c, text);
            record(c, text);
            c->apply_due = c->time;
            c->graph_draft = true;
            query_source_changed = true;
            free(text);
        }
        av_doc_free(document);
    }
    bool pending = c->source_pending;
    if (pending && !c->error[0] && c->native->document) {
        char error[256];
        game_system_document *candidate = game_system_document_from_value(
            av_doc_root(c->native->document),
            resolve_system_component,
            NULL,
            error,
            sizeof error
        );
        if (!candidate) {
            snprintf(c->error, sizeof c->error, "%s", error);
            c->source_pending = false;
            return false;
        }
        game_system_document_free(candidate);
    }
    bool changed = asset_code_sync_graph(c, e, live);
    if (pending && !c->source_pending && !c->error[0] && c->native->document) {
        char error[256];
        game_system_document *candidate = game_system_document_from_value(
            av_doc_root(c->native->document),
            resolve_system_component,
            NULL,
            error,
            sizeof error
        );
        assert(candidate);
        av_doc *values = av_doc_new();
        const ecs_query_expr *before = game_system_document_query(system);
        const ecs_query_expr *after = game_system_document_query(candidate);
        av_value *left = before
                           ? game_query_save_value(values, before, resolve_system_component, system_component_name, NULL)
                           : NULL;
        av_value *right = after
                            ? game_query_save_value(values, after, resolve_system_component, system_component_name, NULL)
                            : NULL;
        if (!av_equals(left, right)) {
            game_system_document_set_query(system, after);
            changed = true;
        }
        av_doc_free(values);
        game_system_document_free(candidate);
    }
    c->system_revision = game_system_document_revision(system);
    return changed || query_source_changed;
}

bool asset_code_location(const asset_code *c, uint64_t graph, uint64_t node, int *start, int *end)
{
    return code_source_location(c->native, graph, node, 0, start, end);
}

static bool node_at(const asset_code *c, int offset, bool executable, uint64_t *graph_id, uint64_t *node_id)
{
    int best = INT_MAX;
    for (int g = 0; c->candidate && g < acount(c->candidate->graphs); g++) {
        graph *gr = c->candidate->graphs[g];
        for (int n = 0; n < acount(gr->nodes); n++) {
            if (executable && node_behavior_for(gr->nodes[n]) == NODE_BEHAVIOR_PURE) {
                continue;
            }
            int start, end;
            if (asset_code_location(c, gr->graph_id, gr->nodes[n]->id, &start, &end) &&
                offset >= start && offset < end && end - start < best) {
                *graph_id = gr->graph_id;
                *node_id = gr->nodes[n]->id;
                best = end - start;
            }
        }
    }
    return best != INT_MAX;
}

bool asset_code_node_at(const asset_code *c, int offset, uint64_t *graph_id, uint64_t *node_id)
{
    return node_at(c, offset, false, graph_id, node_id);
}

bool asset_code_breakpoint_at(const asset_code *c, int offset, uint64_t *graph_id, uint64_t *node_id)
{
    return node_at(c, offset, true, graph_id, node_id);
}

const code_source *asset_code_source(const asset_code *c)
{
    return c->native;
}

const code_source *asset_code_graph_source(const asset_code *c, const editor *e)
{
    if (editor_has_pending_edits(e) ||
        (c->shared_asset && (c->shared_asset != editor_behavior_asset(e) ||
                             c->shared_revision != editor_edit_revision(e)))) {
        return NULL;
    }
    const code_source *source = c->source_pending || c->error[0] ? c->valid_native
                                                                 : c->native;
    return source && source->asset ? source : NULL;
}

bool asset_code_replace_with_metadata(asset_code *c, const char *before, const char *text, const char *metadata)
{
    if (strcmp(before, c->source->text)) {
        return false;
    }
    follow(c, text, false);
    understand_with_metadata(c, text, metadata);
    record(c, text);
    c->apply_due = c->time;
    c->completing = false;
    return true;
}

bool asset_code_replace(asset_code *c, const char *before, const char *text)
{
    return asset_code_replace_with_metadata(c, before, text, NULL);
}

bool asset_code_definition(const asset_code *c, int offset, int *start)
{
    asset_source_span span;
    if (!(c->native->intelligence
              ? code_intelligence_definition(c->native, offset, &span)
              : asset_source_definition_at(c->source, offset, &span))) {
        return false;
    }
    *start = span.start;
    return true;
}

asset_source_span *asset_code_references(const asset_code *c, int offset)
{
    return c->native->intelligence
             ? code_intelligence_references(c->native, offset)
             : asset_source_references_at(c->source, offset);
}

void asset_code_execution(asset_code *c, uint64_t graph, uint64_t node)
{
    if (!node) {
        code_source_free(c->execution_source);
        c->execution_source = NULL;
    } else if (!c->execution_source && !c->runtime_held) {
        const code_source *base = c->error[0] || c->source_pending ? c->valid_native
                                                                   : c->native;
        if (base && base->asset) {
            c->execution_source = code_source_parse_context(base->text, base, &c->context);
        }
    }
    c->execution_graph = graph;
    c->execution_node = node;
}

bool asset_code_execution_location(const asset_code *c, int *start, int *end)
{
    const code_source *source = c->execution_source;
    if (!source ||
        !code_source_location(source, c->execution_graph, c->execution_node, 0, start, end)) {
        return false;
    }
    const char *current = c->source->text, *previous = source->text;
    int old_length = (int)strlen(previous), new_length = (int)strlen(current),
        prefix = 0;
    while (prefix < old_length && prefix < new_length &&
           previous[prefix] == current[prefix]) {
        prefix++;
    }
    if (*end <= prefix) {
        return true;
    }
    int suffix = 0;
    while (suffix < old_length - prefix && suffix < new_length - prefix &&
           previous[old_length - suffix - 1] == current[new_length - suffix - 1]) {
        suffix++;
    }
    if (*start >= old_length - suffix) {
        *start += new_length - old_length;
        *end += new_length - old_length;
        return true;
    }
    return false;
}

void asset_code_time(asset_code *c, double time)
{
    c->time = time;
}

bool asset_code_pending(const asset_code *c)
{
    return c->source_pending;
}

bool asset_code_graph_blocked(const asset_code *c)
{
    return c->source_pending || (c->error[0] && !c->graph_draft);
}

bool asset_code_runtime_held(const asset_code *c)
{
    return c->runtime_held;
}

void asset_code_hold_runtime(asset_code *c, bool hold)
{
    c->runtime_held = hold;
}

const kit_code_marker *asset_code_markers(const asset_code *c)
{
    return c->markers;
}

/* Catalog changes update reflected pins and identities without authoring a
   source or presentation edit. A clean document keeps a clean baseline. */
static void understand_context(asset_code *c, const char *text)
{
    bool clean = !asset_code_dirty(c);
    understand(c, text);
    if (!clean) {
        return;
    }
    free(c->disk_metadata);
    c->disk_metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    if (c->step < acount(c->history) && !strcmp(c->history[c->step].text, text)) {
        free(c->history[c->step].metadata);
        c->history[c->step].metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    }
}

void asset_code_set_registry(asset_code *c, const game_prototype_registry *registry)
{
    if (c->registry == registry) {
        return;
    }
    c->registry = registry;
    char *text = xstrdup(c->source->text);
    understand_context(c, text);
    free(text);
}

static void refresh_context(asset_code *c)
{
    c->context = (code_source_context){ .libraries = c->libraries,
                                        .library_count = acount(c->libraries),
                                        .events = c->events,
                                        .event_count = acount(c->events),
                                        .references = c->references,
                                        .reference_count = acount(c->references),
                                        .prototypes = c->prototypes,
                                        .prototype_count = acount(c->prototypes),
                                        .owner_prototypes = c->owner_prototypes,
                                        .owner_prototype_count = acount(c->owner_prototypes) };
    if (c->native) {
        c->native->context = c->context;
    }
    if (c->valid_native) {
        c->valid_native->context = c->context;
    }
    if (c->execution_source) {
        c->execution_source->context = c->context;
    }
    if (c->valid) {
        code_source *valid = code_source_parse_context(c->valid, c->valid_native, &c->context);
        code_source_free(c->valid_native);
        c->valid_native = valid;
    }
    bool pending = c->source_pending;
    char *text = xstrdup(c->source->text);
    understand_context(c, text);
    if (!pending) {
        compile_native(c);
    }
    if (c->completing) {
        complete(c);
    }
    free(text);
}

void asset_code_set_catalog(asset_code *c, const behavior_asset *const *assets, int count)
{
    int libraries = 0, events = 0;
    bool same = true;
    uint64_t revision = 0;
    for (int i = 0; i < count; i++) {
        if (assets[i]->domain == BEHAVIOR_DOMAIN_LIBRARY ||
            assets[i]->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            for (int g = 0; g < acount(assets[i]->graphs); g++) {
                revision = revision * 31 +
                           graph_topology_revision(assets[i]->graphs[g]);
            }
        }
        if (assets[i]->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            same &= libraries < acount(c->libraries) &&
                    c->libraries[libraries] == assets[i];
            libraries++;
        } else if (assets[i]->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            same &= events < acount(c->events) && c->events[events] == assets[i];
            events++;
        }
    }
    if (same && libraries == acount(c->libraries) &&
        events == acount(c->events) && revision == c->catalog_revision) {
        return;
    }
    c->catalog_revision = revision;
    asetlen(c->libraries, 0);
    asetlen(c->events, 0);
    for (int i = 0; i < count; i++) {
        if (assets[i]->domain == BEHAVIOR_DOMAIN_LIBRARY) {
            apush(c->libraries, assets[i]);
        } else if (assets[i]->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            apush(c->events, assets[i]);
        }
    }
    refresh_context(c);
}

void asset_code_set_references(asset_code *c, const code_reference *references, int count)
{
    bool same = count == acount(c->references);
    for (int i = 0; same && i < count; i++) {
        same = c->references[i].kind == references[i].kind &&
               !strcmp(c->references[i].name, references[i].name);
    }
    if (same) {
        return;
    }
    code_reference *next = NULL;
    for (int i = 0; i < count; i++) {
        apush(next, ((code_reference){ references[i].kind, xstrdup(references[i].name) }));
    }
    code_reference *previous = c->references;
    c->references = next;
    refresh_context(c);
    for (int i = 0; i < acount(previous); i++) {
        free((char *)previous[i].name);
    }
    afree(previous);
}

void asset_code_set_prototypes(
    asset_code *c,
    const code_prototype *prototypes,
    int count,
    const char *const *owners,
    int owner_count
)
{
    bool same = count == acount(c->prototypes) &&
                owner_count == acount(c->owner_prototypes);
    for (int i = 0; same && i < count; i++) {
        const code_prototype *a = &c->prototypes[i], *b = &prototypes[i];
        same = !strcmp(a->name, b->name) && a->component_count == b->component_count;
        for (int j = 0; same && j < a->component_count; j++) {
            same = !strcmp(a->components[j], b->components[j]);
        }
    }
    for (int i = 0; same && i < owner_count; i++) {
        same = !strcmp(c->owner_prototypes[i], owners[i]);
    }
    if (same) {
        return;
    }
    code_prototype *next = NULL;
    const char **next_owners = NULL;
    for (int i = 0; i < count; i++) {
        const code_prototype *p = &prototypes[i];
        const char **components = xmalloc((size_t)p->component_count * sizeof *components);
        for (int j = 0; j < p->component_count; j++) {
            components[j] = xstrdup(p->components[j]);
        }
        apush(next, ((code_prototype){ xstrdup(p->name), components, p->component_count }));
    }
    for (int i = 0; i < owner_count; i++) {
        apush(next_owners, xstrdup(owners[i]));
    }
    code_prototype *previous = c->prototypes;
    const char **previous_owners = c->owner_prototypes;
    c->prototypes = next;
    c->owner_prototypes = next_owners;
    refresh_context(c);
    free_prototypes(previous, previous_owners);
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

static av_doc *source_document(const asset_code *c, const char *text)
{
    if (!code_source_is_structured(text)) {
        char error[256];
        return av_doc_read(text, error, sizeof error);
    }
    code_source *source = code_source_parse_context(text, NULL, &c->context);
    behavior_asset *asset = code_source_build_behavior(source);
    av_doc *doc = NULL;
    if (asset) {
        doc = av_doc_new();
        av_doc_set_root(
            doc,
            source->document ? av_clone(doc, av_doc_root(source->document))
                             : behavior_asset_to_value(doc, asset)
        );
    }
    behavior_asset_free(asset);
    code_source_free(source);
    return doc;
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
    bool dirty = asset_code_dirty(c);
    char *text = NULL, *metadata = NULL;
    code_source *incoming_source = code_source_parse_context(disk, NULL, &c->context);
    code_source_metadata_load(incoming_source, c->path);
    char *disk_metadata = incoming_source->asset && code_source_is_structured(disk)
                            ? code_source_metadata_export_source(incoming_source)
                            : NULL;
    code_source_free(incoming_source);
    if (dirty) {
        char error[256];
        av_doc *base = source_document(c, c->disk);
        av_doc *incoming = source_document(c, disk);
        av_doc *current = source_document(c, c->source->text);
        av_doc *merged = av_doc_new();
        bool ok = base && incoming && current;
        if (ok) {
            av_value *value = merge(
                merged,
                av_doc_root(base),
                av_doc_root(current),
                av_doc_root(incoming),
                &ok
            );
            if (ok) {
                if (code_source_is_structured(c->source->text)) {
                    behavior_asset *asset = behavior_asset_from_value(value);
                    text = asset
                             ? code_source_reconcile_with_metadata(c->native, asset, &metadata, error, sizeof error)
                             : NULL;
                    behavior_asset_free(asset);
                    if (text &&
                        (c->system_initialized || strstr(c->path, "/systems/"))) {
                        code_source *source = code_source_parse_context(text, NULL, &c->context);
                        code_source_metadata_restore(source, metadata);
                        char *with_query = code_source_reconcile_query(
                            source,
                            av_obj_get(value, "query"),
                            error,
                            sizeof error
                        );
                        free(text);
                        text = with_query;
                        if (text) {
                            code_source *queried = code_source_parse_context(text, source, &c->context);
                            free(metadata);
                            metadata = queried->asset
                                         ? code_source_metadata_export_source(queried)
                                         : NULL;
                            code_source_free(queried);
                        }
                        code_source_free(source);
                    }
                } else {
                    text = asset_source_reconcile(c->source->text, value, error, sizeof error);
                }
            }
        }
        av_doc_free(base);
        av_doc_free(incoming);
        av_doc_free(current);
        av_doc_free(merged);
        if (!text) {
            snprintf(c->error, sizeof c->error, "Source conflicts with a disk or visual edit; the current text is retained");
            free(disk);
            free(metadata);
            free(disk_metadata);
            return;
        }
    } else {
        text = xstrdup(disk);
        metadata = disk_metadata ? xstrdup(disk_metadata) : NULL;
    }
    free(c->disk);
    c->disk = disk;
    free(c->disk_metadata);
    c->disk_metadata = disk_metadata;
    follow(c, text, false);
    understand_with_metadata(c, text, metadata);
    free(metadata);
    valid_overlay(c, true);
    record(c, text);
    free(text);
}

static void complete(asset_code *c)
{
    asset_source_completions_free(&c->completions);
    asetlen(c->items, 0);
    if (c->native->intelligence) {
        code_intelligence_complete(c->native, kit_code_cursor(c->editor), &c->completions);
    } else {
        asset_source_complete(
            c->source,
            kit_code_cursor(c->editor),
            c->path,
            (const char *const *)c->names,
            acount(c->names),
            &c->completions
        );
    }
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
    c->hover = c->native->intelligence
                 ? code_intelligence_hover(c->native, kit_code_hover(c->editor))
                 : asset_source_hover(c->source, kit_code_hover(c->editor));
    kit_code_desc desc = *preferences;
    int length = (int)visible_length(c, c->source->text);
    kit_code_span *shown_spans = NULL;
    for (int i = 0; i < acount(c->spans); i++) {
        kit_code_span span = c->spans[i];
        if (span.start >= length) {
            continue;
        }
        span.end = span.end < length ? span.end : length;
        apush(shown_spans, span);
    }
    desc.spans = shown_spans;
    desc.span_count = acount(shown_spans);
    desc.markers = c->markers;
    desc.marker_count = acount(c->markers);
    kit_code_marker *shown_markers = NULL;
    for (int i = 0; i < acount(c->markers); i++) {
        if (c->markers[i].start < length) {
            kit_code_marker marker = c->markers[i];
            marker.end = marker.end < length ? marker.end : length;
            apush(shown_markers, marker);
        }
    }
    int start, end;
    for (int i = 0; i < acount(c->breakpoints); i++) {
        if (c->breakpoints[i].start < length) {
            apush(shown_markers, c->breakpoints[i]);
        }
    }
    if (c->execution_node && asset_code_execution_location(c, &start, &end) &&
        start < length) {
        int line, column;
        code_source_position(c->native, start, &line, &column);
        apush(
            shown_markers,
            (
                (kit_code_marker){ .line = line,
                                   .severity = KIT_CODE_INFO,
                                   .message = c->error[0] || c->runtime_held ? "Paused in the last valid runtime; this source has unapplied edits"
                                                                             : "Execution paused here",
                                   .start = start,
                                   .end = end }
            )
        );
    }
    desc.markers = shown_markers;
    desc.marker_count = acount(shown_markers);
    desc.completions = c->completing ? c->items : NULL;
    desc.completion_count = c->completing ? acount(c->items) : 0;
    desc.completion_start = c->completions.start;
    desc.hover = c->hover;
    desc.history = true;
    kit_code_result result = kit_code(k, key, c->editor, &desc);
    afree(shown_markers);
    afree(shown_spans);
    if (result.events & KIT_CODE_CHANGED) {
        char *text = edited_text(c);
        understand(c, text);
        record(c, text);
        free(text);
        c->apply_due = c->time + 0.2;
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
        asset_code_replay(c, result.events & KIT_CODE_REDO, live);
    }
    if (result.events & KIT_CODE_CURSOR && c->completing) {
        complete(c);
    }
    return result;
}

bool asset_code_save(asset_code *c, char *error, size_t size)
{
    if (code_source_is_behavior(c->native) && c->source_pending) {
        snprintf(error, size, "Compile the behavior before saving");
        return false;
    }
    if (c->error[0] || (!c->candidate && !c->source->document)) {
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
    if (code_source_is_structured(c->source->text)) {
        char *persisted = code_source_persist(c->native);
        if (!persisted) {
            snprintf(error, size, "Could not preserve source identities");
            return false;
        }
        if (strcmp(persisted, c->source->text)) {
            follow(c, persisted, false);
            understand(c, persisted);
            record(c, persisted);
            c->source_pending = false;
            compile_native(c);
        }
        free(persisted);
        if (c->error[0] || !c->candidate) {
            snprintf(error, size, "%s", c->error[0] ? c->error : "Invalid persisted behavior source");
            return false;
        }
    }
    if (write_file_atomic(c->path, c->source->text)) {
        snprintf(error, size, "Could not write %s", c->path);
        return false;
    }
    if (code_source_is_behavior(c->native) &&
        !code_source_metadata_save(c->native, c->path)) {
        snprintf(error, size, "Could not save graph presentation metadata for %s", c->path);
        return false;
    }
    free(c->disk);
    c->disk = xstrdup(c->source->text);
    free(c->disk_metadata);
    c->disk_metadata = c->metadata ? xstrdup(c->metadata) : NULL;
    c->runtime_held = false;
    valid_overlay(c, true);
    c->error[0] = 0;
    return true;
}
