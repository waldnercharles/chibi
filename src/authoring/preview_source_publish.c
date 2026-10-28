#include "authoring/preview_source_internal.h"
#include "authoring/source_asset.h"
#include "authoring/source_presentation.h"
#include "runtime/engine.h"
#include "runtime/world.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void message(char *error, size_t size, const char *text)
{
    if (error && size) {
        snprintf(error, size, "%s", text);
    }
}
static const authoring_source *source_at_identity(const authoring_sources *sources, const char *identity)
{
    if (!sources) {
        return NULL;
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->analysis->identity &&
            !strcmp(source->analysis->identity, identity)) {
            return source;
        }
    }
    return NULL;
}
static uint64_t declaration_abi(const authoring_sources *sources)
{
    const script_catalog *catalog = authoring_sources_catalog(sources);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < catalog->capability_count; i++) {
        hash = (hash ^ catalog->capabilities[i].id) * UINT64_C(1099511628211);
        hash = (hash ^
                script_catalog_capability_abi(catalog, &catalog->capabilities[i])) *
               UINT64_C(1099511628211);
    }
    return hash;
}
static bool *changed_sources(const preview_source_state *state)
{
    int count = authoring_sources_count(state->working);
    bool *changed = xcalloc((size_t)count, sizeof *changed);
    bool declarations = !state->applied || declaration_abi(state->working) !=
                                               declaration_abi(state->applied);
    for (int i = 0; i < count; i++) {
        const authoring_source *source = authoring_sources_at(state->working, i);
        const authoring_source
            *old = source_at_identity(state->applied, source->analysis->identity);
        changed[i] = declarations || !old ||
                     strcmp(source->analysis->text, old->analysis->text);
    }
    bool progress;
    do {
        progress = false;
        for (int i = 0; i < count; i++) {
            if (changed[i]) {
                continue;
            }
            const execution_module *module = authoring_sources_at(state->working, i)
                                                 ->module;
            for (int j = 0; !changed[i] && j < execution_module_import_count(module);
                 j++) {
                const execution_module *dependency = execution_module_import_at(module, j);
                for (int k = 0; k < count; k++) {
                    if (changed[k] && authoring_sources_at(state->working, k)->module ==
                                          dependency) {
                        changed[i] = true;
                        progress = true;
                        break;
                    }
                }
            }
        }
    } while (progress);
    return changed;
}

typedef struct
{
    preview_definition *current, *prepared;
} projection_stage;
struct preview_source_staging
{
    preview_world *world;
    authoring_sources *sources;
    source_staging *runtime;
    projection_stage *projections;
    preview_definition **definitions, **removed;
    preview_field_relink *fields;
};
static preview_definition *source_projection(preview_world *world, const authoring_source *source)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *definition = world->definitions[i];
        if (!definition->working_presentation || !definition->path) {
            continue;
        }
        const authoring_source *old = preview_source_at_path(
            world->source_state->applied,
            definition->path
        );
        if ((old && !strcmp(old->analysis->identity, source->analysis->identity)) ||
            (!old && !strcmp(definition->path, source->path))) {
            return definition;
        }
    }
    return NULL;
}
static void stage_free(preview_source_staging *stage)
{
    for (int i = 0; i < acount(stage->projections); i++) {
        preview_definition_free(stage->projections[i].prepared);
    }
    afree(stage->projections);
    afree(stage->definitions);
    afree(stage->removed);
    preview_fields_relink_free(stage->fields);
    authoring_sources_free(stage->sources);
    free(stage);
}
/* Field graphs call source libraries, so a changed or removed library
 * relinks the fields that call it. */
static bool relink_fields(preview_source_staging *stage, const bool *changed, char *error, size_t size)
{
    const authoring_sources *applied = stage->world->source_state->applied;
    const char **names = NULL;
    for (int i = 0; i < authoring_sources_count(stage->sources); i++) {
        const script_analysis *a = authoring_sources_at(stage->sources, i)->analysis;
        const authoring_source *old = source_at_identity(applied, a->identity);
        if (changed[i] && a->kind == SCRIPT_PROGRAM_LIBRARY) {
            apush(names, a->name);
        }
        if (changed[i] && old && old->analysis->kind == SCRIPT_PROGRAM_LIBRARY) {
            apush(names, old->analysis->name);
        }
    }
    for (int i = 0; applied && i < authoring_sources_count(applied); i++) {
        const script_analysis *a = authoring_sources_at(applied, i)->analysis;
        if (a->kind == SCRIPT_PROGRAM_LIBRARY &&
            !source_at_identity(stage->sources, a->identity)) {
            apush(names, a->name);
        }
    }
    bool relinked = !acount(names) || preview_fields_relink(
                                          stage->world,
                                          stage->sources,
                                          names,
                                          acount(names),
                                          &stage->fields,
                                          error,
                                          size
                                      );
    afree(names);
    return relinked;
}
static bool install_relationships(
    preview_world *world,
    const authoring_sources *sources,
    char *error,
    size_t size
)
{
    ecs_relationship_desc **relationships = NULL;
    ecs_world *ecs = world->runtime->ecs;
    bool installed = false;
    const source_catalog_definition *definitions = authoring_sources_definitions(sources);
    for (int i = 0; i < authoring_sources_definition_count(sources); i++) {
        if (!definitions[i].relationship) {
            continue;
        }
        ecs_relationship_desc
            *desc = authoring_relationship_source_read(definitions[i].text, error, size);
        if (!desc) {
            goto done;
        }
        apush(relationships, desc);
        const ecs_relationship_desc *found = ecs_world_relationship(ecs, desc->id);
        if (ecs_world_component(ecs, desc->id) ||
            (found && (strcmp(found->name, desc->name) || !found->key ||
                       strcmp(found->key, desc->key) || found->traits != desc->traits ||
                       found->on_delete_target != desc->on_delete_target))) {
            if (error && size) {
                snprintf(error, size, "Relationship %s schema differs from the world", desc->name);
            }
            goto done;
        }
    }
    /* Query plans validate their required relationships during publication,
     * before a preview has loaded any scenes. Use the prepared snapshot so
     * unsaved declarations and mirrored worlds install the same schemas. */
    for (int i = 0; i < acount(relationships); i++) {
        const ecs_relationship_desc *desc = relationships[i];
        if (!ecs_world_relationship(ecs, desc->id) &&
            ecs_world_register_relationship(ecs, desc, NULL)) {
            if (error && size) {
                snprintf(error, size, "Relationship %s could not be registered", desc->name);
            }
            goto done;
        }
    }
    installed = true;
done:
    for (int i = 0; i < acount(relationships); i++) {
        authoring_relationship_source_free(relationships[i]);
    }
    afree(relationships);
    return installed;
}
preview_source_staging *preview_sources_stage(
    preview_world *world,
    const char *restart_path,
    char *error,
    size_t size
)
{
    assert(world);
    preview_source_state *state = world->source_state;
    if (!state || !state->working || !authoring_sources_valid(state->working)) {
        message(error, size, "Only a complete valid source collection can be published");
        return NULL;
    }
    if (state->staging) {
        message(error, size, "Source publication is already staged");
        return NULL;
    }
    if (restart_path && !preview_source_at_path(state->working, restart_path)) {
        message(error, size, "Restart source is absent from the candidate collection");
        return NULL;
    }
    for (int i = 0; i < authoring_sources_count(state->working); i++) {
        const authoring_source *source = authoring_sources_at(state->working, i);
        preview_definition
            *definition = preview_find_definition(world, source->analysis->name);
        if (definition && !definition->working_presentation) {
            message(error, size, "Source declaration conflicts with an existing declarative asset");
            return NULL;
        }
    }
    preview_source_staging *stage = xcalloc(1, sizeof *stage);
    stage->world = world;
    stage->sources = authoring_sources_retain(state->working);
    for (int i = 0; i < acount(world->definitions); i++) {
        if (!world->definitions[i]->working_presentation) {
            apush(stage->definitions, world->definitions[i]);
        }
    }
    for (int i = 0; i < authoring_sources_count(stage->sources); i++) {
        const authoring_source *source = authoring_sources_at(stage->sources, i);
        preview_definition *current = source_projection(world, source);
        authoring_source_presentation *presentation = authoring_source_presentation_new(
            source->analysis,
            authoring_sources_catalog(stage->sources),
            current ? current->working_presentation : NULL
        );
        if (!presentation) {
            message(error, size, "Source presentation could not be prepared");
            stage_free(stage);
            return NULL;
        }
        preview_definition *prepared = preview_projection_new(source->analysis->name, presentation);
        authoring_source_presentation_free(presentation);
        prepared->path = xstrdup(source->path);
        apush(stage->projections, ((projection_stage){ current, prepared }));
        apush(stage->definitions, current ? current : prepared);
    }
    for (int i = 0; i < acount(world->definitions); i++) {
        preview_definition *definition = world->definitions[i];
        if (!definition->working_presentation) {
            continue;
        }
        bool retained = false;
        for (int j = 0; j < acount(stage->projections); j++) {
            retained |= stage->projections[j].current == definition;
        }
        if (!retained) {
            apush(stage->removed, definition);
        }
    }
    bool *changed = changed_sources(state);
    if (!relink_fields(stage, changed, error, size)) {
        free(changed);
        stage_free(stage);
        return NULL;
    }
    source_publication *entries = NULL;
    for (int i = 0; i < authoring_sources_count(stage->sources); i++) {
        const authoring_source *source = authoring_sources_at(stage->sources, i);
        bool restart = restart_path && !strcmp(restart_path, source->path);
        if (!changed[i] && !restart) {
            continue;
        }
        const authoring_source
            *old = source_at_identity(state->applied, source->analysis->identity);
        apush(
            entries,
            ((source_publication){
                .name = source->analysis->name,
                .previous_name = old ? old->analysis->name : NULL,
                .module = source->module,
                .restart = restart,
            })
        );
    }
    free(changed);
    for (int i = 0; state->applied && i < authoring_sources_count(state->applied); i++) {
        const authoring_source *old = authoring_sources_at(state->applied, i);
        if (!source_at_identity(stage->sources, old->analysis->identity)) {
            apush(entries, ((source_publication){ .previous_name = old->analysis->name }));
        }
    }
    if (!install_relationships(world, stage->sources, error, size)) {
        afree(entries);
        stage_free(stage);
        return NULL;
    }
    stage->runtime = source_stage(world->runtime->sources, entries, acount(entries), NULL, error, size);
    afree(entries);
    if (!stage->runtime) {
        stage_free(stage);
        return NULL;
    }
    state->staging = stage;
    return stage;
}
void preview_sources_discard(preview_source_staging *stage)
{
    assert(stage && stage->world->source_state->staging == stage);
    source_discard(stage->runtime);
    stage->world->source_state->staging = NULL;
    stage_free(stage);
}
void preview_sources_commit(preview_source_staging *stage)
{
    assert(stage && stage->world->source_state->staging == stage);
    preview_world *world = stage->world;
    preview_source_state *state = world->source_state;
    assert(state->working == stage->sources);
    source_commit(stage->runtime);
    const input_action *actions = authoring_sources_input(stage->sources);
    if (world->runtime->engine) {
        engine_set_input_actions(world->runtime->engine, actions, acount(actions));
    }
    for (int i = 0; i < acount(stage->projections); i++) {
        projection_stage *row = &stage->projections[i];
        preview_definition *definition = row->current ? row->current : row->prepared;
        if (row->current) {
            preview_definition *prepared = row->prepared;
            free(definition->name);
            free(definition->path);
            authoring_source_presentation_free(definition->working_presentation);
            authoring_source_presentation_free(definition->applied_presentation);
            definition->name = prepared->name;
            definition->path = prepared->path;
            definition->working_presentation = prepared->working_presentation;
            definition->applied_presentation = prepared->applied_presentation;
            definition->working = prepared->working;
            definition->applied = prepared->applied;
            definition->dirty = 0;
            prepared->name = prepared->path = NULL;
            prepared->working_presentation = prepared->applied_presentation = NULL;
            prepared->working = prepared->applied = NULL;
        } else {
            row->prepared = NULL;
        }
        definition->world = world;
        definition->registered = 1;
    }
    for (int i = 0; i < acount(stage->removed); i++) {
        stage->removed[i]->registered = 0;
        stage->removed[i]->world = NULL;
        preview_definition_free(stage->removed[i]);
    }
    afree(world->definitions);
    world->definitions = stage->definitions;
    stage->definitions = NULL;
    preview_fields_relink_commit(world, stage->fields);
    stage->fields = NULL;
    if (state->applied != state->working) {
        authoring_sources_free(state->applied);
    }
    state->applied = state->working;
    state->staging = NULL;
    stage_free(stage);
}
int preview_sources_publish(preview_world *world, const char *restart_path, char *error, size_t size)
{
    preview_source_staging *stage = preview_sources_stage(world, restart_path, error, size);
    if (!stage) {
        return -1;
    }
    preview_sources_commit(stage);
    return 0;
}
