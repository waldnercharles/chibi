#include "editor/workspace/asset_refactor.h"

#include "assets/code_source.h"
#include "assets/refactor.h"
#include "editor/documents/ui_buffer.h"
#include "editor/workspace/scene_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* One file of a plan: `text` replaces `from` at `to`. Without `from` the
   file is new; without `to` it goes. */
typedef struct
{
    int kind;
    char *name, *renamed;
    char *from, *to;
    char *text, *before;
    int changes;
    bool metadata;
} refactor_file;

struct app_refactor
{
    refactor_file *files;
    int mentions;
};

static char *copy(const char *text)
{
    return text ? xstrdup(text) : NULL;
}

void app_refactor_free(app_refactor *plan)
{
    for (int i = 0; plan && i < acount(plan->files); i++) {
        refactor_file *file = &plan->files[i];
        free(file->name);
        free(file->renamed);
        free(file->from);
        free(file->to);
        free(file->text);
        free(file->before);
    }
    if (plan) {
        afree(plan->files);
        free(plan);
    }
}

int app_refactor_count(const app_refactor *plan)
{
    int count = 0;
    for (int i = 0; i < acount(plan->files); i++) {
        count += !plan->files[i].metadata;
    }
    return count;
}

app_refactor_asset app_refactor_at(const app_refactor *plan, int index)
{
    const refactor_file *file = NULL;
    for (int i = 0; i < acount(plan->files); i++) {
        if (!plan->files[i].metadata && index-- == 0) {
            file = &plan->files[i];
            break;
        }
    }
    assert(file);
    return (app_refactor_asset){ .kind = file->kind,
                                 .name = file->name,
                                 .renamed = file->renamed,
                                 .changes = file->changes };
}

int app_refactor_mentions(const app_refactor *plan)
{
    return plan->mentions;
}

/* ---------- Planning ---------- */

/* Whether Studio holds edits of the file it has not saved. */
static bool unsaved(app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (definition->unsaved &&
            scene_same_file(game_live_definition_path(definition), path)) {
            return true;
        }
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        if (scene->prototypes[i].dirty &&
            scene_same_file(scene->prototypes[i].path, path)) {
            return true;
        }
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene_same_file(scene->tabs[i].path, path) &&
            scene_tabs_dirty(scene, &scene->tabs[i])) {
            return true;
        }
    }
    return scene->scene_dirty && scene_same_file(scene->scene_path, path);
}

/* Refuses a file Studio holds unsaved edits of, naming what to save first. */
static bool saved(app_scene_authoring *scene, const char *path, const char *name, char *error, size_t size)
{
    if (!path || !unsaved(scene, path)) {
        return true;
    }
    snprintf(error, size, "Save %s first", name);
    return false;
}

/* What Studio holds of its open documents, where unsaved() looks. */
static void sync_edits(app_scene_authoring *scene)
{
    sync_behavior_working(scene);
    scene_tabs_sync_current(scene);
}

/* The index, fresh with every edit, and each asset's saved file. */
static char **saved_texts(app_scene_authoring *scene)
{
    sync_edits(scene);
    app_scene_authoring_refresh_asset_index(scene);
    const reference_index *index = scene->asset_index;
    int count = reference_index_asset_count(index);
    char **texts = xcalloc((size_t)count + 1, sizeof *texts);
    for (int i = 0; i < count; i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        texts[i] = asset->path && !asset->unresolved ? read_scene_file(asset->path)
                                                     : NULL;
    }
    return texts;
}

static char *metadata_path(const char *path)
{
    size_t size = strlen(path) + sizeof ".meta";
    char *out = xmalloc(size);
    snprintf(out, size, "%s.meta", path);
    return out;
}

static char **saved_metadata(app_scene_authoring *scene, char *const *texts)
{
    int count = reference_index_asset_count(scene->asset_index);
    char **metadata = xcalloc((size_t)count + 1, sizeof *metadata);
    for (int i = 0; i < count; i++) {
        const reference_asset *asset = reference_index_asset_at(scene->asset_index, i);
        if (!texts[i] || !asset->path || !code_source_is_structured(texts[i])) {
            continue;
        }
        for (int j = 0; j < acount(scene->tabs); j++) {
            const scene_document_tab *tab = &scene->tabs[j];
            if (!tab->source_state || !scene_same_file(tab->path, asset->path)) {
                continue;
            }
            const char *text = av_get_str(av_obj_get(tab->source_state, "text"));
            if (text && !strcmp(text, texts[i])) {
                metadata[i] = copy(av_get_str(av_obj_get(tab->source_state, "metadata")));
                break;
            }
        }
        if (!metadata[i]) {
            char *path = metadata_path(asset->path);
            char *json = read_scene_file(path);
            free(path);
            if (json && code_source_metadata_matches(json, texts[i])) {
                metadata[i] = json;
            } else {
                free(json);
            }
        }
    }
    return metadata;
}

/* A sibling of `path` named `name`, with its extension. */
static char *renamed_path(const char *path, const char *name)
{
    const char *slash = strrchr(path, '/');
    const char *extension = strrchr(path, '.');
    int folder = slash ? (int)(slash - path + 1) : 0;
    size_t size = (size_t)folder + strlen(name) + strlen(extension) + 1;
    char *out = xmalloc(size);
    snprintf(out, size, "%.*s%s%s", folder, path, name, extension);
    return out;
}

static int changes_in(const refactor_edit *edit)
{
    int count = 0;
    for (int i = 0; i < acount(edit->references); i++) {
        count += edit->references[i]->role != REFERENCE_DEFINE;
    }
    return count;
}

/* A ui asset's styles, which follow its markup when a plan renames or
   deletes it. */
static void add_styles(app_refactor *out, const reference_asset *asset, const refactor_edit *edit)
{
    char *from = ui_buffer_part_file(asset->path, UI_PART_RCSS);
    char *text = read_scene_file(from);
    if (!text) {
        free(from);
        return;
    }
    refactor_file file = {
        .kind = (int)asset->kind,
        .name = xstrdup(content_basename(from)),
        .from = from,
        .to = edit->name ? renamed_path(from, edit->name) : NULL,
        .text = edit->name ? xstrdup(text) : NULL,
        .before = text,
    };
    file.renamed = file.to ? xstrdup(content_basename(file.to)) : NULL;
    apush(out->files, file);
}

/* Metadata is a transaction companion, not an additional authored asset. */
static void add_metadata(app_refactor *out, const refactor_file *source, const refactor_edit *edit)
{
    if (!code_source_is_structured(source->before)) {
        return;
    }
    char *from = metadata_path(source->from);
    char *before = read_scene_file(from);
    char *text = edit->text && edit->metadata
                   ? code_source_metadata_bind(edit->metadata, edit->text)
                   : NULL;
    if (!before && !text) {
        free(from);
        return;
    }
    refactor_file file = {
        .kind = source->kind,
        .name = xstrdup(source->name),
        .renamed = copy(source->renamed),
        .from = before ? from : NULL,
        .to = text ? metadata_path(source->to) : NULL,
        .text = text,
        .before = before,
        .metadata = true,
    };
    if (!before) {
        free(from);
    }
    apush(out->files, file);
}

static app_refactor *files_of(const reference_index *index, char *const *texts, const refactor_plan *plan)
{
    app_refactor *out = xcalloc(1, sizeof *out);
    out->mentions = acount(plan->mentions);
    for (int i = 0; i < acount(plan->edits); i++) {
        const refactor_edit *edit = &plan->edits[i];
        const reference_asset *asset = reference_index_asset_at(index, edit->asset);
        refactor_file file = {
            .kind = (int)asset->kind,
            .name = xstrdup(asset->name),
            .renamed = copy(edit->name),
            .from = xstrdup(asset->path),
            .to = !edit->text ? NULL
                : edit->name  ? renamed_path(asset->path, edit->name)
                              : xstrdup(asset->path),
            .text = copy(edit->text),
            .before = xstrdup(texts[edit->asset]),
            .changes = changes_in(edit),
        };
        apush(out->files, file);
        add_metadata(out, &file, edit);
        if (asset->kind == REFERENCE_ASSET_UI && (edit->name || !edit->text)) {
            add_styles(out, asset, edit);
        }
    }
    return out;
}

static int plan_symbol(
    app_scene_authoring *scene,
    reference_symbol symbol,
    const char *name,
    app_refactor **out,
    char *error,
    size_t size
)
{
    *out = NULL;
    if (runtime_session(scene)) {
        snprintf(error, size, "Stop the scene first");
        return -1;
    }
    char **texts = saved_texts(scene);
    const reference_index *index = scene->asset_index;
    char **metadata = saved_metadata(scene, texts);
    refactor_project project = { .index = index,
                                 .texts = (const char *const *)texts,
                                 .metadata = (const char *const *)metadata,
                                 .registry = &scene->registry };
    refactor_plan plan;
    int result = name
                   ? refactor_rename(&project, symbol, name, &plan, error, size)
                   : refactor_delete(&project, symbol, true, &plan, error, size);
    for (int i = 0; result == 0 && i < acount(plan.edits); i++) {
        const reference_asset
            *asset = reference_index_asset_at(index, plan.edits[i].asset);
        result = saved(scene, asset->path, asset->name, error, size) ? 0 : -1;
    }
    if (result == 0) {
        *out = files_of(index, texts, &plan);
    }
    refactor_plan_free(&plan);
    for (int i = 0; i < reference_index_asset_count(index); i++) {
        free(texts[i]);
        free(metadata[i]);
    }
    free(texts);
    free(metadata);
    return result;
}

int app_scene_authoring_plan_rename(
    app_scene_authoring *scene,
    reference_symbol symbol,
    const char *name,
    app_refactor **plan,
    char *error,
    size_t error_size
)
{
    return plan_symbol(scene, symbol, name, plan, error, error_size);
}

int app_scene_authoring_plan_delete(
    app_scene_authoring *scene,
    reference_symbol symbol,
    app_refactor **plan,
    char *error,
    size_t error_size
)
{
    return plan_symbol(scene, symbol, NULL, plan, error, error_size);
}

bool app_scene_authoring_refactor_reaches(app_scene_authoring *scene, reference_symbol symbol)
{
    app_scene_authoring_refresh_asset_index(scene);
    /* Calls name a parameter by declaration or by name. */
    reference_symbol reach = symbol.kind == REFERENCE_SYMBOL_PARAMETER
                               ? (reference_symbol){ .kind = REFERENCE_SYMBOL_GRAPH,
                                                     .asset = symbol.asset,
                                                     .id = symbol.graph_id }
                               : symbol;
    const reference **uses = NULL;
    refactor_blockers(scene->asset_index, reach, &uses);
    bool reaches = false;
    for (int i = 0; i < acount(uses); i++) {
        reaches |= uses[i]->source != symbol.asset;
    }
    afree(uses);
    return reaches;
}

/* ---------- Applying ---------- */

static bool moved(const refactor_file *file)
{
    return file->from && (!file->to || strcmp(file->from, file->to));
}

/* Every file still holds what the plan was made from, and no new one would
   replace another. */
static bool current(const app_refactor *plan, char *error, size_t size)
{
    for (int i = 0; i < acount(plan->files); i++) {
        const refactor_file *file = &plan->files[i];
        char *now = file->from ? read_scene_file(file->from) : NULL;
        bool same = !file->from || (now && !strcmp(now, file->before));
        free(now);
        struct stat info;
        bool taken = file->to && (!file->from || strcmp(file->from, file->to)) &&
                     stat(file->to, &info) == 0;
        if (!same || taken) {
            snprintf(
                error,
                size,
                "%s %s",
                file->renamed && taken ? file->renamed : file->name,
                taken ? "exists" : "changed on disk"
            );
            return false;
        }
    }
    return true;
}

static int write_file(const refactor_file *file)
{
    if (file->to && write_file_atomic(file->to, file->text)) {
        return -1;
    }
    return moved(file) ? remove_content_file(file->from) : 0;
}

static void restore_file(const refactor_file *file)
{
    if (file->to && (!file->from || strcmp(file->from, file->to))) {
        remove_content_file(file->to);
    }
    if (file->from) {
        write_file_atomic(file->from, file->before);
    }
}

/* The plan that puts every file back. */
static app_refactor *reversed(const app_refactor *plan)
{
    app_refactor *undo = xcalloc(1, sizeof *undo);
    for (int i = acount(plan->files) - 1; i >= 0; i--) {
        const refactor_file *file = &plan->files[i];
        refactor_file back = { .kind = file->kind,
                               .name = xstrdup(file->renamed ? file->renamed : file->name),
                               .renamed = file->renamed ? xstrdup(file->name) : NULL,
                               .from = copy(file->to),
                               .to = copy(file->from),
                               .text = copy(file->before),
                               .before = copy(file->text),
                               .changes = file->changes,
                               .metadata = file->metadata };
        apush(undo->files, back);
    }
    return undo;
}

int app_scene_authoring_apply_refactor(
    app_scene_authoring *scene,
    const app_refactor *plan,
    app_refactor **undo,
    char *error,
    size_t error_size
)
{
    *undo = NULL;
    if (runtime_session(scene)) {
        snprintf(error, error_size, "Stop the scene first");
        return -1;
    }
    /* Edits made since the plan, as before an Undo, are not reloaded over. */
    sync_edits(scene);
    for (int i = 0; i < acount(plan->files); i++) {
        if (!saved(scene, plan->files[i].from, plan->files[i].name, error, error_size)) {
            return -1;
        }
    }
    if (!current(plan, error, error_size)) {
        return -1;
    }
    int count = acount(plan->files);
    int written = 0;
    while (written < count && write_file(&plan->files[written]) == 0) {
        written++;
    }
    if (written < count) {
        snprintf(error, error_size, "Could not write %s", plan->files[written].name);
        for (int i = written; i >= 0; i--) {
            restore_file(&plan->files[i]);
        }
        return -1;
    }
    scene_move *moves = NULL;
    for (int i = 0; i < count; i++) {
        if (!plan->files[i].metadata) {
            apush(moves, ((scene_move){ plan->files[i].from, plan->files[i].to }));
        }
    }
    scene_disk_follow(scene, moves, acount(moves));
    afree(moves);
    *undo = reversed(plan);
    return 0;
}
