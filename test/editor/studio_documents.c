/* Studio frames over a project on disk: drawing a graph never changes what its
 * asset saves, so reloading a viewed asset leaves it saved. */
#include "assets/ui_events.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/text_overlay.h"
#include "test/suite_internal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SECONDS (1.0 / 60.0)
#define PROJECT "test/fixtures/studio"
/* Weapons' Ignore Shooter function and its automatic component setter. The
   setter's file entry, {id: 10247, type: Set_GameCollider_ignore_entity},
   stays one line wherever the canvas arranges it. */
#define FUNCTION_GRAPH 1
#define SETTER 10247

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    studio *studio;
    double time;
} studio_fixture;

/* Studio over scene `scene` of the project at `project` with `behavior`
   open. */
static studio_fixture fixture_open(const char *project, const char *scene, const char *behavior)
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

static studio_fixture fixture_new()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Main.yaml", "Weapons");
    assert(editor_open_graph(f.graph, FUNCTION_GRAPH) == 0);
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    studio_maximize_graph(f.studio);
    return f;
}

static void fixture_free(studio_fixture *f)
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
static void frame(studio_fixture *f, bool reload)
{
    f->time += FRAME_SECONDS;
    cf_app_update(NULL);
    app_input_frame();
    editor_tick(f->graph, f->time);
    assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
    if (reload) {
        assert(app_scene_authoring_reload(f->scene) == 0);
    }
    studio_frame(f->studio, FRAME_SECONDS);
    cf_app_draw_onto_screen(true);
}

static void reload_keeps_viewed_graph_saved()
{
    studio_fixture f = fixture_new();
    for (int i = 0; i < 3; i++) {
        frame(&f, false);
    }
    frame(&f, true);
    for (int i = 0; i < 3; i++) {
        frame(&f, false);
    }
    const node *setter = graph_node_by_id(editor_active_graph(f.graph), SETTER);
    test_check(setter && setter->x != 0, "Studio arranges the reloaded graph it shows");
    const game_live_definition *weapons = game_live_find_definition(f.live, "Weapons");
    test_check(!weapons->unsaved, "reloading a library Studio drew leaves it saved");
    fixture_free(&f);
}

/* UI documents as tabs and in the Text Editor (test/editor/ui_documents.c). */
void test_ui_document_tabs();
/* Assets ▸ New and Settings ▸ Project (test/editor/studio_new_assets.c). */
void test_studio_new_assets();
/* Workstream I: the Text Editor's code intelligence
   (test/editor/code_intelligence_studio.c). */
void test_code_intelligence_studio();

/* ---------- UI wiring (workstream K) ---------- */

#define VAULT PROJECT "/content/scenes/Vault.yaml"
#define BANK PROJECT "/content/ui/Bank.rml"
#define BANK_STYLES PROJECT "/content/ui/Bank.rcss"

/* Studio over `project`'s scene `scene` with `behavior` open and the Scene
   filling the workspace. Preferences stay as saved, but for UI Scale 1, so
   the layout the checks click is the same whatever the user's scale. */
static studio_fixture scene_open(const char *project, const char *scene, const char *behavior)
{
    studio_fixture f = fixture_open(project, scene, behavior);
    f.studio = studio_new(f.scene, f.viewport, project);
    f.studio->prefs.ui_scale = f.studio->applied.ui_scale = 1;
    f.studio->prefs.editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    f.studio->applied.editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    f.studio->maximized = STUDIO_AREA_TOP;
    return f;
}

/* Studio over the Vault scene, whose Vault shows the Bank document and
   attaches Teller. */
static studio_fixture vault_open()
{
    return scene_open(PROJECT, VAULT, "Teller");
}

static void frames(studio_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        frame(f, false);
        /* The user's preferences stay as they are. */
        f->studio->prefs_changed_at = 0;
    }
}

static int bank_errors(const studio *s)
{
    int count = 0;
    for (int i = 0; i < acount(s->records); i++) {
        count += s->records[i].kind == KIT_TOAST_ERROR &&
                 strstr(s->records[i].text, "ui/Bank.rml") != NULL;
    }
    return count;
}

/* A broken ui asset's error shows in the Scene and Problems, and Output
   records it once per change. */
static void ui_errors_recorded()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    bool clean = !s->ui_error[0];
    text_overlay_set(BANK, "");
    frames(&f, 3);
    bool shown = strstr(s->ui_error, "ui/Bank.rml") != NULL && bank_errors(s) == 1;
    frames(&f, 3);
    bool once = bank_errors(s) == 1;
    text_overlay_set(BANK, "<div><body/></div>\n");
    frames(&f, 3);
    bool changed = bank_errors(s) == 2;
    text_overlay_set(BANK, NULL);
    frames(&f, 3);
    bool fixed = !s->ui_error[0] && bank_errors(s) == 2;
    if (!(clean && shown && once && changed && fixed)) {
        printf("  %d %d %d %d %d: %s\n", clean, shown, once, changed, fixed, s->ui_error);
    }
    test_check(clean && shown && once && changed && fixed, "a game UI error shows in the Scene and Output records it once per change");
    fixture_free(&f);
}

/* Teller hears the Bank the Vault shows. */
static void ui_events_offered()
{
    studio_fixture f = vault_open();
    frames(&f, 3);
    ui_event *events = studio_behavior_ui_events(f.studio);
    bool ok = acount(events) == 2 && !strcmp(events[0].name, "Extract") &&
              !strcmp(events[0].description, "Sent when the player clicks #extract in Bank") &&
              !strcmp(events[1].name, "Close");
    test_check(ok, "the Add menu offers the events of the GameUi documents that reach the open behavior");
    ui_events_free(events);
    fixture_free(&f);
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

/* The asset's root in the preview's documents, found from the element under
   the pointer. */
static rml_element *preview_root(render_ui *ui)
{
    rml_element *hover = rml_context_hover_element(render_ui_context(ui));
    return hover ? rml_element_query_selector(rml_element_owner_document(hover), "[data-ui=Bank]")
                 : NULL;
}

/* The preview shows the asset alone with the backdrop's class and logs the
   events it emits, which reach no behavior. */
static void preview_logs_actions()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    studio_preview_ui(s, "Bank");
    /* The stage lays out, then the frame's size holds for two draws. */
    frames(&f, 8);
    studio_canvas *c = s->canvas;
    render_ui *ui = engine_viewport_ui(c->viewport);
    click(ui, 5, 5);
    rml_element *root = preview_root(ui);
    bool alone = root && rml_element_query_selector_all(
                             rml_element_owner_document(root),
                             "[data-ui=Bank]",
                             NULL,
                             0
                         ) == 1;
    bool dark = root && rml_element_is_class_set(root, "dark");
    frames(&f, 1);
    bool logged = acount(c->actions) == 1 &&
                  !strcmp(c->actions[0].event, "Extract") &&
                  !strcmp(c->actions[0].element, "#extract");
    click(ui, 5, 5);
    c->light = true;
    frames(&f, 1);
    render_ui_input(ui, &(rml_input){ .type = RML_INPUT_POINTER_MOVE, .x = 5, .y = 5 });
    root = preview_root(ui);
    bool light = root && rml_element_is_class_set(root, "light") &&
                 !rml_element_is_class_set(root, "dark");
    bool counted = acount(c->actions) == 1 && c->actions[0].count == 2;
    studio_preview_ui(s, NULL);
    bool closed = !studio_previewed_ui(s);
    /* The scene's surface lays out again, then the closed preview frees. */
    frames(&f, 3);
    closed &= !s->canvas;
    studio_command_run(s, STUDIO_COMMAND_PREVIEW_UI);
    bool picking = s->pick.open && !strcmp(s->pick.key, STUDIO_PICK_PREVIEW) &&
                   kit_command_is_open(s->kit);
    if (!(picking && alone && dark && logged && light && counted && closed)) {
        printf("  %d %d %d %d %d %d %d\n", picking, alone, dark, logged, light, counted, closed);
    }
    test_check(picking && alone && dark && logged && light && counted && closed, "the Canvas preview shows a ui asset alone, themed, and logs the events it emits");
    fixture_free(&f);
}

/* The Scene area follows the active document: a ui asset's UI document
   shows its preview, and the scene's document shows the scene again. */
static void preview_follows_document()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    char path[PATH_MAX];
    assert(realpath(BANK, path));
    bool opened = studio_open_ui_source(s, path, 1);
    frames(&f, 2);
    bool shown = opened && studio_previewed_ui(s) &&
                 !strcmp(studio_previewed_ui(s), "Bank");
    int scene = -1;
    for (int i = 0; i < acount(f.scene->tabs); i++) {
        scene = f.scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE ? i : scene;
    }
    assert(scene >= 0 && scene_tabs_activate(f.scene, scene) == 0);
    frames(&f, 2);
    bool back = !studio_previewed_ui(s);
    test_check(shown && back, "the Scene area previews the active UI document's asset and returns to the scene with its document");
    fixture_free(&f);
}

static bool logged_last(const studio *s, const char *event)
{
    const studio_canvas *c = s->canvas;
    return acount(c->actions) &&
           !strcmp(c->actions[acount(c->actions) - 1].event, event);
}

/* The elements of the Canvas's asset that `selector` matches, up to 8. */
static int canvas_elements(studio *s, const char *selector, rml_element **found)
{
    char query[192];
    snprintf(query, sizeof query, "[data-ui=%s] %s", s->canvas->asset, selector);
    rml_context *context = render_ui_context(engine_viewport_ui(s->canvas->viewport));
    return rml_element_query_selector_all(rml_context_document(context, 0), query, found, 8);
}

/* The variables the Canvas's documents bind. */
static yyjson_mut_val *canvas_variables(studio *s)
{
    render_ui *ui = engine_viewport_ui(s->canvas->viewport);
    return yyjson_mut_doc_get_root(render_ui_data(ui)->doc);
}

/* Studio over the Studio project's scene, whose root document is its
   manifest's, as the application opens it. */
static studio_fixture studio_project_open()
{
    static char root[PATH_MAX];
    char scene[PATH_MAX + 32];
    assert(realpath("projects/studio", root));
    snprintf(scene, sizeof scene, "%s/content/scenes/Studio.yaml", root);
    studio_fixture f = scene_open(root, scene, "Dismiss");
    app_scene_authoring_set_default_ui(f.scene, "StudioShell");
    return f;
}

/* With the Studio project open, its ui assets are Studio's chrome, so the
   Canvas previews them over Studio's own data: TopBar lists the document
   tabs and labels its commands, whose studio.* events only reach the
   Actions log. A game's preview binds only save and system. */
static void preview_binds_studio_data()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    frames(&f, 3);
    studio_preview_ui(s, "TopBar");
    frames(&f, 8);
    rml_element *found[8];
    /* Besides data-for's template. */
    int tabs = canvas_elements(s, ".doc-tab", found) - 1;
    bool listed = s->own_project && tabs > 0 && tabs == acount(f.scene->tabs);
    char label[64] = "";
    if (canvas_elements(s, ".search-trigger .label", found) == 1) {
        rml_element_inner_rml(found[0], label, sizeof label);
    }
    bool labeled = !strcmp(
        label,
        studio_command_describe(s, STUDIO_COMMAND_SEARCH).label
    );
    canvas_elements(s, ".search-trigger", found);
    rml_element_click(found[0]);
    frames(&f, 2);
    bool logged = logged_last(s, "studio.search") && !kit_command_is_open(s->kit);
    fixture_free(&f);

    f = vault_open();
    frames(&f, 3);
    studio_preview_ui(f.studio, "Bank");
    frames(&f, 3);
    yyjson_mut_val *variables = canvas_variables(f.studio);
    bool game = yyjson_mut_obj_size(variables) == 2 &&
                yyjson_mut_obj_get(variables, "save") &&
                yyjson_mut_obj_get(variables, "system");
    if (!(listed && labeled && logged && game)) {
        printf("  %d %d %d %d: %d tabs, '%s'\n", listed, labeled, logged, game, tabs, label);
    }
    test_check(listed && labeled && logged && game, "a Studio-project Canvas preview binds Studio's data and only logs its commands; a game's binds save and system");
    fixture_free(&f);
}

static void ui_wiring_checks()
{
    ui_errors_recorded();
    ui_events_offered();
    preview_logs_actions();
    preview_follows_document();
    preview_binds_studio_data();
}

/* ---------- Authored status bar (workstream J) ---------- */

static rml_element *status_bar(studio *s)
{
    rml_element *doc = rml_context_document(rml_backend_context(s->backend), 0);
    return rml_element_query_selector(doc, "[data-ui=StatusBar]");
}

/* The shown labels of the status bar's key hints, joined by '|'. */
static void status_hints(studio *s, char *out, size_t size)
{
    rml_element *labels[8];
    int count = rml_element_query_selector_all(status_bar(s), "[data-ui=KeyHint] .label", labels, 8);
    out[0] = '\0';
    for (int i = 0; i < count && i < 8; i++) {
        char text[64] = "";
        rml_element_inner_rml(labels[i], text, sizeof text);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? "|" : "", text);
    }
}

static bool toggle_on(studio *s)
{
    char state[8] = "";
    rml_element *assets = rml_element_query_selector(status_bar(s), ".toggle-drawer");
    rml_element_attribute(assets, "data-state", state, sizeof state);
    return !strcmp(state, "on");
}

/* Studio shows the authored status bar over its own state: the hovered
   area's hints, the last report, a drawer toggle that runs its command, and
   UI Scale native in its slot. */
static void status_bar_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    kit_rect area = s->areas[STUDIO_AREA_TOP].rect;
    s->in.mouse_x = area.x + area.w / 2;
    s->in.mouse_y = area.y + area.h / 2;
    frames(&f, 2);
    char scene[256], elsewhere[256];
    status_hints(s, scene, sizeof scene);
    s->in.mouse_x = s->in.mouse_y = 1;
    frames(&f, 2);
    status_hints(s, elsewhere, sizeof elsewhere);
    bool hints = !strcmp(scene, "Select|Pan (middle)|Frame") &&
                 !strcmp(elsewhere, "Select|Search");

    studio_report(s, "Saved");
    frames(&f, 2);
    rml_element *report = rml_element_query_selector(status_bar(s), ".report");
    char said[32] = "";
    rml_element_attribute(rml_element_query_selector(report, "morph-text"), "aria-label", said, sizeof said);
    bool reported = rml_element_is_visible(report, false) && !strcmp(said, "Saved");
    s->status_time = s->time - 4;
    frames(&f, 2);
    reported &= rml_element_has_attribute(report, "data-ending-style") ||
                !rml_element_is_visible(report, false);

    int before = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool shown_before = toggle_on(s) == (before == 1);
    rml_element_click(rml_element_query_selector(status_bar(s), ".toggle-drawer"));
    frames(&f, 2);
    int after = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool toggled = shown_before && after != before && toggle_on(s) == (after == 1);

    kit_rect scale;
    bool native = kit_find(s->kit, "app/status/scale/scale", &scale);
    if (!(hints && reported && toggled && native)) {
        printf("  %d %d %d %d: %s / %s\n", hints, reported, toggled, native, scene, elsewhere);
    }
    test_check(hints && reported && toggled && native, "Studio's authored status bar follows the hovered area and reports, runs its drawer toggles' commands and hosts UI Scale natively");
    fixture_free(&f);
}

/* ---------- Wave 2 follow-ups (workstream P) ---------- */

static void key_event_mod(SDL_Scancode key, bool down, SDL_Keymod mod, bool repeat)
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

static void key_event(SDL_Scancode key, bool down)
{
    key_event_mod(key, down, SDL_KMOD_NONE, false);
}

/* The Canvas preview takes the keys typed while its area has the keyboard,
   so the asset's data-keys bindings send their events, and not while
   another area has it. */
static void preview_hears_keys()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(BANK, "<div id=\"extract\" autofocus data-keys=\"C:Close\">Extract</div>\n");
    text_overlay_set(BANK_STYLES, "#extract { display: block; height: 100%; tab-index: auto; }\n");
    frames(&f, 3);
    studio_preview_ui(s, "Bank");
    frames(&f, 8);
    s->focus_area = STUDIO_AREA_BOTTOM;
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool elsewhere = !logged_last(s, "Close");
    s->focus_area = STUDIO_AREA_TOP;
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool heard = logged_last(s, "Close");
    /* Studio keeps its keys there: Space still plays the scene. */
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    bool shortcut = app_scene_authoring_runtime_state(f.scene).playing;
    studio_runtime(s, APP_RUNTIME_STOP);
    test_check(elsewhere && heard && shortcut, "the focused Canvas preview takes keys, so data-keys bindings fire, and Studio keeps its shortcuts");
    studio_preview_ui(s, NULL);
    frames(&f, 3);
    game_entities *entities = f.scene->live->entities;
    int before = game_entities_pending_events(entities);
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    frames(&f, 2);
    bool editing = game_entities_pending_events(entities) == before;
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 3);
    before = game_entities_pending_events(entities);
    key_event(SDL_SCANCODE_C, true);
    key_event(SDL_SCANCODE_C, false);
    /* The key reaches the document, whose activation reaches Vault a frame
       later, before the next scene frame runs it. */
    frames(&f, 2);
    bool playing = game_entities_pending_events(entities) == before + 1;
    test_check(editing && playing, "the focused Scene's game documents take keys while the scene plays");
    text_overlay_set(BANK, NULL);
    text_overlay_set(BANK_STYLES, NULL);
    fixture_free(&f);
}

/* The first element in Studio's documents that `selector` matches. */
static rml_element *studio_element(studio *s, const char *selector)
{
    rml_context *context = rml_backend_context(s->backend);
    rml_element *found = NULL;
    for (int i = 0; !found && i < rml_context_document_count(context); i++) {
        found = rml_element_query_selector(rml_context_document(context, i), selector);
    }
    return found;
}

static void text_event(const char *text);

/* Recording consumes key and text events before the normal shortcut and
   game-input paths. A repeat must leave the recorder active for the next
   non-repeat keydown. */
static void settings_records_shortcut()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    studio_ui_show(s, "Settings");
    frames(&f, 4);
    rml_element *search_field = studio_element(s, ".search-input");
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    bool text_field_kept = search_field &&
                           !app_scene_authoring_runtime_state(f.scene).playing;

    /* The Settings page test covers the Record control. Here, exercise the
       host after that control has requested a recording. */
    s->shortcut_recording = STUDIO_COMMAND_SEARCH + 1;
    bool recording = s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;
    SDL_Event lost = { .type = SDL_EVENT_WINDOW_FOCUS_LOST };
    lost.window.windowID = SDL_GetWindowID(cf_app_get_window());
    assert(SDL_PushEvent(&lost));
    frames(&f, 1);
    bool focus_canceled = recording && s->shortcut_recording == 0 &&
                          !s->prefs.shortcuts[STUDIO_COMMAND_SEARCH][0];
    lost.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    assert(SDL_PushEvent(&lost));
    frames(&f, 1);
    s->shortcut_recording = STUDIO_COMMAND_SEARCH + 1;
    recording = s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;

    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    SDL_Scancode primary_key = s->mac ? SDL_SCANCODE_LGUI : SDL_SCANCODE_LCTRL;
    key_event_mod(primary_key, true, primary, false);
    key_event_mod(SDL_SCANCODE_P, true, primary | SDL_KMOD_SHIFT, true);
    key_event_mod(SDL_SCANCODE_P, false, primary | SDL_KMOD_SHIFT, false);
    frames(&f, 2);
    bool repeat_ignored = recording &&
                          s->shortcut_recording == STUDIO_COMMAND_SEARCH + 1;
    text_event("p");
    key_event_mod(SDL_SCANCODE_P, true, primary | SDL_KMOD_SHIFT, false);
    key_event_mod(SDL_SCANCODE_P, false, primary | SDL_KMOD_SHIFT, false);
    key_event_mod(primary_key, false, SDL_KMOD_NONE, false);
    frames(&f, 3);
    bool captured = s->shortcut_recording == 0 &&
                    !strcmp(s->prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+Shift+P");
    if (!(text_field_kept && focus_canceled && repeat_ignored && captured)) {
        printf(
            "  %d %d %d %d (recording %d)\n",
            text_field_kept,
            focus_canceled,
            repeat_ignored,
            captured,
            s->shortcut_recording
        );
    }
    test_check(
        text_field_kept && focus_canceled && repeat_ignored && captured,
        "Settings text focus blocks Play; focus loss cancels recording, which "
        "ignores repeats and captures a chord through the host input queue"
    );
    fixture_free(&f);
}

/* Repeats remain limited to commands that are useful while held. */
static void shortcut_repeat_policy()
{
    studio_fixture zoom = fixture_new();
    studio *s = zoom.studio;
    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    snprintf(s->prefs.shortcuts[STUDIO_COMMAND_ZOOM_IN], STUDIO_CHORD_SIZE, "Mod+F12");
    s->prefs.ui_scale = 1;
    apush(s->keys, ((studio_key){ SDLK_F12, primary, true }));
    studio_shortcuts(s);
    float held_scale = s->prefs.ui_scale;
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_F12, primary, false }));
    studio_shortcuts(s);
    bool zoom_repeat_runs = held_scale > 1;
    bool zoom_press_runs = s->prefs.ui_scale > held_scale;
    fixture_free(&zoom);

    studio_fixture search = fixture_new();
    s = search.studio;
    primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    apush(s->keys, ((studio_key){ SDLK_K, primary, true }));
    studio_shortcuts(s);
    bool toggle_repeat_ignored = !kit_command_is_open(s->kit);
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_K, primary, false }));
    studio_shortcuts(s);
    bool toggle_press_runs = kit_command_is_open(s->kit);
    test_check(zoom_repeat_runs && zoom_press_runs && toggle_repeat_ignored && toggle_press_runs, "repeatable zoom runs on a held shortcut while the Search toggle waits for a fresh press");
    fixture_free(&search);
}

/* Whether the Scene's last draw framed the camera it holds now, as the
   pointer it measured shows. */
static bool drew_camera(const studio_fixture *f)
{
    engine_viewport_camera camera;
    engine_viewport_pointer pointer;
    engine_viewport_get_camera(f->viewport, &camera);
    engine_viewport_get_pointer(f->viewport, &pointer);
    float width = f->studio->surface.rect.w * f->studio->prefs.ui_scale;
    return fabsf(pointer.world_per_pixel * width - camera.span_width) <
           0.01f * camera.span_width;
}

/* The Scene draws the camera its first draw fits to the Vault, which the
   reset camera does not frame, and its zoom readout shows nothing before
   then and that camera's zoom after, so it never rolls from the reset
   camera's. */
static void readout_follows_camera()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    bool shown = false, current = true, drawn = true;
    for (int i = 0; i < 6; i++) {
        frames(&f, 1);
        drawn &= f.scene->camera_target < 0 || drew_camera(&f);
        rml_element *readout = studio_element(s, ".readout");
        if (!readout) {
            continue;
        }
        engine_viewport_view view;
        engine_viewport_get_view(f.viewport, &view);
        char value[32], expected[32];
        rml_element_attribute(readout, "aria-label", value, sizeof value);
        snprintf(expected, sizeof expected, "%d%%", (int)roundf(s->surface.rect.w / view.width * 100));
        current &= !strcmp(value, expected);
        shown = true;
    }
    test_check(shown && current && drawn, "the Scene draws the camera it fits from its first frame, and its zoom readout shows that camera");
    fixture_free(&f);
}

/* ---------- Authored top bar and area headers (workstream T) ---------- */

/* The elements of Studio's `panel` that `selector` matches, up to 8. */
static int panel_elements(studio *s, const char *panel, const char *selector, rml_element **found)
{
    char query[128];
    snprintf(query, sizeof query, "[data-ui=%s] %s", panel, selector);
    rml_element *doc = rml_context_document(rml_backend_context(s->backend), 0);
    return rml_element_query_selector_all(doc, query, found, 8);
}

/* A press and release on `el`, which focuses it as a click does. Input is
   in dp, and the element's box in pixels. */
static void press(studio *s, rml_element *el)
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

static int tab_named(const app_scene_authoring *scene, const char *label)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].label && !strcmp(scene->tabs[i].label, label)) {
            return i;
        }
    }
    return -1;
}

/* Studio shows its authored top bar and area headers over its own state:
   the menubar, the play target and each area's editor type menu and
   controls in their native slots, the document tabs, whose faces and close
   buttons run their commands with the tab's index, the transport's
   commands, and each header's Maximize. */
static void chrome_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    /* The Text Editor shows the document, so the Scene gives up the workspace. */
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 3);
    kit_rect r;
    rml_element *heads[8], *tabs[8], *play[8];
    int shown = panel_elements(s, "AreaHeader", ".area-head", heads);
    bool slots = kit_find(s->kit, "app/topbar/menus/file", &r) &&
                 kit_find(s->kit, "app/topbar/target/target", &r) &&
                 kit_find(s->kit, "app/workspace/main/top/head/type/type", &r) &&
                 kit_find(s->kit, "app/workspace/main/top/head/controls", &r) &&
                 s->maximized < 0 && shown >= STUDIO_AREA_COUNT - 1;
    int count = panel_elements(s, "TopBar", ".doc-tab", tabs) - 1;
    int bank = tab_named(f.scene, "Bank"), scene = bank == 0 ? 1 : 0;
    rml_element *close = bank >= 0
                           ? rml_element_query_selector(tabs[bank], ".doc-close")
                           : NULL;
    bool listed = count == acount(f.scene->tabs) && f.scene->active_tab == bank &&
                  close && rml_element_is_visible(close, true) &&
                  !rml_element_is_visible(rml_element_query_selector(tabs[scene], ".doc-close"), true);
    /* The press takes the keyboard from the Text Editor, whose focused text
       would make its document active again. */
    press(s, rml_element_query_selector(tabs[scene], ".face"));
    frames(&f, 2);
    bool selected = f.scene->active_tab == scene && !kit_wants_keyboard(s->kit);
    /* This narrow window clips the last tab, so the click goes to it. */
    rml_element_click(close);
    frames(&f, 2);
    bool closed = tab_named(f.scene, "Bank") < 0 &&
                  panel_elements(s, "TopBar", ".doc-tab", tabs) - 1 ==
                      acount(f.scene->tabs);

    panel_elements(s, "TopBar", ".group > .btn", play);
    rml_element_click(play[0]);
    frames(&f, 2);
    bool playing = app_scene_authoring_runtime_state(f.scene).playing;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 1);

    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool maximized = s->maximized == STUDIO_AREA_TOP &&
                     panel_elements(s, "AreaHeader", ".area-head", heads) == 1;
    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool restored = maximized && s->maximized < 0 &&
                    panel_elements(s, "AreaHeader", ".area-head", heads) == shown;
    if (!(slots && listed && selected && closed && playing && restored)) {
        printf("  %d %d %d %d %d %d\n", slots, listed, selected, closed, playing, restored);
    }
    test_check(slots && listed && selected && closed && playing && restored, "Studio's authored top bar and area headers host their native slots and run tab, transport and area commands");
    fixture_free(&f);
}

/* ---------- arena2d end to end (wave 3, workstream X) ---------- */

static void text_event(const char *text)
{
    SDL_Event event = { .type = SDL_EVENT_TEXT_INPUT };
    event.text.windowID = SDL_GetWindowID(cf_app_get_window());
    event.text.text = text;
    assert(SDL_PushEvent(&event));
}

/* The value of the focused element in the Scene's game documents. */
static void game_field(studio_fixture *f, char *value, size_t size)
{
    rml_context *context = render_ui_context(engine_viewport_ui(f->viewport));
    rml_element *focus = rml_context_focus_element(context);
    value[0] = '\0';
    if (focus) {
        rml_element_value(focus, value, size);
    }
}

/* While the scene plays, a focused text field in its game documents takes
   the text typed in the focused Scene, SDL delivering text for it, and
   Studio keeps the keys with the primary modifier. */
static void scene_text_field()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(BANK, "<input id=\"name\" type=\"text\" autofocus/>\n");
    s->focus_area = STUDIO_AREA_TOP;
    frames(&f, 3);
    char value[32];
    text_event("x");
    frames(&f, 2);
    game_field(&f, value, sizeof value);
    bool editing = !value[0] && !engine_viewport_ui_typing(f.viewport);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 3);
    bool started = engine_viewport_ui_typing(f.viewport) && s->text_input;
    key_event(SDL_SCANCODE_A, true);
    text_event("a");
    key_event(SDL_SCANCODE_A, false);
    text_event("b");
    frames(&f, 2);
    game_field(&f, value, sizeof value);
    bool typed = !strcmp(value, "ab");
    SDL_Event select = { .type = SDL_EVENT_KEY_DOWN };
    select.key.windowID = SDL_GetWindowID(cf_app_get_window());
    select.key.scancode = SDL_SCANCODE_A;
    select.key.key = SDLK_A;
    select.key.mod = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    select.key.down = true;
    assert(SDL_PushEvent(&select));
    frames(&f, 1);
    bool kept = !acount(s->game_input);
    key_event(SDL_SCANCODE_A, false);
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 2);
    bool stopped = !engine_viewport_ui_typing(f.viewport) && !s->text_input;
    if (!(editing && started && typed && kept && stopped)) {
        printf("  %d %d %d '%s' %d %d\n", editing, started, typed, value, kept, stopped);
    }
    test_check(editing && started && typed && kept && stopped, "a focused text field in the playing Scene's game UI takes typed text, and Studio keeps its chords");
    text_overlay_set(BANK, NULL);
    fixture_free(&f);
}

/* ---------- Rename objects and prototype components ---------- */

static void tap(SDL_Scancode key)
{
    key_event(key, true);
    key_event(key, false);
}

/* The pointer to (x, y) in dp, as SDL reports it in window points. */
static void mouse_move(const studio *s, float x, float y)
{
    SDL_Event move = { .type = SDL_EVENT_MOUSE_MOTION };
    move.motion.windowID = SDL_GetWindowID(cf_app_get_window());
    move.motion.x = x * s->prefs.ui_scale;
    move.motion.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&move));
}

static void mouse_click(studio_fixture *f, float x, float y, Uint8 button)
{
    const studio *s = f->studio;
    mouse_move(s, x, y);
    for (int down = 1; down >= 0; down--) {
        SDL_Event press = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                         : SDL_EVENT_MOUSE_BUTTON_UP };
        press.button.windowID = SDL_GetWindowID(cf_app_get_window());
        press.button.button = button;
        press.button.down = down;
        press.button.clicks = 1;
        press.button.x = x * s->prefs.ui_scale;
        press.button.y = y * s->prefs.ui_scale;
        assert(SDL_PushEvent(&press));
    }
    frames(f, 2);
}

static bool click_path(studio_fixture *f, const char *path, Uint8 button)
{
    kit_rect r;
    if (!kit_find(f->studio->kit, path, &r)) {
        return false;
    }
    mouse_click(f, r.x + r.w * 0.5f, r.y + r.h * 0.5f, button);
    return true;
}

static void mouse_over(studio_fixture *f, studio_area_slot area)
{
    kit_rect r = f->studio->areas[area].rect;
    mouse_move(f->studio, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    frames(f, 1);
}

static bool named(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!strcmp(scene->actors[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* Studio over the Vault scene with the Outliner and Properties beside the
   Scene, whatever layout was saved. */
static studio_fixture side_open()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    s->prefs.editors[STUDIO_AREA_OUTLINER] = STUDIO_EDITOR_OUTLINER;
    s->prefs.editors[STUDIO_AREA_PROPERTIES] = STUDIO_EDITOR_PROPERTIES;
    memcpy(s->applied.editors, s->prefs.editors, sizeof s->prefs.editors);
    s->focus_area = STUDIO_AREA_OUTLINER;
    frames(&f, 3);
    return f;
}

/* Renames one object at a time: F2 over the Outliner edits its row, Enter
   renames as one Undo step, Escape keeps the name and a name in use is
   refused; without the Outliner, F2 edits Properties' name field. */
static void rename_objects()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    app_scene_authoring *scene = f.scene;
    studio_add_object(s, "Vault");
    select_instance(scene, 0);
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_OUTLINER);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool field = s->rename.entity && s->rename.outliner && kit_wants_keyboard(s->kit);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool renamed = named(scene, "Strongroom") && !named(scene, "Vault") &&
                   !s->rename.entity;
    studio_history(s, false);
    bool undone = named(scene, "Vault") && !named(scene, "Strongroom");
    studio_history(s, true);
    bool redone = named(scene, "Strongroom");
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Nope");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    bool kept = named(scene, "Strongroom") && !named(scene, "Nope") &&
                !s->rename.entity;
    select_instance(scene, 1);
    frames(&f, 1);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    text_event("Strongroom");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool refused = named(scene, "Vault_1") && strstr(scene->message, "Strongroom");
    s->prefs.editors[STUDIO_AREA_OUTLINER] = STUDIO_EDITOR_ASSETS;
    s->applied.editors[STUDIO_AREA_OUTLINER] = STUDIO_EDITOR_ASSETS;
    frames(&f, 2);
    mouse_over(&f, STUDIO_AREA_TOP);
    tap(SDL_SCANCODE_F2);
    frames(&f, 3);
    bool properties = s->tab == STUDIO_TAB_OBJECT && kit_wants_keyboard(s->kit);
    text_event("Annex");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool inspector = named(scene, "Annex") && !named(scene, "Vault_1");
    if (!(field && renamed && undone && redone && kept && refused &&
          properties && inspector)) {
        printf("  %d %d %d %d %d %d %d %d\n", field, renamed, undone, redone, kept, refused, properties, inspector);
    }
    test_check(field && renamed && undone && redone && kept, "F2 over the Outliner renames its row in place as one Undo step, and Escape keeps the name");
    test_check(refused && properties && inspector, "a name in use is refused, and F2 edits Properties' name field while no area shows the Outliner");
    fixture_free(&f);
}

static bool prototype_has(app_scene_authoring *scene, const char *component)
{
    const game_prototype *prototype = find_prototype(scene, "Vault")->prototype;
    for (int i = 0; i < acount(prototype->components); i++) {
        if (!strcmp(prototype->components[i].desc->name, component)) {
            return true;
        }
    }
    return false;
}

/* Add Component searches the components the prototype lacks and adds the
   chosen one; its panel's menu removes it, and the toast's Undo returns it. */
static void prototype_components()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    /* Properties fills the workspace, so its last panels are in view. */
    s->maximized = s->focus_area = STUDIO_AREA_PROPERTIES;
    assert(app_scene_authoring_open_prototype(f.scene, "Vault") == 0);
    frames(&f, 3);
    const char *scroll = "app/workspace/main/properties/body/properties/scroll";
    char path[256];
    snprintf(path, sizeof path, "%s/add/component", scroll);
    bool button = click_path(&f, path, SDL_BUTTON_LEFT);
    bool palette = kit_command_is_open(s->kit) && s->components.open;
    text_event("Health");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    char added[128];
    snprintf(added, sizeof added, "%s", s->components.added);
    bool gained = added[0] && prototype_has(f.scene, added) && !s->components.open;
    snprintf(path, sizeof path, "%s/%s", scroll, added);
    kit_rect panel = { 0 };
    bool shown = kit_find(s->kit, path, &panel);
    mouse_click(&f, panel.x + 40, panel.y + 10, SDL_BUTTON_RIGHT);
    frames(&f, 8);
    bool menu = kit_popup_is_open(s->kit, "component-menu");
    click_path(&f, "layer/@component-menu/remove", SDL_BUTTON_LEFT);
    bool removed = !prototype_has(f.scene, added);
    rml_element *undo = studio_element(s, ".toast-action");
    if (undo) {
        rml_element_click(undo);
    }
    frames(&f, 2);
    bool restored = prototype_has(f.scene, added);
    studio_history(s, false);
    bool undone = !prototype_has(f.scene, added);
    studio_history(s, true);
    bool redone = prototype_has(f.scene, added);
    if (!(button && palette && gained && shown && menu && removed && undo &&
          restored && undone && redone)) {
        printf(
            "  %d %d %d %d %d %d %d %d %d %d\n",
            button,
            palette,
            gained,
            shown,
            menu,
            removed,
            undo != NULL,
            restored,
            undone,
            redone
        );
    }
    test_check(button && palette && gained, "Add Component searches the components a prototype lacks and adds the one chosen");
    test_check(shown && menu && removed && undo && restored && undone && redone, "a component panel's menu removes it with Undo in a toast, and Undo and Redo step both");
    fixture_free(&f);
}

int studio_documents_run(const char *program)
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
    reload_keeps_viewed_graph_saved();
    test_ui_document_tabs();
    test_studio_new_assets();
    /* UI wiring (workstream K) */
    ui_wiring_checks();
    /* Authored status bar (workstream J) */
    status_bar_follows_studio();
    /* Workstream I: code intelligence in the Text Editor. */
    test_code_intelligence_studio();
    /* Wave 2 follow-ups (workstream P) */
    preview_hears_keys();
    settings_records_shortcut();
    shortcut_repeat_policy();
    readout_follows_camera();
    /* Authored top bar and area headers (workstream T) */
    chrome_follows_studio();
    /* arena2d end to end (wave 3, workstream X) */
    scene_text_field();
    /* Rename objects and prototype components */
    rename_objects();
    prototype_components();
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
