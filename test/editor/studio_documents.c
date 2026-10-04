/* Studio frames over a project on disk, in a hidden window: the fixtures and
 * input every surface's checks share, and the runner. Each surface's checks
 * live in studio_documents_<surface>.c (studio_documents.h). */
#include "test/editor/studio_documents.h"

#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"
#include "ui/input_feel.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Studio over scene `scene` of the project at `project` with `behavior`
   open. */
studio_fixture fixture_open(const char *project, const char *scene, const char *behavior)
{
    studio_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new("Editing", behavior_asset_new("Editing"));
    assert(definition && game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("Editing"));
    f.runtime = engine_new(f.world, f.live);
    f.viewport = engine_viewport_new(640, 360);
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
    char content[PATH_MAX];
    snprintf(content, sizeof content, "%s/content", project);
    assert(app_scene_authoring_set_content_root(f.scene, content) == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, scene) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, behavior) == 0);
    return f;
}

studio_fixture fixture_new()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Main.kdl", "Weapons");
    assert(editor_open_graph(f.graph, FUNCTION_GRAPH) == 0);
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    studio_maximize_graph(f.studio);
    return f;
}

void fixture_free(studio_fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
}

/* One application frame. File > Reload runs inside Studio's frame, before the
   canvas draws, so a reload lands between the scene and Studio frames. */
void frame_delta(studio_fixture *f, bool reload, double delta_seconds)
{
    f->time += delta_seconds;
    cf_app_update(NULL);
    app_input_frame();
    editor_tick(f->graph, f->time);
    assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
    if (reload) {
        assert(app_scene_authoring_reload(f->scene) == 0);
    }
    studio_frame(f->studio, delta_seconds);
    cf_app_draw_onto_screen(true);
}

void frame(studio_fixture *f, bool reload)
{
    frame_delta(f, reload, FRAME_SECONDS);
}

void graph_drag_button(const studio *s, float x, float y, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP };
    event.button.windowID = SDL_GetWindowID(cf_app_get_window());
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x * s->prefs.ui_scale;
    event.button.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

/* Studio over `project`'s scene `scene` with `behavior` open and the Scene
   filling the workspace. Preferences stay as saved but for the layout: UI
   Scale 1 and the default split modes, so the areas the checks click are the
   same whatever the user's. */
studio_fixture scene_open(const char *project, const char *scene, const char *behavior)
{
    studio_fixture f = fixture_open(project, scene, behavior);
    f.studio = studio_new(f.scene, f.viewport, project);
    studio_prefs defaults = studio_prefs_default();
    studio_prefs *p = &f.studio->prefs;
    p->ui_scale = 1;
    p->workspace = defaults.workspace;
    memcpy(p->layouts, defaults.layouts, sizeof defaults.layouts);
    f.studio->applied = *p;
    f.studio->maximized = STUDIO_AREA_TOP;
    return f;
}

/* Studio over the Vault scene, whose Vault shows the Bank document and
   attaches Teller. */
studio_fixture vault_open()
{
    return scene_open(PROJECT, VAULT, "Teller");
}

void frames(studio_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        frame(f, false);
        /* The user's preferences stay as they are. */
        f->studio->prefs_changed_at = 0;
    }
}

/* Studio over the Studio project's scene, whose root document is its
   manifest's, as the application opens it. */
studio_fixture studio_project_open()
{
    static char root[PATH_MAX];
    char scene[PATH_MAX + 32];
    assert(realpath("projects/studio", root));
    snprintf(scene, sizeof scene, "%s/content/scenes/Studio.kdl", root);
    studio_fixture f = scene_open(root, scene, "Dismiss");
    app_scene_authoring_set_default_ui(f.scene, "StudioShell");
    return f;
}

void key_event_mod(SDL_Scancode key, bool down, SDL_Keymod mod, bool repeat)
{
    SDL_Event event = { .type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP };
    event.key.windowID = SDL_GetWindowID(cf_app_get_window());
    event.key.scancode = key;
    event.key.key = SDL_GetKeyFromScancode(key, mod, true);
    event.key.mod = mod;
    event.key.down = down;
    event.key.repeat = repeat;
    assert(SDL_PushEvent(&event));
}

void key_event(SDL_Scancode key, bool down)
{
    key_event_mod(key, down, SDL_KMOD_NONE, false);
}

/* The first element in Studio's documents that `selector` matches. */
rml_element *studio_element(studio *s, const char *selector)
{
    rml_context *context = rml_backend_context(s->backend);
    rml_element *found = NULL;
    for (int i = 0; !found && i < rml_context_document_count(context); i++) {
        found = rml_element_query_selector(rml_context_document(context, i), selector);
    }
    return found;
}

/* The rows data-for made in the element at `selector`, leaving out its
   template, and the first one's text. */
int list_rows(studio *s, const char *selector, char *first, size_t size)
{
    first[0] = '\0';
    rml_element *window = studio_element(s, selector);
    int rows = 0;
    for (int i = 0; window && i < rml_element_child_count(window); i++) {
        rml_element *row = rml_element_child(window, i);
        if (rml_element_has_attribute(row, "data-for") ||
            !rml_element_is_visible(row, false)) {
            continue;
        }
        if (!rows++) {
            rml_element_inner_rml(row, first, size);
        }
    }
    return rows;
}

/* The elements of Studio's `panel` that `selector` matches, up to 8. */
int panel_elements(studio *s, const char *panel, const char *selector, rml_element **found)
{
    char query[128];
    snprintf(query, sizeof query, "[data-ui=%s] %s", panel, selector);
    return test_query_all(rml_backend_context(s->backend), query, found, 8);
}

/* A press and release on `el`, which focuses it as a click does. Input is
   in dp, and the element's box in pixels. */
void press(studio *s, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    rml_input input = { .x = (at.x + size.x * 0.5f) / ratio,
                        .y = (at.y + size.y * 0.5f) / ratio };
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        input.type = types[i];
        rml_backend_process_input(s->backend, &input);
    }
}

int tab_named(const app_scene_authoring *scene, const char *label)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].label && !strcmp(scene->tabs[i].label, label)) {
            return i;
        }
    }
    return -1;
}

void text_event(const char *text)
{
    SDL_Event event = { .type = SDL_EVENT_TEXT_INPUT };
    event.text.windowID = SDL_GetWindowID(cf_app_get_window());
    event.text.text = text;
    assert(SDL_PushEvent(&event));
}

void tap(SDL_Scancode key)
{
    key_event(key, true);
    key_event(key, false);
}

/* The pointer to (x, y) in dp, as SDL reports it in window points. */
void mouse_move(const studio *s, float x, float y)
{
    SDL_Event move = { .type = SDL_EVENT_MOUSE_MOTION };
    move.motion.windowID = SDL_GetWindowID(cf_app_get_window());
    move.motion.x = x * s->prefs.ui_scale;
    move.motion.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&move));
}

/* Studio over the Vault scene with the Outliner and Properties beside the
   Scene, whatever layout was saved. */
studio_fixture side_open()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    s->focus_area = STUDIO_AREA_OUTLINER;
    frames(&f, 3);
    return f;
}

/* UI documents as tabs and in the Text Editor (test/editor/ui_documents.c). */
void test_ui_document_tabs();
/* Assets ▸ New and Settings ▸ Project (test/editor/studio_new_assets.c). */
void test_studio_new_assets();
/* Workstream I: the Text Editor's code intelligence
   (test/editor/code_intelligence_studio.c). */
void test_code_intelligence_studio();

/* Which checks a run makes. */
typedef enum
{
    RUN_ALL,
    RUN_PROPERTIES, /* the fields alone (--studio-properties) */
    RUN_LISTS,      /* lists and drag alone (--studio-lists) */
    RUN_OUTLINER,   /* the Outliner alone (--studio-outliner) */
    RUN_OUTPUT,     /* Output alone (--studio-output) */
    RUN_MEMBERS,    /* Members alone (--studio-members) */
    RUN_SHELL,      /* the shell alone (--studio-shell) */
    RUN_SCENE,      /* the Scene area alone (--studio-scene) */
    RUN_CHROME,     /* the editor chrome alone (--studio-chrome) */
    RUN_PANELS,     /* the authored panel checks alone (--studio-panels) */
    RUN_INSPECTOR,  /* the UI Inspector alone (--studio-inspector) */
    RUN_PALETTE,    /* the palette and pickers alone (--studio-palette) */
    RUN_GRAPH_CHROME, /* graph menus, Node and Member tabs (--studio-graph-chrome) */
} studio_run;

static int studio_documents_app(const char *program, studio_run run)
{
    CF_Result result = cf_make_app(
        "Studio documents",
        0,
        0,
        0,
        1280,
        800,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Studio documents app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    switch (run) {
        case RUN_PROPERTIES: studio_documents_property_field_checks(); break;
        case RUN_LISTS: studio_documents_list_checks(); break;
        case RUN_OUTLINER: studio_documents_outliner_checks(); break;
        case RUN_OUTPUT: studio_documents_output_checks(); break;
        case RUN_MEMBERS: studio_documents_members_checks(); break;
        case RUN_SHELL: studio_documents_shell_checks(); break;
        case RUN_SCENE: studio_documents_scene_checks(); break;
        case RUN_CHROME: studio_documents_chrome_checks(); break;
        case RUN_PANELS: studio_documents_panel_checks(); break;
        case RUN_INSPECTOR: studio_documents_inspector_checks(); break;
        case RUN_PALETTE: studio_documents_palette_checks(); break;
        case RUN_GRAPH_CHROME: studio_documents_graph_chrome_checks(); break;
        case RUN_ALL:
            studio_documents_graph_checks();
            test_ui_document_tabs();
            test_studio_new_assets();
            test_code_intelligence_studio();
            studio_documents_panel_checks();
            studio_documents_shell_checks();
            studio_documents_assets_checks();
            studio_documents_outliner_checks();
            studio_documents_properties_checks();
            studio_documents_list_checks();
            studio_documents_output_checks();
            studio_documents_members_checks();
            studio_documents_scene_checks();
            studio_documents_chrome_checks();
            studio_documents_inspector_checks();
            studio_documents_palette_checks();
            studio_documents_graph_chrome_checks();
            break;
    }
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}

int studio_documents_run(const char *program)
{
    return studio_documents_app(program, RUN_ALL);
}

int studio_properties_run(const char *program)
{
    return studio_documents_app(program, RUN_PROPERTIES);
}

/* Lists and drag (workstream F4) alone. */
int studio_lists_run(const char *program)
{
    return studio_documents_app(program, RUN_LISTS);
}

/* The Outliner (workstream P2) alone. */
int studio_outliner_run(const char *program)
{
    return studio_documents_app(program, RUN_OUTLINER);
}

/* Output (workstream P4) alone. */
int studio_output_run(const char *program)
{
    return studio_documents_app(program, RUN_OUTPUT);
}

/* Members (workstream P5) alone. */
int studio_members_run(const char *program)
{
    return studio_documents_app(program, RUN_MEMBERS);
}

/* The shell (workstream C3) alone. */
int studio_shell_run(const char *program)
{
    return studio_documents_app(program, RUN_SHELL);
}

/* The Scene area (workstream C1) alone. */
int studio_scene_run(const char *program)
{
    return studio_documents_app(program, RUN_SCENE);
}

/* The editor chrome (workstream C2) alone. */
int studio_chrome_run(const char *program)
{
    return studio_documents_app(program, RUN_CHROME);
}

/* The authored panel checks alone. */
int studio_panels_run(const char *program)
{
    return studio_documents_app(program, RUN_PANELS);
}

/* The UI Inspector (workstream I1) alone. */
int studio_inspector_run(const char *program)
{
    return studio_documents_app(program, RUN_INSPECTOR);
}

/* The palette, pickers and refactor popover (workstream C4) alone. */
int studio_palette_run(const char *program)
{
    return studio_documents_app(program, RUN_PALETTE);
}

/* The graph's menus and Properties' Node and Member tabs (workstream G1)
   alone. */
int studio_graph_chrome_run(const char *program)
{
    return studio_documents_app(program, RUN_GRAPH_CHROME);
}
