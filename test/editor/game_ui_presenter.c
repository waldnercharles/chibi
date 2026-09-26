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
    ui_world w = { .ecs = world,
                   .model = model,
                   .entities = live->entities,
                   .settings = game_behavior_save(live->behaviors) };
    render_ui *ui = render_ui_new(root);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(!render_ui_error(ui)[0], "a GameUi document loads from content/ui");

    int before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "clicking a data-event element queues its event for the entity"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 900, 600);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(game_entities_pending_events(live->entities) == before, "clicking outside every data-event element sends nothing");

    write_file(path, "rml: ''\n");
    render_ui_update(ui, &w, 1280, 720, 1.1);
    test_check(
        strstr(render_ui_error(ui), "ui/Menu.yaml") != NULL,
        "an invalid edit reports its asset and keeps the last valid document"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "the last valid document keeps working after an invalid edit"
    );

    char overlay[96], event[32];
    snprintf(overlay, sizeof overlay, "%s/Pause.yaml", folder);
    write_file(
        overlay,
        "rml: <div id=\"go\" data-event=\"Resume\">Go</div>\n"
        "rcss: 'body { pointer-events: auto; } #go { display: block; width: "
        "200dp; height: "
        "60dp; } body.done #go { display: none; }'\n"
    );
    before = game_entities_pending_events(live->entities);
    render_ui_set_overlay(ui, "Pause", "paused");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool taken = render_ui_take_overlay_event(ui, event, sizeof event) &&
                 !strcmp(event, "Resume") &&
                 !render_ui_take_overlay_event(ui, event, sizeof event);
    test_check(taken && game_entities_pending_events(live->entities) == before, "an overlay activation waits for its host and covers the world's documents");
    render_ui_set_overlay(ui, "Pause", "done");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(!render_ui_take_overlay_event(ui, event, sizeof event), "the overlay state is its body class");
    render_ui_set_overlay(ui, "Pause", NULL);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "a hidden overlay lets clicks reach the world's documents"
    );
    unlink(overlay);

    ecs_world_destroy_entity(world, menu);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_ui_set_text(model, ecs_entity_to_u64(menu), "resume", "Go");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(game_ui_element_count(model) == 0, "removing a GameUi forgets its elements");

    char tags[96];
    snprintf(tags, sizeof tags, "%s/Tags.yaml", folder);
    write_file(
        tags,
        "rml: <div class=\"tag\" data-event=\"A\">A</div><div class=\"tag\" "
        "data-event=\"B\">B</div>\n"
        "rcss: '.tag { display: block; height: 60dp; }'\n"
    );
    ecs_entity strip = ecs_world_create_entity(world);
    assert(GAME_GameUi_add(world, strip, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Tags" };
    game_ui_set_visible(model, ecs_entity_to_u64(strip), ".tag", false);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    click(ui, 40, 90);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool hidden = game_entities_pending_events(live->entities) == before;
    game_ui_set_visible(model, ecs_entity_to_u64(strip), ".tag", true);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    click(ui, 40, 90);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        hidden && game_entities_pending_events(live->entities) == before + 2,
        "a selector applies a setting to every element it matches"
    );
    unlink(tags);

    char host[96], button[96];
    snprintf(host, sizeof host, "%s/Host.yaml", folder);
    snprintf(button, sizeof button, "%s/Button.yaml", folder);
    write_file(host, "rml: <use ui=\"Button\" id=\"go\">Go</use>\n");
    write_file(
        button,
        "rml: <div class=\"button\" data-event=\"Go\"><slot/></div>\n"
        "rcss: '.button { display: block; height: 60dp; }'\n"
    );
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Host" };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool used = !render_ui_error(ui)[0] &&
                game_entities_pending_events(live->entities) == before + 1;
    write_file(button, "rml: <div class=\"button\"><slot/></div>\n");
    render_ui_update(ui, &w, 1280, 720, 1.1);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(used && game_entities_pending_events(live->entities) == before, "a widget's events reach its host's entity, and editing it reloads the host");
    unlink(host);
    unlink(button);

    char options[96];
    snprintf(options, sizeof options, "%s/Options.yaml", folder);
    write_file(
        options,
        "rml: <input type=\"checkbox\" id=\"sound\" data-setting=\"sound\"/>\n"
        "rcss: '#sound { display: block; width: 40dp; height: 40dp; }'\n"
    );
    game_save *settings = game_behavior_save(live->behaviors);
    game_save_set(settings, "sound", "true");
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Options" };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 20, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool unchecked = !strcmp(game_save_get(settings, "sound"), "false");
    game_save_set(settings, "sound", "true");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 20, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(unchecked && !strcmp(game_save_get(settings, "sound"), "false"), "a data-setting checkbox shows its saved value and saves the player's change");
    unlink(options);

    char keys[96];
    snprintf(keys, sizeof keys, "%s/Keys.yaml", folder);
    write_file(
        keys,
        "rml: <div data-keys=\"Enter:Choose\"><div id=\"row\" autofocus "
        "data-event=\"Row\">Row</div></div>\n"
        "rcss: '#row { display: block; height: 40dp; tab-index: auto; }'\n"
    );
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Keys" };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    render_ui_input(ui, &(rml_input){ .type = RML_INPUT_KEY_PRESS, .key = RML_INPUT_KEY_ENTER });
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool keyed = game_entities_pending_events(live->entities) == before + 1;
    game_ui_click(model, ecs_entity_to_u64(strip), "#row");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(keyed && game_entities_pending_events(live->entities) == before + 2, "data-keys sends an event from the autofocused element, and Click UI Element clicks");
    unlink(keys);

    char panel[96];
    snprintf(panel, sizeof panel, "%s/Panel.yaml", folder);
    write_file(
        panel,
        "rml: <div id=\"go\" data-event=\"Go\">Go</div>\n"
        "rcss: '#go { display: block; width: 200dp; height: 60dp; }'\n"
    );
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Panel", .hidden = true };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool unseen = game_entities_pending_events(live->entities) == before;
    w.revealed = ecs_entity_to_u64(strip);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        unseen && game_entities_pending_events(live->entities) == before + 1,
        "a hidden GameUi shows while the editor reveals its entity"
    );
    w.revealed = 0;
    unlink(panel);

    render_ui_free(ui);
    game_live_world_free(live);
    ecs_world_free(world);
    unlink(path);
    rmdir(folder);
    rmdir(root);
}
