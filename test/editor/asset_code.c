#include "editor/documents/asset_code.h"

#include "assets/code_catalog.h"
#include "assets/code_source.h"
#include "assets/code_source_intelligence.h"
#include "assets/system_document.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/reactive.h"
#include "runtime/behaviors/live.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void source_only_repair_preserves_open_graph()
{
    const char
        *invalid = "behavior Repair { state float amount = ; on Tick() {} }";
    const char *valid = "// Repaired file\nbehavior Repair { state float "
                        "amount = 2; on Tick() { amount += event.dt; } }";
    code_source *baseline = code_source_parse("behavior Existing { state float count = 7; on Tick() { count += event.dt; } }");
    behavior_asset *working = code_source_build_behavior(baseline);
    assert(working);
    editor *e = editor_new(working);
    char path[] = "/tmp/chibi-source-repair-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs(invalid, file);
    assert(!fclose(file));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && asset_code_error(code)[0]);
    assert(!asset_code_validate(code, e));
    assert(asset_code_replace(code, invalid, valid));
    asset_code_time(code, 1);
    assert(asset_code_validate(code, e));
    assert(editor_behavior_asset(e) == working);
    assert(!strcmp(editor_behavior_asset(e)->name, "Existing"));
    assert(asset_code_save(code, error, sizeof error));
    char *saved = text_file_read(path);
    assert(saved && strstr(saved, "Repaired file") && strstr(saved, "behavior Repair"));
    free(saved);
    asset_code_free(code);
    editor_free(e);
    code_source_free(baseline);
    assert(!unlink(path));
}

static void system_view_preserves_source()
{
    const char *text = "// Authored execution flow\nsystem View { state float "
                       "elapsed = 0; on Tick() { elapsed += event.dt; } }";
    char path[] = "/tmp/chibi-system-view-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs(text, file);
    assert(!fclose(file));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_error(code)[0]);
    editor *e = editor_new(
        code_source_build_behavior((code_source *)asset_code_source(code))
    );
    assert(!e->active_graph->dataflow);
    editor_set_ui_context(e, EDITOR_UI_CONTEXT_SYSTEM);
    assert(!e->active_graph->dataflow);
    asset_code_sync_graph(code, e, false);
    bool warning = false;
    const code_source *source = asset_code_source(code);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        warning |= source->diagnostics[i].severity == CMPL_WARNING;
    }
    assert(!warning && !asset_code_dirty(code));
    assert(!strcmp(asset_code_text(code), text));
    const char
        *edited = "// Authored execution flow\nsystem View { state float "
                  "elapsed = 0; on Tick() { elapsed += event.dt * 2; } }";
    assert(asset_code_replace(code, text, edited));
    asset_code_time(code, 1);
    assert(asset_code_sync_graph(code, e, false));
    assert(e->arrange_pending && !e->active_graph->dataflow);
    /* A signature refresh must not republish pristine disk layout over the
       automatically arranged working graph. */
    assert(asset_code_save(code, error, sizeof error));
    warning = false;
    source = asset_code_source(code);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        warning |= source->diagnostics[i].severity == CMPL_WARNING;
    }
    assert(!warning);
    code_source *library = code_source_parse("library Helpers { pure function One() -> (Value: int) { result Value: int = 1; } }");
    const behavior_asset *catalog[] = { library->asset };
    behavior_asset *working = e->asset;
    asset_code_set_catalog(code, catalog, 1);
    assert(asset_code_pending(code));
    assert(!asset_code_sync_graph(code, e, false));
    assert(e->asset == working && !asset_code_dirty(code));
    asset_code_free(code);
    code_source_free(library);
    editor_free(e);
    assert(!unlink(path));
}

static node *imported_call(const behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            node *n = asset->graphs[i]->nodes[j];
            if (n->reference_asset && !strcmp(n->reference_asset, "Shared")) {
                return n;
            }
        }
    }
    return NULL;
}

static void compatible_catalog_publication()
{
    code_source *old = code_source_parse("library Shared { pure function Seven() -> (Result: int) { result Result: int = 7; } }");
    code_source *next = code_source_parse_previous("library Shared { pure function Seven(Extra: int = 4) -> (Result: int) { result Result: int = 7; } }", old);
    assert(old->asset && next->asset);
    const char *text = "behavior Consumer { state count: int = 1; value chosen "
                       "= Shared.Seven(); }";
    char path[] = "/tmp/chibi-source-compatible-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    asset_code *code = asset_code_open(path, error, sizeof error);
    const behavior_asset *catalog[] = { old->asset };
    asset_code_set_catalog(code, catalog, 1);
    av_value *checkpoint = asset_code_capture(code);
    asset_code *restored = asset_code_restore(checkpoint, NULL);
    assert(restored && !asset_code_dirty(restored));
    asset_code_free(code);
    code = restored;
    asset_code_set_catalog(code, catalog, 1);
    av_value_free(checkpoint);
    editor *e = editor_new(behavior_asset_clone(asset_code_source(code)->asset));
    editor_set_asset_catalog(e, catalog, 1);
    e->arrange_pending = false;
    node *call = imported_call(e->asset);
    assert(call);
    uint64_t id = call->id;
    call->x = 137;
    call->y = 251;
    call->placed = true;
    assert(!asset_code_sync_graph(code, e, true));
    catalog[0] = next->asset;
    editor_set_asset_catalog(e, catalog, 1);
    asset_code_set_catalog(code, catalog, 1);
    assert(asset_code_sync_graph(code, e, true));
    call = imported_call(e->asset);
    assert(call && call->id == id && call->input_count == 1);
    assert(call->input_pins[0].default_value.i == 4);
    assert(call->x == 137 && call->y == 251 && call->placed);
    assert(!asset_code_dirty(code) && !strcmp(asset_code_text(code), text));
    assert(!asset_code_error(code)[0]);
    /* A graph edit waiting for source synchronization must survive the next
       compatible context publication, as one shared source history change. */
    behavior_variable *count = behavior_asset_find_variable_by_name(e->asset, "count");
    pin_value value = { .type = PIN_I32, .i = 9 };
    assert(!behavior_asset_set_variable_default(e->asset, count->id, &value));
    e->edit_pending = true;
    code_source *third = code_source_parse_previous(
        "library Shared { pure function Seven(Extra: int = 4, More: int = 8) "
        "-> (Result: int) { result Result: int = 7; } }",
        next
    );
    assert(third->asset);
    catalog[0] = third->asset;
    editor_set_asset_catalog(e, catalog, 1);
    asset_code_set_catalog(code, catalog, 1);
    assert(asset_code_sync_graph(code, e, true));
    call = imported_call(e->asset);
    assert(call && call->id == id && call->input_count == 2);
    assert(call->x == 137 && call->y == 251 && call->placed);
    assert(
        behavior_asset_find_variable_by_name(e->asset, "count")->default_value.i == 9
    );
    assert(asset_code_dirty(code) && !asset_code_error(code)[0]);
    assert(asset_code_can_replay(code, false));
    asset_code_free(code);
    editor_free(e);
    code_source_free(third);
    code_source_free(next);
    code_source_free(old);
    assert(!unlink(path));
}

static void inherited_catalog_defaults()
{
    for (int authored = 0; authored < 2; authored++) {
        code_source *old = code_source_parse("library Shared { pure function Seven(Extra: int = 4) -> (Result: int) { result Result: int = 7; } }");
        code_source *next = code_source_parse_previous("library Shared { pure function Seven(Extra: int = 6) -> (Result: int) { result Result: int = 7; } }", old);
        const char *text = authored
                             ? "behavior Consumer { state count: int = 1; "
                               "value chosen = Shared.Seven(Extra: 4); }"
                             : "behavior Consumer { state count: int = 1; "
                               "value chosen = Shared.Seven(); }";
        char path[] = "/tmp/chibi-source-defaults-XXXXXX", error[256];
        int fd = mkstemp(path);
        assert(fd >= 0);
        FILE *file = fdopen(fd, "w");
        assert(file && fputs(text, file) >= 0 && !fclose(file));
        asset_code *code = asset_code_open(path, error, sizeof error);
        const behavior_asset *catalog[] = { old->asset };
        asset_code_set_catalog(code, catalog, 1);
        editor *e = editor_new(behavior_asset_clone(asset_code_source(code)->asset));
        editor_set_asset_catalog(e, catalog, 1);
        e->arrange_pending = false;
        node *call = imported_call(e->asset);
        assert(call && call->input_count == 1);
        uint64_t id = call->id;
        call->x = 137;
        call->y = 251;
        call->placed = true;
        assert(!asset_code_sync_graph(code, e, true));
        catalog[0] = next->asset;
        editor_set_asset_catalog(e, catalog, 1);
        asset_code_set_catalog(code, catalog, 1);
        assert(asset_code_sync_graph(code, e, true) == !authored);
        call = imported_call(e->asset);
        assert(
            call && call->id == id &&
            call->input_pins[0].default_value.i == (authored ? 4 : 6)
        );
        assert(call->x == 137 && call->y == 251 && call->placed);
        assert(!asset_code_dirty(code) && !strcmp(asset_code_text(code), text));
        behavior_variable *count = behavior_asset_find_variable_by_name(e->asset, "count");
        pin_value value = { .type = PIN_I32, .i = 9 };
        assert(!behavior_asset_set_variable_default(e->asset, count->id, &value));
        e->edit_pending = true;
        code_source *third = code_source_parse_previous(
            "library Shared { pure function Seven(Extra: int = 8) -> (Result: "
            "int) { result Result: int = 7; } }",
            next
        );
        catalog[0] = third->asset;
        editor_set_asset_catalog(e, catalog, 1);
        asset_code_set_catalog(code, catalog, 1);
        assert(asset_code_sync_graph(code, e, true) == !authored);
        call = imported_call(e->asset);
        assert(
            call && call->id == id &&
            call->input_pins[0].default_value.i == (authored ? 4 : 8)
        );
        assert(call->x == 137 && call->y == 251 && call->placed);
        assert(
            behavior_asset_find_variable_by_name(e->asset, "count")
                ->default_value.i == 9
        );
        assert(asset_code_dirty(code) && !asset_code_error(code)[0]);
        call->input_pins[0].default_value.i = 13;
        e->edit_pending = true;
        code_source *fourth = code_source_parse_previous(
            "library Shared { pure function Seven(Extra: int = 10) -> (Result: "
            "int) { result Result: int = 7; } }",
            third
        );
        catalog[0] = fourth->asset;
        editor_set_asset_catalog(e, catalog, 1);
        asset_code_set_catalog(code, catalog, 1);
        assert(!asset_code_sync_graph(code, e, true));
        call = imported_call(e->asset);
        assert(call && call->input_pins[0].default_value.i == 13);
        assert(!asset_code_error(code)[0]);
        assert(
            imported_call(asset_code_source(code)->asset)
                ->input_pins[0]
                .default_value.i == 13
        );
        asset_code_free(code);
        editor_free(e);
        code_source_free(fourth);
        code_source_free(third);
        code_source_free(next);
        code_source_free(old);
        assert(!unlink(path));
    }
}

static void changed_catalog_signature()
{
    code_source *library = code_source_parse("library First { pure function Same() -> (Result: int) { result Result: int = 1; } }");
    behavior_asset *asset = code_source_build_behavior(library);
    assert(asset);
    char path[] = "/tmp/chibi-source-catalog-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("behavior Consumer { value one = First.Same(); }", file);
    assert(!fclose(file));
    char error[256];
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code);
    const behavior_asset *catalog[] = { asset };
    asset_code_set_catalog(code, catalog, 1);
    assert(!asset_code_error(code)[0]);
    behavior_asset *consumer = behavior_asset_clone(asset_code_source(code)->asset);
    assert(consumer);
    editor *e = editor_new(consumer);
    editor_set_asset_catalog(e, catalog, 1);
    asset_code_sync_graph(code, e, true);
    uint64_t graph_id, node_id;
    int at = (int)(strstr(kit_code_text(asset_code_editor(code), NULL), "First.Same") -
                   kit_code_text(asset_code_editor(code), NULL));
    assert(asset_code_node_at(code, at, &graph_id, &node_id));
    asset_code_execution(code, graph_id, node_id);
    int start, end;
    assert(asset_code_execution_location(code, &start, &end));
    char *before = strdup(asset_code_text(code));
    char *invalid = malloc(strlen(before) + 4);
    assert(invalid);
    snprintf(invalid, strlen(before) + 4, "??\n%s", before);
    assert(asset_code_replace(code, before, invalid));
    assert(asset_code_error(code)[0]);
    int shifted_start, shifted_end;
    assert(asset_code_execution_location(code, &shifted_start, &shifted_end));
    assert(shifted_start == start + 3 && shifted_end == end + 3);
    av_value *state = asset_code_capture(code);
    asset_code *restored = asset_code_restore(state, NULL);
    assert(restored && asset_code_error(restored)[0]);
    asset_code_set_catalog(restored, catalog, 1);
    asset_code_execution(restored, graph_id, node_id);
    assert(asset_code_execution_location(restored, &shifted_start, &shifted_end));
    assert(shifted_start == start + 3 && shifted_end == end + 3);
    assert(asset_code_replay(restored, false, true));
    assert(!asset_code_error(restored)[0]);
    assert(asset_code_source(restored)->asset);
    assert(asset_code_node_at(restored, at, &graph_id, &node_id));
    asset_code_free(restored);
    av_value_free(state);
    const char *changed_statement = "??\nbehavior Consumer { value one = "
                                    "First.Different(); }";
    assert(asset_code_replace(code, invalid, changed_statement));
    assert(!asset_code_execution_location(code, &shifted_start, &shifted_end));
    assert(asset_code_replay(code, false, true));
    free(invalid);
    free(before);
    assert(asset_code_replay(code, false, true));
    editor_free(e);
    graph *function = behavior_asset_find_graph_by_name(asset, "Same");
    assert(function && !graph_set_metadata(function, "Different", ""));
    asset_code_set_catalog(code, catalog, 1);
    assert(asset_code_error(code)[0]);
    asset_code_free(code);
    behavior_asset_free(asset);
    code_source_free(library);
    assert(!unlink(path));
}

static void prototype_symbols_survive_editor_catalog_refresh()
{
    const char *path = "projects/arena2d/content/behaviors/PlayerEffects.code";
    char *text = text_file_read_disk(path), error[256];
    assert(text);
    code_catalog *catalog = code_catalog_load_project("projects/arena2d");
    const code_source_context *context = code_catalog_context(catalog);
    assert(context && context->reference_count);
    const behavior_asset **assets = NULL;
    for (int i = 0; i < context->library_count; i++) {
        apush(assets, context->libraries[i]);
    }
    for (int i = 0; i < context->event_count; i++) {
        apush(assets, context->events[i]);
    }
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && strstr(asset_code_error(code), "MovementEmitter"));
    game_prototype_registry registry = app_component_registry();
    asset_code_set_registry(code, &registry);
    asset_code_set_references(code, context->references, context->reference_count);
    asset_code_set_catalog(code, assets, acount(assets));
    assert(!asset_code_error(code)[0] && !asset_code_dirty(code));
    assert(!strcmp(asset_code_text(code), text));
    behavior_asset *original = behavior_asset_clone(asset_code_source(code)->asset);
    char component[] = "GAME.GameVelocity", owner[] = "Player";
    const char *components[] = { component }, *owners[] = { owner };
    code_prototype prototypes[] = { { owner, components, 1 } };
    av_value *before_context = asset_code_capture(code);
    asset_code_set_prototypes(code, prototypes, 1, owners, 1);
    component[0] = owner[0] = '?';
    const code_source_context *owned = &asset_code_source(code)->context;
    assert(owned->prototype_count == 1 && owned->owner_prototype_count == 1);
    assert(!strcmp(owned->prototypes[0].components[0], "GAME.GameVelocity"));
    assert(!strcmp(owned->owner_prototypes[0], "Player"));
    assert(behavior_asset_equal(original, asset_code_source(code)->asset));
    assert(!asset_code_dirty(code));
    av_value *after_context = asset_code_capture(code);
    assert(
        av_get_int(av_obj_get(before_context, "step")) ==
        av_get_int(av_obj_get(after_context, "step"))
    );
    assert(
        av_arr_size(av_obj_get(before_context, "history")) ==
        av_arr_size(av_obj_get(after_context, "history"))
    );
    av_value_free(before_context);
    av_value_free(after_context);
    const char *updated_components[] = { "GAME.GameTransform" };
    const char *updated_owners[] = { "Player" };
    code_prototype updated[] = { { "Player", updated_components, 1 } };
    asset_code_set_prototypes(code, updated, 1, updated_owners, 1);
    assert(!strcmp(asset_code_source(code)->context.prototypes[0].components[0], "GAME.GameTransform"));
    assert(behavior_asset_equal(original, asset_code_source(code)->asset));
    assert(!asset_code_dirty(code));
    behavior_asset **replacement = NULL;
    for (int i = 0; i < acount(assets); i++) {
        apush(replacement, behavior_asset_clone(assets[i]));
    }
    asset_code_set_catalog(code, (const behavior_asset *const *)replacement, acount(replacement));
    assert(!asset_code_error(code)[0] && !asset_code_dirty(code));
    assert(behavior_asset_equal(original, asset_code_source(code)->asset));
    asset_code_set_references(code, NULL, 0);
    assert(strstr(asset_code_error(code), "MovementEmitter"));
    assert(!strcmp(asset_code_text(code), text) && !asset_code_dirty(code));
    asset_code_set_references(code, context->references, context->reference_count);
    assert(!asset_code_error(code)[0] && !asset_code_dirty(code));
    assert(behavior_asset_equal(original, asset_code_source(code)->asset));
    av_value *checkpoint = asset_code_capture(code);
    asset_code *restored = asset_code_restore(checkpoint, &registry);
    assert(restored);
    asset_code_set_references(restored, context->references, context->reference_count);
    asset_code_set_catalog(restored, assets, acount(assets));
    asset_code_set_prototypes(restored, updated, 1, updated_owners, 1);
    assert(!asset_code_error(restored)[0] && !asset_code_dirty(restored));
    assert(behavior_asset_equal(original, asset_code_source(restored)->asset));
    asset_code_free(restored);
    av_value_free(checkpoint);
    asset_code_free(code);
    behavior_asset_free(original);
    for (int i = 0; i < acount(replacement); i++) {
        behavior_asset_free(replacement[i]);
    }
    afree(replacement);
    afree(assets);
    code_catalog_free(catalog);
    free(text);
}

static void projectile_source_survives_scene_publication()
{
    const char *path = "projects/arena2d/content/behaviors/ProjectileHit.code";
    char *text = text_file_read_disk(path), error[256];
    assert(text);
    code_catalog *catalog = code_catalog_load_project("projects/arena2d");
    const code_source_context *context = code_catalog_context(catalog);
    assert(context);
    const behavior_asset **assets = NULL;
    ecs_world *world = ecs_world_new();
    game_live_world *live = game_live_world_new(world);
    for (int i = 0; i < context->library_count + context->event_count; i++) {
        const behavior_asset *asset = i < context->library_count
                                        ? context->libraries[i]
                                        : context->events[i - context->library_count];
        apush(assets, asset);
        assert(!game_live_add_definition(
            live,
            game_live_definition_new(asset->name, behavior_asset_clone(asset))
        ));
    }
    app_scene_authoring scene = { .live = live };
    behavior_asset *working = scene_graph_source_path(&scene, text, path, error, sizeof error);
    assert(working);
    editor *e = editor_new(working);
    editor_set_asset_catalog(e, assets, acount(assets));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code);
    game_prototype_registry registry = app_component_registry();
    asset_code_set_registry(code, &registry);
    asset_code_set_catalog(code, assets, acount(assets));
    av_value *checkpoint = asset_code_capture(code);
    asset_code *restored = asset_code_restore(checkpoint, &registry);
    assert(restored);
    asset_code_free(code);
    code = restored;
    asset_code_set_catalog(code, assets, acount(assets));
    av_value_free(checkpoint);
    behavior_asset *authored = behavior_asset_clone(asset_code_source(code)->asset);
    behavior_asset **rebound = NULL;
    for (int frame = 0; frame < 8; frame++) {
        /* Studio republishes equivalent catalog models as their documents are
           refreshed. Implicit component/Self producers must retain identity. */
        behavior_asset **next_catalog = NULL;
        for (int i = 0; i < acount(assets); i++) {
            apush(next_catalog, behavior_asset_clone(assets[i]));
        }
        editor_set_asset_catalog(e, (const behavior_asset *const *)next_catalog, acount(next_catalog));
        asset_code_set_catalog(code, (const behavior_asset *const *)next_catalog, acount(next_catalog));
        assert(behavior_asset_equal(authored, asset_code_source(code)->asset));
        for (int i = 0; i < acount(rebound); i++) {
            behavior_asset_free(rebound[i]);
        }
        afree(rebound);
        rebound = next_catalog;
        asset_code_time(code, frame);
        asset_code_sync_graph(code, e, false);
        ed_compile(e);
        editor_tick(e, frame);
        if (e->arrange_pending) {
            for (int g = 0; g < acount(e->asset->graphs); g++) {
                graph *graph = e->asset->graphs[g];
                for (int n = 0; n < acount(graph->nodes); n++) {
                    if (!graph->nodes[n]->placed) {
                        graph->nodes[n]->x += 100;
                        graph->nodes[n]->y += 100;
                    }
                }
            }
            e->arrange_pending = false;
        }
        assert(!strcmp(asset_code_text(code), text));
        assert(!asset_code_dirty(code));
    }
    asset_code_free(code);
    editor_free(e);
    behavior_asset_free(authored);
    for (int i = 0; i < acount(rebound); i++) {
        behavior_asset_free(rebound[i]);
    }
    afree(rebound);
    game_live_world_free(live);
    ecs_world_free(world);
    afree(assets);
    code_catalog_free(catalog);
    free(text);
}

static void component_source_survives_compile_and_open()
{
    const char *text = "behavior Labels { on Tick(GameMovement movement) { "
                       "movement.speed += event.dt; } }";
    char path[] = "/tmp/chibi-source-component-open-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && asset_code_source(code)->asset);
    behavior_asset *authored = behavior_asset_clone(asset_code_source(code)->asset);
    editor *e = editor_new(behavior_asset_clone(authored));
    assert(behavior_asset_equal(authored, e->asset));
    asset_code_sync_graph(code, e, false);
    ed_compile(e);
    editor_tick(e, 1);
    assert(behavior_asset_equal(authored, e->asset));
    for (int g = 0; g < acount(e->asset->graphs); g++) {
        graph *graph = e->asset->graphs[g];
        for (int n = 0; n < acount(graph->nodes); n++) {
            graph->nodes[n]->x += 100;
            graph->nodes[n]->y += 100;
        }
    }
    e->arrange_pending = false;
    assert(!asset_code_sync_graph(code, e, false));
    ed_compile(e);
    editor_tick(e, 2);
    assert(!asset_code_sync_graph(code, e, false));
    assert(!asset_code_dirty(code) && !strcmp(asset_code_text(code), text));
    behavior_asset_free(authored);
    asset_code_free(code);
    editor_free(e);
    assert(!unlink(path));
}

static void opening_metadata_survives_catalog_setup()
{
    const char *text = "behavior MetadataOpen { on Tick() {} }";
    char path[] = "/tmp/chibi-source-metadata-open-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    code_source *source = code_source_parse(text);
    assert(source->asset && acount(source->asset->graph->nodes));
    node *original = source->asset->graph->nodes[0];
    uint64_t id = original->id;
    original->x = 123;
    original->placed = true;
    original->breakpoint = true;
    assert(code_source_metadata_save(source, path));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_dirty(code));
    game_prototype_registry registry = { 0 };
    asset_code_set_registry(code, &registry);
    code_source *library = code_source_parse(
        "library Helper { pure function Seven(Input: float = 7) -> "
        "(Result: float) { result Result: float = entry.Input; } }"
    );
    assert(library->asset);
    const behavior_asset *catalog[] = { library->asset };
    asset_code_set_catalog(code, catalog, 1);
    const code_source *opened = asset_code_source(code);
    const node *restored = graph_node_by_id(opened->asset->graph, id);
    assert(restored && restored->x == 123 && restored->placed && restored->breakpoint);
    assert(!asset_code_dirty(code) && !strcmp(asset_code_text(code), text));
    asset_code_free(code);
    code_source_free(library);
    code_source_free(source);
    char metadata_path[256];
    snprintf(metadata_path, sizeof metadata_path, "%s.meta", path);
    assert(!unlink(metadata_path) && !unlink(path));
}

static void preexisting_graph_history()
{
    const char
        *text = "behavior History { state count: int = 1; on Tick() {} }";
    char path[] = "/tmp/chibi-source-history-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    code_source *source = code_source_parse(text);
    assert(source->asset);
    editor *e = editor_new(behavior_asset_clone(source->asset));
    e->arrange_pending = false;
    node *tracked = e->asset->graph->nodes[0];
    uint64_t tracked_id = tracked->id;
    tracked->x = 10;
    tracked->placed = true;
    apush(e->undo, behavior_asset_clone(e->asset));
    tracked->x = 20;
    tracked->breakpoint = true;
    e->asset->variables[0].default_value.i = 2;
    apush(e->undo, behavior_asset_clone(e->asset));
    e->asset->variables[0].default_value.i = 3;
    tracked->x = 30;
    behavior_asset *future = behavior_asset_clone(e->asset);
    future->variables[0].default_value.i = 4;
    graph_node_by_id(future->graph, tracked_id)->x = 40;
    apush(e->redo, future);
    char error[256];
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_error(code)[0]);
    assert(!asset_code_sync_graph(code, e, false));
    assert(
        asset_code_dirty(code) && asset_code_can_replay(code, false) &&
        asset_code_can_replay(code, true)
    );
    av_value *checkpoint = asset_code_capture(code);
    asset_code *restored = asset_code_restore(checkpoint, NULL);
    assert(restored);
    asset_code_free(code);
    code = restored;
    av_value_free(checkpoint);
    const int expected[] = { 2, 1, 2, 3, 4 };
    for (int i = 0; i < 5; i++) {
        assert(asset_code_replay(code, i >= 2, false));
        assert(asset_code_sync_graph(code, e, false));
        assert(editor_behavior_asset(e)->variables[0].default_value.i == expected[i]);
        const node *replayed = graph_node_by_id(editor_behavior_asset(e)->graph, tracked_id);
        assert(replayed && replayed->x == expected[i] * 10);
        assert(replayed->placed && replayed->breakpoint == (expected[i] != 1));
        assert(
            !strstr(asset_code_text(code), "@node") &&
            !strstr(asset_code_text(code), "@authoring")
        );
        assert(
            editor_behavior_asset(e)->variables[0].id ==
            source->asset->variables[0].id
        );
    }
    assert(!asset_code_can_replay(code, true));
    asset_code_free(code);
    editor_free(e);
    code_source_free(source);
    unlink(path);
}

static void shared_source_history()
{
    const char *text = "// Keep this author comment.\nbehavior Shared { state "
                       "float speed = 1; on Tick() { "
                       "speed += 1; } }";
    char path[] = "/tmp/chibi-source-shared-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    char error[256];
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_error(code)[0]);
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    assert(asset && acount(asset->variables) == 1);
    editor *e = editor_new(asset);
    assert(!asset_code_sync_graph(code, e, true) && !asset_code_pending(code));
    /* CPU fixture completes the initial graph view arrangement explicitly. */
    e->arrange_pending = false;
    asset_code_sync_graph(code, e, true);
    assert(!asset_code_dirty(code) && !asset_code_pending(code));
    uint64_t breakpoint_graph, breakpoint_node;
    int operand = (int)(strstr(text, "speed +=") - text);
    assert(asset_code_breakpoint_at(code, operand, &breakpoint_graph, &breakpoint_node));
    graph *breakpoint_owner = behavior_asset_find_graph(asset, breakpoint_graph);
    node *executable = breakpoint_owner
                         ? graph_node_by_id(breakpoint_owner, breakpoint_node)
                         : NULL;
    assert(executable && node_behavior_for(executable) != NODE_BEHAVIOR_PURE);
    assert(!asset_code_error(code)[0]);
    const behavior_asset *working = editor_behavior_asset(e);
    uint64_t state_id = working->variables[0].id;
    kit_code_set_cursor(asset_code_editor(code), 5);
    ((behavior_asset *)working)->variables[0].default_value.f = 3;
    e->edit_pending = true;
    assert(asset_code_sync_graph(code, e, true));
    assert(asset_code_dirty(code) && asset_code_can_replay(code, false));
    assert(kit_code_cursor(asset_code_editor(code)) == 5);
    code_source *changed = code_source_parse(kit_code_text(asset_code_editor(code), NULL));
    behavior_asset *candidate = code_source_compile_behavior(changed);
    assert(candidate && candidate->variables[0].default_value.f == 3);
    assert(candidate->variables[0].id == state_id);
    behavior_asset_free(candidate);
    code_source_free(changed);
    assert(asset_code_replay(code, false, true));
    assert(asset_code_sync_graph(code, e, true));
    assert(editor_behavior_asset(e)->variables[0].default_value.f == 1);
    assert(asset_code_replay(code, true, true));
    assert(asset_code_sync_graph(code, e, true));
    assert(editor_behavior_asset(e)->variables[0].default_value.f == 3);
    assert(asset_code_save(code, error, sizeof error));
    assert(!asset_code_dirty(code));
    assert(strstr(kit_code_text(asset_code_editor(code), NULL), "Keep this author comment"));
    char *saved_text = text_file_read_disk(path);
    assert(saved_text);
    assert(!strstr(saved_text, "@node") && !strstr(saved_text, "@authoring"));
    assert(!strstr(kit_code_text(asset_code_editor(code), NULL), "@authoring"));
    assert(!strcmp(asset_code_text(code), saved_text));
    av_value *saved_state = asset_code_capture(code);
    assert(!strcmp(av_get_str(av_obj_get(saved_state, "text")), saved_text));
    /* Old checkpoints may place a cursor in the now hidden metadata. */
    av_doc *projected_state = av_doc_new();
    av_value *projected_root = av_clone(projected_state, saved_state);
    av_doc_set_root(projected_state, projected_root);
    av_obj_set(projected_state, projected_root, "cursor", av_sint(projected_state, (int)strlen(saved_text)));
    av_obj_set(projected_state, projected_root, "anchor", av_sint(projected_state, (int)strlen(saved_text)));
    asset_code *projected = asset_code_restore(projected_root, NULL);
    assert(projected && !strcmp(asset_code_text(projected), saved_text));
    assert(!strstr(kit_code_text(asset_code_editor(projected), NULL), "@authoring"));
    assert(
        kit_code_cursor(asset_code_editor(projected)) <=
        (int)strlen(kit_code_text(asset_code_editor(projected), NULL))
    );
    asset_code_free(projected);
    av_doc_free(projected_state);
    av_value_free(saved_state);
    code_source *saved_source = code_source_parse(saved_text);
    behavior_asset *saved_asset = code_source_compile_behavior(saved_source);
    assert(saved_asset && saved_asset->variables[0].id == state_id);
    assert(saved_asset->graph->graph_id == editor_behavior_asset(e)->graph->graph_id);
    char *graph_text = behavior_asset_save(editor_behavior_asset(e));
    assert(graph_text && write_text_file_atomic(path, graph_text) == 0);
    free(graph_text);
    char *graph_saved = text_file_read_disk(path);
    assert(graph_saved && strstr(graph_saved, "Keep this author comment"));
    code_source *graph_source = code_source_parse(graph_saved);
    behavior_asset *graph_asset = code_source_compile_behavior(graph_source);
    assert(graph_asset && graph_asset->variables[0].id == state_id);
    behavior_asset_free(graph_asset);
    code_source_free(graph_source);
    free(graph_saved);
    behavior_asset_free(saved_asset);
    code_source_free(saved_source);
    free(saved_text);
    const char *before = asset_code_text(code);
    char *rename_before = strdup(before);
    int rename_at = (int)(strstr(before, "speed +=") - before);
    char *renamed = code_intelligence_rename(asset_code_source(code), rename_at, "velocity", error, sizeof error);
    assert(renamed && strstr(renamed, "velocity +=") && !strstr(renamed, "speed +="));
    assert(asset_code_replace(code, rename_before, renamed));
    assert(!strstr(kit_code_text(asset_code_editor(code), NULL), "@authoring"));
    assert(!strstr(asset_code_text(code), "@authoring"));
    asset_code_sync_graph(code, e, true);
    assert(!strcmp(editor_behavior_asset(e)->variables[0].name, "velocity"));
    assert(!asset_code_replace(code, rename_before, renamed));
    assert(asset_code_replay(code, false, true));
    asset_code_sync_graph(code, e, true);
    assert(!strcmp(editor_behavior_asset(e)->variables[0].name, "speed"));
    assert(!strcmp(asset_code_text(code), rename_before));
    assert(!strstr(kit_code_text(asset_code_editor(code), NULL), "@authoring"));
    assert(editor_behavior_asset(e)->variables[0].id == state_id);
    assert(!asset_code_dirty(code));
    /* Invalid drafts remain exact text; separate metadata survives history. */
    const char *broken_body = "behavior Shared { state string label = \"";
    char *broken = strdup(broken_body);
    assert(broken);
    assert(asset_code_replace(code, rename_before, broken));
    assert(!strcmp(kit_code_text(asset_code_editor(code), NULL), broken_body));
    av_value *broken_state = asset_code_capture(code);
    asset_code *broken_restored = asset_code_restore(broken_state, NULL);
    assert(broken_restored && !strcmp(asset_code_text(broken_restored), broken));
    assert(!strcmp(kit_code_text(asset_code_editor(broken_restored), NULL), broken_body));
    asset_code_free(broken_restored);
    av_value_free(broken_state);
    assert(asset_code_replay(code, false, true));
    assert(!strcmp(asset_code_text(code), rename_before));
    asset_code_sync_graph(code, e, true);
    free(broken);
    free(rename_before);
    free(renamed);
    av_value *checkpoint = asset_code_capture(code);
    av_doc *draft = av_doc_new();
    av_value *state = av_clone(draft, checkpoint);
    av_doc_set_root(draft, state);
    av_obj_set(draft, state, "text", av_str(draft, "behavior Shared { state float speed ="));
    av_value *history = av_obj_get(state, "history");
    /* A new edit discards redo entries, including the malformed draft above. */
    size_t kept = (size_t)av_get_int(av_obj_get(state, "step")) + 1;
    while (av_arr_size(history) > kept) {
        av_arr_remove(history, av_arr_size(history) - 1);
    }
    av_obj_add_str(draft, av_arr_add_obj(draft, history), "text", "behavior Shared { state float speed =");
    av_obj_set(draft, state, "step", av_sint(draft, (int)av_arr_size(history) - 1));
    asset_code *invalid = asset_code_restore(state, NULL);
    assert(invalid && asset_code_error(invalid)[0]);
    assert(!asset_code_sync_graph(invalid, e, true));
    assert(editor_behavior_asset(e)->variables[0].default_value.f == 3);
    assert(text_overlay_text(path) && strstr(text_overlay_text(path), "Shared"));
    assert(asset_code_replay(invalid, false, true));
    asset_code_sync_graph(invalid, e, true);
    assert(!asset_code_error(invalid)[0]);
    asset_code_free(invalid);
    av_doc_free(draft);
    av_value_free(checkpoint);
    editor_free(e);
    code_source_free(source);
    asset_code_free(code);
    assert(!unlink(path));
}

static void reactive_graph_publication_history()
{
    const char *text = "behavior ReactiveEdit { bind "
                       "self.ParticleEmitter.Active = false; }";
    char path[] = "/tmp/chibi-source-reactive-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_error(code)[0]);
    const behavior_asset *native = asset_code_source(code)->asset;
    assert(native && acount(native->reactives) == 1);
    behavior_asset *before = behavior_asset_clone(native);
    editor *e = editor_new(behavior_asset_clone(native));
    e->arrange_pending = false;
    assert(!asset_code_sync_graph(code, e, false));
    assert(!asset_code_pending(code) && !asset_code_dirty(code));
    graph *g = e->asset->graph;
    const behavior_reactive *relationship = &e->asset->reactives[0];
    uint64_t event_id = relationship->event_id, set_id = relationship->statement_node_id;
    node *set = graph_node_by_id(g, set_id);
    int value = -1;
    for (int i = 0; i < set->input_count; i++) {
        if (set->input_pins[i].kind == PIN_DATA && set->input_pins[i].type == PIN_BOOL) {
            value = i;
        }
    }
    assert(value >= 0);
    pin_value yes = { .type = PIN_BOOL, .b = true };
    assert(!graph_set_pin_default(g, set, 0, value, &yes));
    e->edit_pending = true;
    assert(!behavior_reactive_validate(e->asset, error, sizeof error));
    assert(asset_code_sync_graph(code, e, false));
    assert(!asset_code_pending(code) && !asset_code_error(code)[0]);
    assert(strstr(asset_code_text(code), "true"));
    assert(behavior_reactive_validate(e->asset, error, sizeof error));
    assert(behavior_asset_equal(e->asset, asset_code_source(code)->asset));
    assert(
        e->asset->reactives[0].event_id == event_id &&
        e->asset->reactives[0].statement_node_id == set_id
    );
    behavior_asset *after = behavior_asset_clone(e->asset);
    av_value *state = asset_code_capture(code);
    asset_code *restored = asset_code_restore(state, NULL);
    assert(restored);
    asset_code_sync_graph(restored, e, false);
    assert(!asset_code_pending(restored) && !asset_code_error(restored)[0]);
    assert(behavior_asset_equal(e->asset, after));
    assert(asset_code_replay(restored, false, false));
    assert(asset_code_sync_graph(restored, e, false));
    assert(behavior_reactive_validate(e->asset, error, sizeof error));
    assert(behavior_asset_equal(e->asset, before));
    assert(asset_code_replay(restored, true, false));
    assert(asset_code_sync_graph(restored, e, false));
    assert(behavior_reactive_validate(e->asset, error, sizeof error));
    assert(behavior_asset_equal(e->asset, after));
    asset_code_free(restored);
    av_value_free(state);
    behavior_asset_free(after);
    behavior_asset_free(before);
    editor_free(e);
    asset_code_free(code);
    assert(!unlink(path));
}

static void shared_system_query_history()
{
    game_system_document *system = game_system_document_new("SourceSystem");
    char *text = game_system_document_save(system, resolve_system_component, system_component_name, NULL);
    assert(text);
    char path[] = "/tmp/chibi-source-system-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    free(text);
    char error[256];
    asset_code *code = asset_code_open(path, error, sizeof error);
    assert(code && !asset_code_error(code)[0]);
    editor *e = editor_new(
        behavior_asset_clone(game_system_document_behavior_asset(system))
    );
    e->arrange_pending = false;
    unsigned initial_revision = game_system_document_revision(system);
    assert(!asset_code_sync_system(code, e, system, true));
    assert(game_system_document_revision(system) == initial_revision);
    const ecs_component_desc *component = resolve_system_component("GAME.GameTransform", NULL);
    assert(component);
    ecs_query_expr *query = ecs_query_expr_has(component->id);
    assert(game_system_document_set_query(system, query) == 0);
    ecs_query_expr_free(query);
    assert(asset_code_sync_system(code, e, system, true));
    assert(strstr(kit_code_text(asset_code_editor(code), NULL), "query"));
    assert(asset_code_replay(code, false, true));
    asset_code_sync_system(code, e, system, true);
    assert(!game_system_document_query(system));
    assert(asset_code_replay(code, true, true));
    asset_code_sync_system(code, e, system, true);
    assert(
        ecs_query_expr_get_component(game_system_document_query(system)) ==
        component->id
    );
    assert(asset_code_save(code, error, sizeof error));
    text = text_file_read_disk(path);
    game_system_document *reopened = game_system_document_load(
        text,
        resolve_system_component,
        NULL,
        error,
        sizeof error
    );
    assert(
        reopened && ecs_query_expr_get_component(game_system_document_query(reopened)) ==
                        component->id
    );
    free(text);
    game_system_document_free(reopened);
    editor_free(e);
    asset_code_free(code);
    game_system_document_free(system);
    assert(!unlink(path));
}

void test_asset_code_checkpoint()
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    const char *path = "/tmp/chibi-source-checkpoint.kdl";
    const char *valid = "name \"Last good\"\n";
    av_obj_add_str(doc, root, "path", path);
    av_obj_add_str(doc, root, "disk", "name \"Saved\"\n");
    av_obj_add_str(doc, root, "text", "name \"unfinished");
    av_obj_add_str(doc, root, "valid", valid);
    av_obj_add_bool(doc, root, "overlay", true);
    av_obj_add_int(doc, root, "step", 1);
    av_obj_add_int(doc, root, "cursor", 3);
    av_obj_add_int(doc, root, "anchor", 12);
    av_value *history = av_obj_add_arr(doc, root, "history");
    av_obj_add_str(doc, av_arr_add_obj(doc, history), "text", "name \"Saved\"\n");
    av_obj_add_str(doc, av_arr_add_obj(doc, history), "text", "name \"unfinished");
    av_obj_add_str(doc, av_arr_add_obj(doc, history), "text", "name \"Finished\"\n");

    /* An invalid draft and the redo branch survive replacement independently
     * of the last valid preview and saved disk baseline. */
    asset_code *code = asset_code_restore(root, NULL);
    assert(code && asset_code_dirty(code) && asset_code_error(code)[0]);
    assert(!strcmp(text_overlay_text(path), valid));
    assert(kit_code_cursor(asset_code_editor(code)) == 3);
    int start, end;
    assert(kit_code_selection(asset_code_editor(code), &start, &end));
    assert(start == 3 && end == 12);
    av_value *snapshot = asset_code_capture(code);
    assert(av_equals(snapshot, root));
    assert(asset_code_has_state(code, snapshot));
    char error[256];
    assert(!asset_code_save(code, error, sizeof error));
    assert(error[0]);
    asset_code *replacement = asset_code_restore(snapshot, NULL);
    assert(replacement);
    asset_code_free(code);
    assert(!strcmp(text_overlay_text(path), valid));
    asset_code_free(replacement);
    assert(!text_overlay_text(path));

    code = asset_code_restore(snapshot, NULL);
    assert(code && !strcmp(kit_code_text(asset_code_editor(code), NULL), "name \"unfinished"));
    av_value *again = asset_code_capture(code);
    assert(av_equals(snapshot, again));
    asset_code_free(code);
    av_value_free(again);
    av_value_free(snapshot);
    av_doc_free(doc);

    /* Independent visual changes merge into an unsaved source draft without
     * canonicalizing its comments or overwriting the typed name. */
    char file_path[] = "/tmp/chibi-kdl-refresh-XXXXXX";
    int fd = mkstemp(file_path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("name \"Saved\"\nvalue 2\n", file);
    assert(!fclose(file));
    doc = av_doc_new();
    root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_str(doc, root, "path", file_path);
    av_obj_add_str(doc, root, "disk", "name \"Saved\"\nvalue 1\n");
    av_obj_add_str(doc, root, "text", "// draft\nname  \"Draft\"\nvalue 1\n");
    av_obj_add_str(doc, root, "valid", "// draft\nname  \"Draft\"\nvalue 1\n");
    av_obj_add_bool(doc, root, "overlay", true);
    av_obj_add_int(doc, root, "step", 0);
    history = av_obj_add_arr(doc, root, "history");
    av_obj_add_str(doc, av_arr_add_obj(doc, history), "text", "// draft\nname  \"Draft\"\nvalue 1\n");
    code = asset_code_restore(root, NULL);
    assert(code);
    asset_code_refresh(code);
    assert(!strcmp(kit_code_text(asset_code_editor(code), NULL), "// draft\nname  \"Draft\"\nvalue 2\n"));
    assert(asset_code_dirty(code));
    assert(asset_code_save(code, error, sizeof error));
    assert(!asset_code_dirty(code));
    asset_code_free(code);
    av_doc_free(doc);
    assert(!unlink(file_path));
    prototype_symbols_survive_editor_catalog_refresh();
    projectile_source_survives_scene_publication();
    component_source_survives_compile_and_open();
    opening_metadata_survives_catalog_setup();
    preexisting_graph_history();
    shared_source_history();
    reactive_graph_publication_history();
    shared_system_query_history();
    changed_catalog_signature();
    compatible_catalog_publication();
    inherited_catalog_defaults();
    source_only_repair_preserves_open_graph();
    system_view_preserves_source();
}
