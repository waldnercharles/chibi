#include "editor/workspace/gameplay_input.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs after Cute initialization; no rendering or input injection is required
 * to prove that a native generation can reconstruct authoring state. */
typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} checkpoint_fixture;

static checkpoint_fixture fixture_new()
{
    checkpoint_fixture fixture = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(
            ecs_world_register_component(fixture.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
        );
    }
    fixture.live = game_live_world_new(fixture.world);
    game_live_definition *definition = game_live_definition_new(
        "ReloadBehavior",
        behavior_asset_new("ReloadBehavior")
    );
    assert(definition && game_live_add_definition(fixture.live, definition) == 0);
    fixture.graph = editor_new(behavior_asset_new("ReloadBehavior"));
    fixture.runtime = engine_new(fixture.world, fixture.live);
    fixture.scene = app_scene_authoring_new(
        fixture.world,
        fixture.live,
        definition,
        editor_behavior_asset(fixture.graph),
        fixture.graph,
        NULL,
        NULL,
        fixture.runtime
    );
    assert(fixture.scene);
    return fixture;
}

static void fixture_free(checkpoint_fixture *fixture)
{
    app_scene_authoring_free(fixture->scene);
    engine_free(fixture->runtime);
    game_live_world_free(fixture->live);
    editor_free(fixture->graph);
    ecs_world_free(fixture->world);
}

static checkpoint_fixture roundtrip(checkpoint_fixture *source)
{
    char *scene = app_scene_authoring_checkpoint(source->scene);
    char *graph = editor_checkpoint(source->graph);
    assert(scene && graph);
    bool full_catalog = game_live_definition_count(source->live) > 1;
    fixture_free(source);
    checkpoint_fixture candidate = fixture_new();
    if (full_catalog) {
        assert(app_scene_authoring_set_content_root(candidate.scene, "test/fixtures/content") == 0);
        assert(app_scene_authoring_load_content(candidate.scene) == 0);
    }
    assert(app_scene_authoring_restore_checkpoint(candidate.scene, scene) == 0);
    assert(editor_restore_checkpoint(candidate.graph, graph));
    app_scene_authoring_set_editor_working(
        candidate.scene,
        editor_behavior_asset(candidate.graph)
    );
    free(scene);
    free(graph);
    return candidate;
}

static void shader_scene_load()
{
    checkpoint_fixture f = fixture_new();
    assert(app_scene_authoring_set_content_root(f.scene, "test/fixtures/content") == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    const char *shared[] = { "content/behaviors/HealthBehavior.yaml",
                             "content/behaviors/PlayerContactBehavior.yaml" };
    for (int i = 0; i < 2; i++) {
        char *json = read_scene_file(shared[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        assert(asset);
        game_live_definition *definition = game_live_definition_new(asset->name, asset);
        assert(definition && game_live_add_definition(f.live, definition) == 0);
        game_live_definition_set_path(definition, shared[i]);
        assert(
            game_prototype_preview_register_behavior(
                f.scene->prototype_preview,
                asset->name,
                asset
            ) == 0
        );
    }
    int loaded = app_scene_authoring_load(f.scene, "test/fixtures/content/scenes/ShaderDemo.yaml");
    if (loaded) {
        fprintf(stderr, "shader scene load: %s\n", app_scene_authoring_message(f.scene));
    }
    assert(loaded == 0 && acount(f.scene->actors) == 3);
    game_live_definition *input = game_live_find_definition(f.live, "InputBehavior");
    int applied_nodes = acount(input->applied->graph->nodes);
    int working_nodes = acount(input->working->graph->nodes);
    graph_add_node(input->working->graph, registry_node_by_name("Integer"));
    game_live_mark_dirty(input);
    f = roundtrip(&f);
    input = game_live_find_definition(f.live, "InputBehavior");
    assert(input->dirty && acount(input->working->graph->nodes) == working_nodes + 1);
    assert(acount(input->applied->graph->nodes) == applied_nodes);
    assert(graph_remove_node(input->working->graph, alast(input->working->graph->nodes)) == 0);
    assert(acount(f.scene->actors) == 3);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0);
    game_live_definition *input_before_open = game_live_find_definition(f.live, "InputBehavior");
    char *input_before_json = behavior_asset_save_json(input_before_open->working);
    free(input_before_json);
    assert(app_scene_authoring_open_behavior(f.scene, "InputBehavior") == 0);
    char *input_after_open_json = behavior_asset_save_json(editor_behavior_asset(f.graph));
    free(input_after_open_json);
    assert(app_scene_authoring_preview_prototype(f.scene, "Player") == 0);
    behavior_instance *movement = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    behavior_instance_set_trace_enabled(movement, 1);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    int idle_trace = acount(movement->trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 0.00) >= FLOW_OK);
    assert(acount(movement->trace) == idle_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 1 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    int first_trace = acount(movement->trace);
    assert(first_trace > idle_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 2 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    int held_trace = acount(movement->trace);
    assert(held_trace > first_trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 3 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    int release_trace = acount(movement->trace);
    assert(release_trace > held_trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 4 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    assert(acount(movement->trace) == release_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 5 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    int refocused_trace = acount(movement->trace);
    engine_viewport *unfocused = engine_viewport_new(32, 32);
    app_gameplay_input(f.scene, f.runtime, unfocused);
    assert(app_scene_authoring_frame(f.scene, 6 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    int unfocused_trace = acount(movement->trace);
    assert(unfocused_trace > refocused_trace);
    app_gameplay_input(f.scene, f.runtime, unfocused);
    assert(app_scene_authoring_frame(f.scene, 7 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    assert(acount(movement->trace) == unfocused_trace);
    engine_viewport_free(unfocused);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 8 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    assert(acount(movement->trace) == unfocused_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    assert(app_scene_authoring_frame(f.scene, 9 * ENGINE_FIXED_SECONDS) >= FLOW_OK);
    assert(acount(movement->trace) > unfocused_trace);

    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_PREVIEW, APP_RUNTIME_PAUSE) == 0);
    behavior_instance *before = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    assert(before);
    assert(scene_debug_matches(editor_behavior_asset(f.graph), before->asset));
    char *linked_json = behavior_asset_save_json(before->asset);
    behavior_asset *linked_copy = behavior_asset_load_json(linked_json);
    free(linked_json);
    assert(linked_copy && scene_debug_matches(editor_behavior_asset(f.graph), linked_copy));
    behavior_asset_free(linked_copy);
    int count = acount(before->asset->graph->nodes);
    assert(editor_spawn(f.graph, registry_node_by_name("Has_GameHealth"), 20, 20));
    app_scene_authoring_set_editor_working(f.scene, editor_behavior_asset(f.graph));
    preview_behavior_edits(f.scene);
    behavior_instance *after = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    assert(after == before && acount(after->asset->graph->nodes) == count + 1);
    assert(scene_debug_matches(editor_behavior_asset(f.graph), after->asset));
    fixture_free(&f);
}

static void scene_tabs_retain_authored_state()
{
    checkpoint_fixture f = fixture_new();
    assert(
        app_scene_authoring_register_prototype(f.scene, "{\"name\":\"ReloadPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(app_scene_authoring_add_instance(f.scene, "ReloadPrototype") == 0);
    const char *first_path = "/tmp/chibi-39-scene-a.yaml";
    const char *second_path = "/tmp/chibi-39-scene-b.yaml";
    char *first_json = scene_authoring_scene_json(f.scene);
    assert(first_json && write_text_file_atomic(first_path, first_json) == 0);
    free(first_json);
    assert(app_scene_authoring_load(f.scene, first_path) == 0);
    scene_tabs_document_selected(f.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    int first = f.scene->active_tab;
    /* Loading into the untitled tab names it after the file. */
    assert(!strcmp(f.scene->tabs[first].label, "chibi-39-scene-a"));
    f.scene->camera = (scene_camera){ .view = { 1, 2, 300, 200 }, .saved = true };
    assert(f.scene->selected == 0);
    char *second_json = scene_authoring_scene_json(f.scene);
    assert(second_json && write_text_file_atomic(second_path, second_json) == 0);
    free(second_json);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int second = f.scene->active_tab;
    assert(first != second && f.scene->active_tab == second);
    GameTransform
        before = *GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity);
    GameTransform after = { 11, 12 };
    write_position(f.scene, f.scene->actors[0].entity, after);
    assert(scene_history_push(f.scene->history, f.scene->actors[0].entity, before, after));
    f.scene->scene_dirty = true;
    f.scene->camera.view.center_x = 42;
    scene_tabs_sync_current(f.scene);
    assert(scene_tabs_activate(f.scene, first) == 0);
    assert(GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity)->x == 1);
    assert(f.scene->camera.view.center_x == 1 && f.scene->selected == 0);
    assert(scene_tabs_activate(f.scene, second) == 0);
    assert(GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity)->x == 11);
    assert(f.scene->camera.view.center_x == 42 && f.scene->selected == 0);
    assert(replay_scene_edit(f.scene, false) == 0);
    assert(GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity)->x == 1);
    assert(replay_scene_edit(f.scene, true) == 0);
    assert(GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity)->x == 11);
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static void scene_tabs_save_as_keeps_identity()
{
    checkpoint_fixture f = fixture_new();
    assert(
        app_scene_authoring_register_prototype(f.scene, "{\"name\":\"SaveAsPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(app_scene_authoring_add_instance(f.scene, "SaveAsPrototype") == 0);
    const char *first_path = "/tmp/chibi-39-save-as-a.yaml";
    const char *second_path = "/tmp/chibi-39-save-as-b.yaml";
    char *json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(first_path, json) == 0);
    free(json);
    assert(app_scene_authoring_load(f.scene, first_path) == 0);
    int scene_tab = f.scene->active_tab;
    char *identity = xstrdup(f.scene->tabs[scene_tab].key);
    assert(app_scene_authoring_save(f.scene, second_path) == 0);
    assert(acount(f.scene->tabs) == 1 && f.scene->scene_path);
    assert(!strcmp(f.scene->scene_path, second_path));
    assert(!strcmp(f.scene->tabs[scene_tab].key, identity));
    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    assert(scene_tabs_activate(f.scene, scene_tab) == 0);
    assert(f.scene->document.kind == APP_EDITOR_DOCUMENT_SCENE);
    assert(!strcmp(f.scene->scene_path, second_path));
    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int scene_tabs = 0;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        scene_tabs += f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE;
    }
    assert(scene_tabs == 1);
    free(identity);
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static void scene_tabs_failed_restore_rolls_back()
{
    checkpoint_fixture f = fixture_new();
    assert(
        app_scene_authoring_register_prototype(f.scene, "{\"name\":\"RollbackPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(app_scene_authoring_add_instance(f.scene, "RollbackPrototype") == 0);
    const char *first_path = "/tmp/chibi-39-rollback-a.yaml";
    const char *second_path = "/tmp/chibi-39-rollback-b.yaml";
    char *json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(first_path, json) == 0);
    free(json);
    assert(app_scene_authoring_load(f.scene, first_path) == 0);
    json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(second_path, json) == 0);
    free(json);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int second = f.scene->active_tab;
    int first = -1;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        if (f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE && i != second) {
            first = i;
        }
    }
    assert(first >= 0 && scene_tabs_activate(f.scene, first) == 0);
    write_position(f.scene, f.scene->actors[0].entity, (GameTransform){ 11, 12 });
    f.scene->scene_dirty = true;
    scene_tabs_sync_current(f.scene);
    free(f.scene->tabs[second].scene_history);
    f.scene->tabs[second].scene_history = xstrdup("{}");
    assert(scene_tabs_activate(f.scene, second) != 0);
    assert(f.scene->active_tab == first && f.scene->scene_dirty);
    assert(GAME_GameTransform_get_const(f.world, f.scene->actors[0].entity)->x == 11);
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static void scene_tabs_discard_reverts_shared_assets()
{
    checkpoint_fixture f = fixture_new();
    assert(
        app_scene_authoring_register_prototype(f.scene, "{\"name\":\"DiscardPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(app_scene_authoring_open_prototype(f.scene, "DiscardPrototype") == 0);
    assert(
        app_scene_authoring_set_prototype_field(
            f.scene,
            "DiscardPrototype",
            "GameTransform",
            "x",
            (pin_value){ .type = PIN_F32, .f = 7 }
        ) == 0
    );
    int prototype_tab = f.scene->active_tab;
    assert(f.scene->prototypes[0].dirty);
    assert(scene_tabs_discard(f.scene, prototype_tab) == 0);
    pin_value value;
    assert(
        app_scene_authoring_read_prototype_field(f.scene, "DiscardPrototype", "GameTransform", "x", &value) ==
            0 &&
        value.f == 1 && !f.scene->prototypes[0].dirty
    );

    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    int behavior_nodes = acount(f.scene->definition->working->graph->nodes);
    assert(editor_spawn(f.graph, registry_node_by_name("Integer"), 12, 34));
    app_scene_authoring_set_editor_working(f.scene, editor_behavior_asset(f.graph));
    assert(sync_behavior_working(f.scene) == 0);
    int behavior_tab = f.scene->active_tab;
    assert(f.scene->definition->dirty);
    assert(scene_tabs_discard(f.scene, behavior_tab) == 0);
    assert(!f.scene->definition->dirty && !f.scene->definition->unsaved);
    assert(acount(f.scene->definition->working->graph->nodes) == behavior_nodes);
    fixture_free(&f);
}

/* The canvas arranges an opened graph's automatic nodes. Opening another
   behavior keeps both tabs saved. */
static void opening_behaviors_keeps_them_saved()
{
    checkpoint_fixture f = fixture_new();
    behavior_asset *asset = behavior_asset_new("ArrangedBehavior");
    node *automatic = graph_add_node(asset->graph, registry_node_by_name("Integer"));
    automatic->placed = false;
    uint64_t id = automatic->id;
    game_live_definition *arranged = game_live_definition_new("ArrangedBehavior", asset);
    assert(arranged && game_live_add_definition(f.live, arranged) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, "ArrangedBehavior") == 0);
    graph_node_by_id(editor_active_graph(f.graph), id)->x += 300;
    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    assert(sync_behavior_working(f.scene) == 0);
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        assert(!scene_tabs_dirty(f.scene, &f.scene->tabs[i]));
    }
    assert(!arranged->unsaved && !arranged->dirty);
    fixture_free(&f);
}

static void scene_tabs_close_underlying_scene_keeps_context()
{
    checkpoint_fixture f = fixture_new();
    const char *first_path = "/tmp/chibi-39-close-scene-a.yaml";
    const char *second_path = "/tmp/chibi-39-close-scene-c.yaml";
    char *json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(first_path, json) == 0);
    free(json);
    assert(app_scene_authoring_load(f.scene, first_path) == 0);
    json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(second_path, json) == 0);
    free(json);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int first = -1, second = f.scene->active_tab;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        if (f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE && i != second) {
            first = i;
        }
    }
    assert(first >= 0 && scene_tabs_activate(f.scene, first) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    assert(scene_tabs_discard(f.scene, first) == 0);
    assert(f.scene->document.kind == APP_EDITOR_DOCUMENT_SCENE);
    assert(!strcmp(f.scene->scene_path, second_path));
    assert(f.scene->tabs[f.scene->active_tab].kind == APP_EDITOR_DOCUMENT_SCENE);
    assert(!strcmp(f.scene->tabs[f.scene->active_tab].path, second_path));
    assert(
        scene_tabs_current_scene_key(f.scene) ==
        f.scene->tabs[f.scene->active_tab].key
    );
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static void scene_tabs_reject_last_scene_close()
{
    checkpoint_fixture f = fixture_new();
    assert(scene_tabs_discard(f.scene, f.scene->active_tab) == -1);
    assert(acount(f.scene->tabs) == 1);
    assert(f.scene->tabs[0].kind == APP_EDITOR_DOCUMENT_SCENE);
    fixture_free(&f);
}

static void scene_tabs_failed_behavior_discard_is_atomic()
{
    checkpoint_fixture f = fixture_new();
    assert(app_scene_authoring_open_behavior(f.scene, "ReloadBehavior") == 0);
    assert(editor_spawn(f.graph, registry_node_by_name("Integer"), 12, 34));
    app_scene_authoring_set_editor_working(f.scene, editor_behavior_asset(f.graph));
    assert(sync_behavior_working(f.scene) == 0);
    int nodes = acount(f.scene->definition->working->graph->nodes);
    behavior_asset *working = f.scene->definition->working;
    const behavior_asset *editor_asset = editor_behavior_asset(f.graph);
    scene_document_tab *tab = &f.scene->tabs[f.scene->active_tab];
    free(tab->asset_saved);
    tab->asset_saved = xstrdup("{}");
    assert(scene_tabs_discard(f.scene, f.scene->active_tab) == -1);
    assert(f.scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR);
    assert(f.scene->definition->working == working);
    assert(editor_behavior_asset(f.graph) == editor_asset);
    assert(acount(f.scene->definition->working->graph->nodes) == nodes);
    assert(f.scene->definition->dirty && f.scene->definition->unsaved);
    fixture_free(&f);
}

static const ecs_component_desc *system_resolve_component(const char *name, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (!strcmp(GAME_REGISTRY_COMPONENTS[i]->name, name)) {
            return GAME_REGISTRY_COMPONENTS[i];
        }
    }
    return NULL;
}

static const char *test_system_component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (GAME_REGISTRY_COMPONENTS[i]->id == id) {
            return GAME_REGISTRY_COMPONENTS[i]->name;
        }
    }
    return NULL;
}

static void write_system_fixture(const char *path, const char *name)
{
    game_system_document *document = game_system_document_new(name);
    assert(document);
    ecs_query_expr *query = ecs_query_expr_has(GAME_COMPONENT_GameTransform.id);
    assert(query && game_system_document_set_query(document, query) == 0);
    ecs_query_expr_free(query);
    char *json = game_system_document_save(document, system_resolve_component, test_system_component_name, NULL);
    assert(json && write_text_file_atomic(path, json) == 0);
    free(json);
    game_system_document_free(document);
}

static void scene_tabs_retain_system_state()
{
    checkpoint_fixture f = fixture_new();
    const char *first_path = "/tmp/chibi-39-system-a.yaml";
    const char *second_path = "/tmp/chibi-39-system-b.yaml";
    write_system_fixture(first_path, "SystemA");
    write_system_fixture(second_path, "SystemB");
    assert(app_scene_authoring_open_system(f.scene, first_path) == 0);
    int first = f.scene->active_tab;
    assert(editor_spawn(f.graph, registry_node_by_name("Integer"), 12, 34));
    ecs_query_expr *first_query = ecs_query_expr_has(GAME_COMPONENT_GameVelocity.id);
    assert(
        first_query &&
        game_system_document_set_query(f.scene->system_document, first_query) == 0
    );
    ecs_query_expr_free(first_query);
    int first_nodes = acount(editor_active_graph(f.graph)->nodes);
    assert(app_scene_authoring_open_system(f.scene, second_path) == 0);
    int second = f.scene->active_tab;
    assert(first != second);
    assert(editor_spawn(f.graph, registry_node_by_name("Integer"), 56, 78));
    ecs_query_expr *second_query = ecs_query_expr_has(
        GAME_COMPONENT_GameOrientation.id
    );
    assert(
        second_query &&
        game_system_document_set_query(f.scene->system_document, second_query) == 0
    );
    ecs_query_expr_free(second_query);
    int second_nodes = acount(editor_active_graph(f.graph)->nodes);
    assert(scene_tabs_activate(f.scene, first) == 0);
    assert(!strcmp(f.scene->system_path, first_path));
    assert(
        ecs_query_expr_get_component(game_system_document_query(f.scene->system_document)) ==
        GAME_COMPONENT_GameVelocity.id
    );
    assert(acount(editor_active_graph(f.graph)->nodes) == first_nodes);
    assert(
        f.scene->tabs[first].system_saved && f.scene->tabs[first].system_json &&
        strcmp(f.scene->tabs[first].system_saved, f.scene->tabs[first].system_json) != 0
    );
    editor_undo(f.graph);
    assert(acount(editor_active_graph(f.graph)->nodes) == first_nodes - 1);
    editor_redo(f.graph);
    assert(acount(editor_active_graph(f.graph)->nodes) == first_nodes);
    assert(scene_tabs_activate(f.scene, second) == 0);
    assert(!strcmp(f.scene->system_path, second_path));
    assert(
        ecs_query_expr_get_component(game_system_document_query(f.scene->system_document)) ==
        GAME_COMPONENT_GameOrientation.id
    );
    assert(acount(editor_active_graph(f.graph)->nodes) == second_nodes);
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static void checkpoint_restores_next_scene_key()
{
    checkpoint_fixture f = fixture_new();
    const char *first_path = "/tmp/chibi-39-key-a.yaml";
    const char *second_path = "/tmp/chibi-39-key-b.yaml";
    char *json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(first_path, json) == 0);
    free(json);
    assert(app_scene_authoring_load(f.scene, first_path) == 0);
    json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(second_path, json) == 0);
    free(json);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int first = -1;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        if (f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE &&
            f.scene->tabs[i].path && !strcmp(f.scene->tabs[i].path, first_path)) {
            first = i;
        }
    }
    assert(first >= 0);
    free(f.scene->tabs[first].key);
    f.scene->tabs[first].key = xstrdup("scene:scene-17");
    f.scene->next_scene_key = 2;
    f = roundtrip(&f);
    assert(f.scene->next_scene_key == 18);
    free(f.scene->scene_path);
    f.scene->scene_path = NULL;
    f.scene->tabs_transitioning = true;
    scene_tabs_document_selected(f.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    f.scene->tabs_transitioning = false;
    assert(f.scene->next_scene_key == 19);
    assert(!strcmp(f.scene->tabs[f.scene->active_tab].key, "scene:scene-18"));
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
}

static int indexed_asset(const asset_index *index, asset_index_kind kind, const char *name)
{
    int asset = asset_index_find_name(index, kind, name);
    assert(asset >= 0);
    return asset;
}

static const asset_index_reference *indexed_reference(
    const asset_index *index,
    int source,
    int target,
    asset_index_reference_kind kind
)
{
    for (int i = 0; i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->source == source && reference->target == target &&
            reference->kind == kind) {
            return reference;
        }
    }
    return NULL;
}

static void asset_references_follow_authored_edits()
{
    checkpoint_fixture f = fixture_new();
    assert(app_scene_authoring_set_content_root(f.scene, "content") == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    const asset_index *index = app_scene_authoring_asset_index(f.scene);
    int arena = asset_index_find(index, ASSET_INDEX_SCENE, "content/scenes/Arena3D.yaml", 0);
    assert(arena >= 0);
    char *arena_path = realpath("content/scenes/Arena3D.yaml", NULL);
    assert(arena_path && asset_index_find(index, ASSET_INDEX_SCENE, arena_path, 0) == arena);
    free(arena_path);
    int player = indexed_asset(index, ASSET_INDEX_PROTOTYPE, "ArenaPlayer");
    int material = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaPlayer");
    int health = indexed_asset(index, ASSET_INDEX_BEHAVIOR, "HealthBehavior");
    int damage = indexed_asset(index, ASSET_INDEX_EVENT, "Damage");
    assert(player != material);
    const asset_index_reference *binding = indexed_reference(index, player, material, ASSET_INDEX_COMPONENT_BINDING);
    assert(
        binding && !strcmp(binding->component, "GameShader") &&
        !strcmp(binding->field, "material")
    );
    assert(indexed_reference(index, player, health, ASSET_INDEX_BEHAVIOR_ATTACHMENT));
    const asset_index_reference *consumer = indexed_reference(index, health, damage, ASSET_INDEX_EVENT_CONSUMER);
    assert(consumer && consumer->node_id);
    graph *consumer_graph = behavior_asset_find_graph(
        game_live_find_definition(f.live, "HealthBehavior")->working,
        consumer->graph_id
    );
    assert(consumer_graph);
    node *consumer_node = graph_node_by_id(consumer_graph, consumer->node_id);
    assert(consumer_node && !strcmp(consumer_node->reference_name, "Damage"));
    int contact = indexed_asset(index, ASSET_INDEX_BEHAVIOR, "PlayerContactBehavior");
    const asset_index_reference *emitter = indexed_reference(index, contact, damage, ASSET_INDEX_EVENT_EMITTER);
    assert(emitter && emitter->node_id);
    graph *emitter_graph = behavior_asset_find_graph(
        game_live_find_definition(f.live, "PlayerContactBehavior")->working,
        emitter->graph_id
    );
    assert(emitter_graph);
    node *emitter_node = graph_node_by_id(emitter_graph, emitter->node_id);
    assert(emitter_node && !strcmp(emitter_node->reference_name, "Damage"));

    /* These graph edits have not reached live apply or an editor frame. */
    assert(app_scene_authoring_open_behavior(f.scene, "HealthBehavior") == 0);
    node *event = editor_spawn(f.graph, registry_node_by_name("Event"), 10, 20);
    assert(event);
    graph *working = (graph *)editor_active_graph(f.graph);
    assert(graph_set_node_reference(working, event, 0, "PendingReferenceEvent") == 0);
    uint64_t event_id = event->id;
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    health = indexed_asset(index, ASSET_INDEX_BEHAVIOR, "HealthBehavior");
    int pending = indexed_asset(index, ASSET_INDEX_EVENT, "PendingReferenceEvent");
    assert(asset_index_asset_at(index, pending)->unresolved);
    consumer = indexed_reference(index, health, pending, ASSET_INDEX_EVENT_CONSUMER);
    assert(
        consumer && consumer->node_id == event_id &&
        consumer->graph_id == working->graph_id
    );

    /* A second edit while already dirty must replace the old reference. */
    assert(graph_set_node_reference(working, event, 0, "UpdatedReferenceEvent") == 0);
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    health = indexed_asset(index, ASSET_INDEX_BEHAVIOR, "HealthBehavior");
    int updated = indexed_asset(index, ASSET_INDEX_EVENT, "UpdatedReferenceEvent");
    assert(asset_index_find_name(index, ASSET_INDEX_EVENT, "PendingReferenceEvent") < 0);
    consumer = indexed_reference(index, health, updated, ASSET_INDEX_EVENT_CONSUMER);
    assert(consumer && consumer->node_id == event_id);
    assert(graph_remove_node(working, event) == 0);
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    assert(asset_index_find_name(index, ASSET_INDEX_EVENT, "UpdatedReferenceEvent") < 0);

    assert(
        app_scene_authoring_set_prototype_field(
            f.scene,
            "ArenaPlayer",
            "GameShader",
            "material",
            (pin_value){ .type = PIN_STR, .str = "ArenaFloor" }
        ) == 0
    );
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    player = indexed_asset(index, ASSET_INDEX_PROTOTYPE, "ArenaPlayer");
    material = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaPlayer");
    int floor = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaFloor");
    assert(!indexed_reference(index, player, material, ASSET_INDEX_COMPONENT_BINDING));
    assert(indexed_reference(index, player, floor, ASSET_INDEX_COMPONENT_BINDING));

    assert(
        app_scene_authoring_register_prototype(
            f.scene,
            "{\"name\":\"ReferencePrototype\",\"components\":{"
            "\"GameTransform\":{},"
            "\"GameShader\":{\"material\":\"ArenaPlayer\"}}}",
            NULL
        ) == 0
    );
    scene_authoring_select_document(f.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    assert(app_scene_authoring_add_instance(f.scene, "ReferencePrototype") == 0);
    const char *first_path = "/tmp/chibi-40-references-a.yaml";
    const char *second_path = "/tmp/chibi-40-references-b.yaml";
    assert(app_scene_authoring_save(f.scene, first_path) == 0);
    int first_tab = f.scene->active_tab;
    char *json = scene_authoring_scene_json(f.scene);
    assert(json && write_text_file_atomic(second_path, json) == 0);
    free(json);
    assert(scene_tabs_open_scene(f.scene, second_path) == 0);
    int second_tab = f.scene->active_tab;
    const ecs_component_desc *shader = &GAME_COMPONENT_GameShader;
    int field = -1;
    for (int i = 0; i < shader->type->field_count; i++) {
        if (!strcmp(shader->type->fields[i].name, "material")) {
            field = i;
        }
    }
    assert(field >= 0 && acount(f.scene->actors) == 1);
    assert(
        ecs_component_write_field(
            f.world,
            f.scene->actors[0].entity,
            shader->id,
            field,
            (pin_value){ .type = PIN_STR, .str = "ArenaFloor" }
        ) == ECS_OK
    );
    f.scene->scene_dirty = true;
    asset_index_invalidate(f.scene);
    assert(
        !strcmp(GAME_GameShader_get_const(f.world, f.scene->actors[0].entity)->material, "ArenaFloor")
    );
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    assert(
        !strcmp(GAME_GameShader_get_const(f.world, f.scene->actors[0].entity)->material, "ArenaFloor")
    );
    int second_scene = asset_index_find(index, ASSET_INDEX_SCENE, second_path, 0);
    material = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaPlayer");
    floor = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaFloor");
    assert(second_scene >= 0);
    assert(!indexed_reference(index, second_scene, material, ASSET_INDEX_COMPONENT_BINDING));
    binding = indexed_reference(index, second_scene, floor, ASSET_INDEX_COMPONENT_BINDING);
    assert(binding && binding->actor && binding->actor[0]);

    assert(scene_tabs_activate(f.scene, first_tab) == 0);
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    int first_scene = asset_index_find(index, ASSET_INDEX_SCENE, first_path, 0);
    second_scene = asset_index_find(index, ASSET_INDEX_SCENE, second_path, 0);
    material = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaPlayer");
    floor = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaFloor");
    assert(first_scene >= 0 && second_scene >= 0 && first_scene != second_scene);
    assert(indexed_reference(index, first_scene, material, ASSET_INDEX_COMPONENT_BINDING));
    assert(indexed_reference(index, second_scene, floor, ASSET_INDEX_COMPONENT_BINDING));
    assert(!indexed_reference(index, second_scene, material, ASSET_INDEX_COMPONENT_BINDING));
    assert(app_scene_authoring_open_asset(f.scene, second_scene) == 0);
    assert(f.scene->active_tab == second_tab && f.scene->scene_dirty);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0);
    assert(
        ecs_component_write_field(
            f.world,
            f.scene->actors[0].entity,
            shader->id,
            field,
            (pin_value){ .type = PIN_STR, .str = "ArenaEnemy" }
        ) == ECS_OK
    );
    assert(app_scene_authoring_refresh_asset_index(f.scene) == 0);
    second_scene = asset_index_find(index, ASSET_INDEX_SCENE, second_path, 0);
    floor = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaFloor");
    int enemy = indexed_asset(index, ASSET_INDEX_MATERIAL, "ArenaEnemy");
    assert(indexed_reference(index, second_scene, floor, ASSET_INDEX_COMPONENT_BINDING));
    assert(!indexed_reference(index, second_scene, enemy, ASSET_INDEX_COMPONENT_BINDING));
    assert(
        !strcmp(GAME_GameShader_get_const(f.world, f.scene->actors[0].entity)->material, "ArenaEnemy")
    );
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0);
    assert(
        !strcmp(GAME_GameShader_get_const(f.world, f.scene->actors[0].entity)->material, "ArenaFloor")
    );
    remove(first_path);
    remove(second_path);
    fixture_free(&f);
    puts("asset references PASS: typed relationships, pending edits, component changes and retained scene overrides");
}

static void physics_settings_checkpoint()
{
    checkpoint_fixture f = fixture_new();
    game_physics_settings defaults = game_physics_configuration(f.live->physics);
    game_physics_settings settings = defaults;
    settings.workers = 8;
    settings.substeps = 2;
    settings.sleep = false;
    settings.continuous = false;
    settings.force_drive = true;
    settings.contact_hertz = 15;
    settings.contact_damping = 1;
    assert(app_scene_authoring_set_physics(f.scene, settings) == 0);
    assert(f.scene->scene_dirty);
    f = roundtrip(&f);
    assert(
        game_physics_settings_equal(settings, game_physics_configuration(f.live->physics))
    );
    assert(replay_scene_edit(f.scene, false) == 0);
    assert(
        game_physics_settings_equal(defaults, game_physics_configuration(f.live->physics))
    );
    f = roundtrip(&f);
    assert(replay_scene_edit(f.scene, true) == 0);
    assert(
        game_physics_settings_equal(settings, game_physics_configuration(f.live->physics))
    );
    game_physics_settings invalid = settings;
    invalid.substeps = 0;
    assert(app_scene_authoring_set_physics(f.scene, invalid) < 0);
    assert(
        game_physics_settings_equal(settings, game_physics_configuration(f.live->physics))
    );
    fixture_free(&f);
}

static int test_scene_checkpoint()
{
    physics_settings_checkpoint();
    asset_references_follow_authored_edits();
    shader_scene_load();
    scene_tabs_retain_authored_state();
    scene_tabs_save_as_keeps_identity();
    scene_tabs_failed_restore_rolls_back();
    scene_tabs_discard_reverts_shared_assets();
    opening_behaviors_keeps_them_saved();
    scene_tabs_close_underlying_scene_keeps_context();
    scene_tabs_reject_last_scene_close();
    scene_tabs_failed_behavior_discard_is_atomic();
    scene_tabs_retain_system_state();
    checkpoint_restores_next_scene_key();
    checkpoint_fixture fixture = fixture_new();
    assert(
        app_scene_authoring_register_prototype(fixture.scene, "{\"name\":\"ReloadPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(
        app_scene_authoring_set_prototype_field(
            fixture.scene,
            "ReloadPrototype",
            "GameTransform",
            "x",
            (pin_value){ .type = PIN_F32, .f = 7 }
        ) == 0
    );
    assert(app_scene_authoring_add_instance(fixture.scene, "ReloadPrototype") == 0);
    assert(app_scene_authoring_open_behavior(fixture.scene, "ReloadBehavior") == 0);
    assert(editor_spawn(fixture.graph, registry_node_by_name("Integer"), 12, 34));
    fixture.scene->camera = (scene_camera){ .view = { 10, 20, 300, 200 }, .saved = true };
    fixture = roundtrip(&fixture);
    assert(fixture.scene->scene_dirty && acount(fixture.scene->actors) == 1);
    assert(
        acount(fixture.scene->prototypes) == 1 && fixture.scene->prototypes[0].dirty
    );
    assert(fixture.scene->camera.saved && fixture.scene->camera.view.center_x == 10);
    assert(acount(editor_active_graph(fixture.graph)->nodes) == 1);
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == 7
    );
    editor_undo(fixture.graph);
    assert(acount(editor_active_graph(fixture.graph)->nodes) == 0);
    assert(app_scene_authoring_open_prototype(fixture.scene, "ReloadPrototype") == 0);
    assert(prototype_undo(fixture.scene) == 0);
    pin_value value;
    assert(
        app_scene_authoring_read_prototype_field(fixture.scene, "ReloadPrototype", "GameTransform", "x", &value) ==
            0 &&
        value.f == 1
    );
    scene_authoring_select_document(fixture.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    assert(replay_scene_edit(fixture.scene, false) == 0);
    assert(acount(fixture.scene->actors) == 0);
    assert(replay_scene_edit(fixture.scene, true) == 0);
    assert(acount(fixture.scene->actors) == 1);
    float authored_x = GAME_GameTransform_get_const(
                           fixture.world,
                           fixture.scene->actors[0].entity
    )
                           ->x;
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0
    );
    GAME_GameTransform_get(fixture.world, fixture.scene->actors[0].entity)->x = 999;
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE) == 0
    );
    fixture = roundtrip(&fixture);
    app_runtime_state state = app_scene_authoring_runtime_state(fixture.scene);
    assert(state.target == APP_RUNTIME_SCENE && !state.playing && fixture.scene->runtime_snapshot_valid);
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == authored_x
    );
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0
    );
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == authored_x
    );
    fixture_free(&fixture);
    puts("scene checkpoint PASS: unsaved graph/prototype/scene, Undo/Redo, camera, paused restart and authored pre-Play pose");
    return 0;
}

void test_scene_instances();

int scene_documents_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Scene document retention",
        0,
        0,
        0,
        256,
        256,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "scene document app: %s\n", result.details ? result.details : "unknown error");
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    int failed = test_scene_checkpoint();
    test_scene_instances();
    app_input_shutdown();
    cf_destroy_app();
    return failed;
}
