/* GameUi documents without a GPU: loading, model application and events. */
#include "assets/project.h"
#include "render/ui.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void click(render_ui *ui, float x, float y)
{
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        render_ui_input(ui, &(rml_input){ .type = types[i], .x = x, .y = y });
    }
}

/* Shows each ui asset under `content` in turn; false with a message when one
 * does not load cleanly, bindings included. */
static bool shipped_documents_load(
    ecs_world *world,
    ecs_entity entity,
    const char *project_root,
    char *failure,
    size_t size
)
{
    chibi_project project = { 0 };
    assert(chibi_project_open(&project, project_root, failure, size) == 0);
    char folder[1024];
    snprintf(folder, sizeof folder, "%s/ui", project.content);
    DIR *dir = opendir(folder);
    if (!dir) {
        chibi_project_free(&project);
        return true;
    }
    render_ui *ui = render_ui_new(project.content, NULL);
    ui_world w = { .ecs = world };
    bool ok = true;
    for (struct dirent *e; ok && (e = readdir(dir));) {
        char name[128];
        size_t length = strlen(e->d_name), extension = strlen(UI_MARKUP_EXTENSION);
        if (length <= extension ||
            strcmp(e->d_name + length - extension, UI_MARKUP_EXTENSION) ||
            length - extension >= sizeof name) {
            continue;
        }
        snprintf(name, sizeof name, "%.*s", (int)(length - extension), e->d_name);
        /* A root document, with its <body>, shows as the page. */
        char error[64];
        ui_document document = { 0 };
        bool page = ui_document_load(folder, name, NULL, &document, error, sizeof error) &&
                    document.body;
        ui_document_free(&document);
        /* Other documents show on the project's default page, as in a scene. */
        w.page = page ? name : project.ui;
        *GAME_GameUi_get(world, entity) = (GameUi){ .document = page ? "" : name };
        rml_backend_take_warning();
        render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
        render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
        const char *warning = rml_backend_take_warning();
        ok = !render_ui_error(ui)[0] && !warning[0];
        snprintf(failure, size, "ui/%s.rml: %s", name, warning[0] ? warning : render_ui_error(ui));
    }
    closedir(dir);
    render_ui_free(ui);
    chibi_project_free(&project);
    return ok;
}

static void shipped_documents()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameUi_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    static const char *const CONTENT[] = { ".",
                                           "projects/arena2d",
                                           "projects/SpaceCadet",
                                           "projects/studio" };
    char failure[256] = "";
    bool ok = true;
    for (size_t i = 0; ok && i < sizeof CONTENT / sizeof *CONTENT; i++) {
        ok = shipped_documents_load(world, entity, CONTENT[i], failure, sizeof failure);
    }
    if (!ok) {
        printf("  %s\n", failure);
    }
    test_check(ok, "every shipped ui asset and widget loads with its bindings");
    ecs_world_free(world);
}

/* How many of the warnings kept since rml_backend_keep_warnings are RmlUi's
 * about a data view whose element was destroyed. */
static int destroyed_views()
{
    char **warnings = rml_backend_take_warnings();
    int count = 0;
    for (int i = 0; i < acount(warnings); i++) {
        count += strstr(warnings[i], "was it destroyed") != NULL;
        free(warnings[i]);
    }
    afree(warnings);
    return count;
}

/* An overlay with bindings shown on a page from its first frame, as the
 * Canvas preview shows an asset, leaves RmlUi no view of an element it
 * removed, through edits to the asset and the page: the page loads before
 * the overlay installs. */
static void overlay_on_page(const char *root)
{
    char folder[96], page[128], bound[128];
    snprintf(folder, sizeof folder, "%s/ui", root);
    snprintf(page, sizeof page, "%s/Shell.rml", folder);
    snprintf(bound, sizeof bound, "%s/Bound.rml", folder);
    test_write_ui(folder, "Shell", "<body><slot/></body>", NULL);
    test_write_ui(folder, "Bound", "<div data-class-live=\"save.live\">{{ save.label }}</div>", NULL);
    ecs_world *world = ecs_world_new();
    render_ui *ui = render_ui_new(root, NULL);
    ui_world w = { .ecs = world, .page = "Shell" };
    rml_backend_keep_warnings();
    for (int i = 0; i < 3; i++) {
        render_ui_set_overlay(ui, "Bound", "dark");
        render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    }
    int first = destroyed_views();
    rml_backend_keep_warnings();
    text_overlay_set(bound, "<p data-class-live=\"save.live\">{{ save.label }}</p>");
    text_overlay_set(page, "<body class=\"edited\"><slot/></body>");
    for (int i = 0; i < 3; i++) {
        render_ui_set_overlay(ui, "Bound", "dark");
        render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    }
    test_check(
        !first && !destroyed_views() && !render_ui_error(ui)[0],
        "an overlay shown on a page and reinstalled after edits leaves no "
        "data view of a removed element"
    );
    text_overlay_set(bound, NULL);
    text_overlay_set(page, NULL);
    render_ui_free(ui);
    ecs_world_free(world);
    test_remove_ui(folder, "Shell");
    test_remove_ui(folder, "Bound");
}

void test_game_ui_presenter()
{
    shipped_documents();
    char root[] = "/tmp/chibi-game-ui-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(mkdir(folder, 0700) == 0);
    test_write_ui(
        folder,
        "Menu",
        "<div id=\"resume\" data-event-click=\"emit('Resume')\">Resume</div>\n",
        "#resume { display: block; width: 200dp; height: 60dp; }\n"
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
                   .save = game_behavior_save(live->behaviors) };
    render_ui *ui = render_ui_new(root, NULL);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(!render_ui_error(ui)[0], "a GameUi document loads from content/ui");

    int before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "clicking an element that emits an event queues it for the entity"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 900, 600);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(game_entities_pending_events(live->entities) == before, "clicking outside every emitting element sends nothing");

    test_write_ui(folder, "Menu", "", NULL);
    render_ui_update(ui, &w, 1280, 720, 1.1);
    test_check(
        strstr(render_ui_error(ui), "ui/Menu.rml") != NULL,
        "an invalid edit reports its asset and keeps the last valid document"
    );
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        game_entities_pending_events(live->entities) == before + 1,
        "the last valid document keeps working after an invalid edit"
    );

    char event[32];
    test_write_ui(
        folder,
        "Pause",
        "<div id=\"go\" data-event-click=\"emit('Resume')\">Go</div>\n",
        "body { pointer-events: auto; } #go { display: block; width: 200dp; "
        "height: 60dp; } body.done #go { display: none; }\n"
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
    test_remove_ui(folder, "Pause");

    ecs_world_destroy_entity(world, menu);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_ui_set_text(model, ecs_entity_to_u64(menu), "resume", "Go");
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(game_ui_element_count(model) == 0, "removing a GameUi forgets its elements");

    test_write_ui(
        folder,
        "Tags",
        "<div class=\"tag\" data-event-click=\"emit('A')\">A</div><div "
        "class=\"tag\" "
        "data-event-click=\"emit('B')\">B</div>\n",
        ".tag { display: block; height: 60dp; }\n"
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
    test_remove_ui(folder, "Tags");

    test_write_ui(folder, "Host", "<use ui=\"Button\" id=\"go\">Go</use>\n", NULL);
    test_write_ui(
        folder,
        "Button",
        "<div class=\"button\" data-event-click=\"emit('Go')\"><slot/></div>\n",
        ".button { display: block; height: 60dp; }\n"
    );
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Host" };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    bool used = !render_ui_error(ui)[0] &&
                game_entities_pending_events(live->entities) == before + 1;
    test_write_ui(folder, "Button", "<div class=\"button\"><slot/></div>\n", ".button { display: block; height: 60dp; }\n");
    render_ui_update(ui, &w, 1280, 720, 1.1);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(used && game_entities_pending_events(live->entities) == before, "a widget's events reach its host's entity, and editing it reloads the host");
    test_remove_ui(folder, "Host");
    test_remove_ui(folder, "Button");

    test_write_ui(
        folder,
        "Options",
        "<input type=\"checkbox\" id=\"sound\" data-checked=\"save.sound\"/>"
        "<input type=\"radio\" id=\"loud\" data-checked=\"save.volume\" "
        "value=\"loud\"/>\n",
        "input { display: block; width: 40dp; height: 40dp; }\n"
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
    test_check(unchecked && !strcmp(game_save_get(settings, "sound"), "false"), "a checkbox bound to a saved key shows its value and saves the player's change");
    click(ui, 20, 60);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    const char *volume = game_save_get(settings, "volume");
    test_check(volume && !strcmp(volume, "loud"), "a radio bound to a key nothing saved yet saves its value when chosen");
    game_save_set(settings, "volume", "quiet");
    click(ui, 20, 20);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        !strcmp(game_save_get(settings, "sound"), "true") &&
            !strcmp(game_save_get(settings, "volume"), "quiet"),
        "a binding saves only the key it changed, so a key saved in the same "
        "frame keeps its value"
    );
    test_remove_ui(folder, "Options");

    test_write_ui(
        folder,
        "Keys",
        "<div data-keys=\"Enter:Choose\"><div id=\"row\" autofocus "
        "data-event-click=\"emit('Row')\">Row</div></div>\n",
        "#row { display: block; height: 40dp; tab-index: auto; }\n"
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
    test_remove_ui(folder, "Keys");

    /* A page's RCSS styles the documents in its slot, unscoped. */
    test_write_ui(
        folder,
        "Page",
        "<body class=\"page\"><div class=\"bar\"/><slot/></body>",
        ".page .tall { display: block; height: 80dp; }"
    );
    test_write_ui(folder, "Tall", "<div class=\"tall\" data-event-click=\"emit('Hit')\">Hit</div>\n", NULL);
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Tall" };
    w.page = "Page";
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    before = game_entities_pending_events(live->entities);
    click(ui, 40, 60);
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(
        !render_ui_error(ui)[0] &&
            game_entities_pending_events(live->entities) == before + 1,
        "a scene's root document styles the documents shown on it"
    );
    w.page = NULL;
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_remove_ui(folder, "Page");
    test_remove_ui(folder, "Tall");

    test_write_ui(folder, "Broken", "<div data-event-click=\"emit('Go'\">Go</div>\n", NULL);
    *GAME_GameUi_get(world, strip) = (GameUi){ .document = "Broken" };
    render_ui_update(ui, &w, 1280, 720, 1.0 / 60);
    test_check(strstr(render_ui_error(ui), "ui/Broken.rml") != NULL, "a binding RmlUi rejects is the asset's error");
    test_remove_ui(folder, "Broken");

    test_write_ui(
        folder,
        "Panel",
        "<div id=\"go\" data-event-click=\"emit('Go')\">Go</div>\n",
        "#go { display: block; width: 200dp; height: 60dp; }\n"
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
    test_remove_ui(folder, "Panel");
    overlay_on_page(root);

    render_ui_free(ui);
    game_live_world_free(live);
    ecs_world_free(world);
    test_remove_ui(folder, "Menu");
    rmdir(folder);
    rmdir(root);
}
