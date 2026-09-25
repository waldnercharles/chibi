/* GameUi documents without a GPU: loading, model application and events. */
#include "render/ui.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static void click(render_ui *ui, float x, float y)
{
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        render_ui_input(ui, &(rml_input){ .type = types[i], .x = x, .y = y });
    }
}

void test_game_ui_presenter()
{
    char root[] = "/tmp/chibi-game-ui-XXXXXX", folder[64], path[96];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    snprintf(path, sizeof path, "%s/Menu.yaml", folder);
    write_file(
        path,
        "rml: |\n"
        "  <div id=\"resume\" data-event=\"Resume\">Resume</div>\n"
        "rcss: '#resume { display: block; width: 200dp; height: 60dp; }'\n"
    );
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity menu = ecs_world_create_entity(world);
    assert(GAME_GameUi_add(world, menu, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameUi_get(world, menu) = (GameUi){ .document = "Menu" };
    game_ui *model = game_behavior_ui(live->behaviors);
    render_ui *ui = render_ui_new(root);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(!render_ui_error(ui)[0], "a GameUi document loads from content/ui");

    int before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "clicking a data-event element queues its event for the entity"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 900, 600);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(game_entities_pending_events(live->entities) == before, "clicking outside every data-event element sends nothing");

    write_file(path, "rml: ''\n");
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.1);
    test_check(
        strstr(render_ui_error(ui), "ui/Menu.yaml") != NULL,
        "an invalid edit reports its asset and keeps the last valid document"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "the last valid document keeps working after an invalid edit"
    );

    char overlay[96], event[32];
    snprintf(overlay, sizeof overlay, "%s/Pause.yaml", folder);
    write_file(
        overlay,
        "rml: <div id=\"go\" data-event=\"Resume\">Go</div>\n"
        "rcss: '#go { display: block; width: 200dp; height: 60dp; } .done #go "
        "{ display: none; }'\n"
    );
    before = game_entities_pending_events(live->entities);
    render_ui_set_overlay(ui, "Pause", "paused");
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    bool taken = render_ui_take_overlay_event(ui, event, sizeof event) &&
                 !strcmp(event, "Resume") &&
                 !render_ui_take_overlay_event(ui, event, sizeof event);
    test_check(taken && game_entities_pending_events(live->entities) == before, "an overlay activation waits for its host and covers the world's documents");
    render_ui_set_overlay(ui, "Pause", "done");
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(!render_ui_take_overlay_event(ui, event, sizeof event), "the overlay state is its body class");
    render_ui_set_overlay(ui, "Pause", NULL);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "a hidden overlay lets clicks reach the world's documents"
    );
    unlink(overlay);

    ecs_world_destroy_entity(world, menu);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_ui_set_text(model, ecs_entity_to_u64(menu), "resume", "Go");
    render_ui_update(ui, world, model, live->entities, 1280, 720, 1.0 / 60);
    test_check(game_ui_element_count(model) == 0, "removing a GameUi forgets its elements");

    render_ui_free(ui);
    game_live_world_free(live);
    ecs_world_free(world);
    unlink(path);
    rmdir(folder);
    rmdir(root);
}
