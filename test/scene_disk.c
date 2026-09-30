#include "editor/workspace/scene_internal.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "test/support/report.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Another program edits a project the editor has open. */
typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
    char root[64];
    double now;
} disk_fixture;

static const char
    DOT[] = "name: Dot\ncomponents:\n  GAME.GameTransform: {x: %s}\n";
static const char PULSE
    [] = "version: 3\nname: Pulse\nid_counter: 10\nowner_type: ecs.entity\n"
         "graphs:\n- kind: event\n  name: \"\"\n  nodes:\n"
         "  - {id: 1, type: Event, reference_name: Tick}\n%s";

static void path_to(const disk_fixture *f, const char *name, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", f->root, name);
}

static void write_asset(const disk_fixture *f, const char *name, const char *format, const char *value)
{
    char path[128];
    path_to(f, name, path, sizeof path);
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(file, format, value);
    fclose(file);
}

static disk_fixture fixture_new()
{
    disk_fixture f = { .world = ecs_world_new() };
    snprintf(f.root, sizeof f.root, "/tmp/chibi-disk-XXXXXX");
    assert(mkdtemp(f.root));
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    const char *folders[] = { "behaviors", "prototypes", "scenes" };
    for (int i = 0; i < 3; i++) {
        char folder[128];
        path_to(&f, folders[i], folder, sizeof folder);
        assert(mkdir(folder, 0755) == 0);
    }
    write_asset(&f, "prototypes/Dot.yaml", DOT, "1.0");
    write_asset(
        &f,
        "scenes/Main.yaml",
        "version: 1\nname: Main\nentities:\n%s",
        "- {name: Dot_1, prototype: Dot}\n- {name: Dot_2, prototype: Dot}\n"
    );
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new("Editing", behavior_asset_new("Editing"));
    assert(definition && game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("Editing"));
    f.runtime = engine_new(f.world, f.live);
    f.scene = app_scene_authoring_new(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        NULL,
        NULL,
        f.runtime
    );
    char scene_path[128];
    path_to(&f, "scenes/Main.yaml", scene_path, sizeof scene_path);
    assert(app_scene_authoring_set_content_root(f.scene, f.root) == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, scene_path) == 0);
    assert(!app_scene_authoring_sync_disk(f.scene, f.now));
    return f;
}

static void fixture_free(disk_fixture *f)
{
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    const char *files[] = { "prototypes/Dot.yaml", "behaviors/Pulse.yaml",
                            "scenes/Main.yaml",    "prototypes",
                            "behaviors",           "scenes" };
    for (int i = 0; i < 6; i++) {
        char path[128];
        path_to(f, files[i], path, sizeof path);
        remove(path);
    }
    rmdir(f->root);
}

/* The one change the next poll finds, a second later. */
static app_disk_result poll_disk(disk_fixture *f, const char *name)
{
    f->now += 2;
    app_disk_change *changes = app_scene_authoring_sync_disk(f->scene, f->now);
    app_disk_result result = acount(changes) == 1 ? changes[0].result : APP_DISK_NONE;
    assert(!changes || (acount(changes) == 1 && strstr(changes[0].path, name)));
    app_disk_changes_free(changes);
    return result;
}

static float dot_x(disk_fixture *f, int actor)
{
    return GAME_GameTransform_get_const(f->world, f->scene->actors[actor].entity)
        ->x;
}

static void set_dot_x(disk_fixture *f, float x)
{
    pin_value value = { .type = PIN_F32, .f = x };
    assert(
        app_scene_authoring_set_prototype_field(f->scene, "Dot", "GameTransform", "x", value) == 0
    );
}

static void prototypes_follow_disk_unless_edited()
{
    disk_fixture f = fixture_new();
    char path[128];
    path_to(&f, "prototypes/Dot.yaml", path, sizeof path);
    write_asset(&f, "prototypes/Dot.yaml", DOT, "5.0");
    assert(
        poll_disk(&f, "Dot.yaml") == APP_DISK_RELOADED && dot_x(&f, 0) == 5 &&
        dot_x(&f, 1) == 5
    );

    set_dot_x(&f, 7);
    assert(app_scene_authoring_save_prototype(f.scene, "Dot", path) == 0);
    assert(poll_disk(&f, "Dot.yaml") == APP_DISK_NONE && dot_x(&f, 0) == 7);

    set_dot_x(&f, 9);
    write_asset(&f, "prototypes/Dot.yaml", DOT, "3.0");
    assert(poll_disk(&f, "Dot.yaml") == APP_DISK_UNSAVED && dot_x(&f, 0) == 9);
    assert(
        app_scene_authoring_reload_path(f.scene, path) == APP_DISK_RELOADED &&
        dot_x(&f, 0) == 3
    );

    write_asset(&f, "prototypes/Dot.yaml", "%s", "{");
    assert(poll_disk(&f, "Dot.yaml") == APP_DISK_FAILED && dot_x(&f, 0) == 3);
    fixture_free(&f);
}

static void behaviors_appear_and_reload()
{
    disk_fixture f = fixture_new();
    write_asset(&f, "behaviors/Pulse.yaml", PULSE, "");
    assert(poll_disk(&f, "Pulse.yaml") == APP_DISK_RELOADED);
    game_live_definition *pulse = game_live_find_definition(f.live, "Pulse");
    assert(pulse && acount(pulse->working->graph->nodes) == 1);
    write_asset(&f, "behaviors/Pulse.yaml", PULSE, "  - {id: 9, type: Integer}\n");
    assert(poll_disk(&f, "Pulse.yaml") == APP_DISK_RELOADED);
    assert(acount(game_live_find_definition(f.live, "Pulse")->working->graph->nodes) == 2);
    fixture_free(&f);
}

static void the_scene_reloads_after_the_runtime_stops()
{
    disk_fixture f = fixture_new();
    select_instance(f.scene, 1);
    f.scene->camera = (scene_camera){ .view = { 10, 20, 300, 200 }, .saved = true };
    f.scene->runtime_snapshot_valid = true;
    write_asset(
        &f,
        "scenes/Main.yaml",
        "version: 1\nname: Main\nentities:\n%s",
        "- {name: Dot_2, prototype: Dot}\n- {name: Dot_3, prototype: Dot}\n"
    );
    assert(poll_disk(&f, "Main.yaml") == APP_DISK_NONE && acount(f.scene->actors) == 2);
    f.scene->runtime_snapshot_valid = false;
    assert(poll_disk(&f, "Main.yaml") == APP_DISK_RELOADED);
    assert(!strcmp(f.scene->actors[1].name, "Dot_3"));
    assert(!strcmp(selected_actor(f.scene)->name, "Dot_2"));
    assert(f.scene->camera.saved && f.scene->camera.view.center_x == 10);
    fixture_free(&f);
}

void test_scene_disk()
{
    prototypes_follow_disk_unless_edited();
    behaviors_appear_and_reload();
    the_scene_reloads_after_the_runtime_stops();
    test_note(
        "scene disk PASS: external edits reload prototypes, behaviors and the "
        "scene; own saves, unsaved edits and "
        "a running scene wait\n"
    );
}
