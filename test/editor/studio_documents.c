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

/* Studio over the fixture project's scene `scene` with `behavior` open. */
static studio_fixture fixture_open(const char *scene, const char *behavior)
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
    assert(app_scene_authoring_set_content_root(f.scene, PROJECT "/content") == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, scene) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, behavior) == 0);
    return f;
}

static studio_fixture fixture_new()
{
    studio_fixture f = fixture_open(PROJECT "/content/scenes/Main.yaml", "Weapons");
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
/* Workstream I: the Text Editor's code intelligence
   (test/editor/code_intelligence_studio.c). */
void test_code_intelligence_studio();

/* ---------- UI wiring (workstream K) ---------- */

#define VAULT PROJECT "/content/scenes/Vault.yaml"
#define BANK PROJECT "/content/ui/Bank.yaml"

/* Studio over the Vault scene, whose Vault shows the Bank document and
   attaches Teller, with the Scene filling the workspace. Preferences stay as
   saved. */
static studio_fixture vault_open()
{
    studio_fixture f = fixture_open(VAULT, "Teller");
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    f.studio->prefs.editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    f.studio->applied.editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_SCENE;
    f.studio->maximized = STUDIO_AREA_TOP;
    return f;
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
                 strstr(s->records[i].text, "ui/Bank.yaml") != NULL;
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
    text_overlay_set(BANK, "rml: [\n");
    frames(&f, 3);
    bool shown = strstr(s->ui_error, "ui/Bank.yaml") != NULL && bank_errors(s) == 1;
    frames(&f, 3);
    bool once = bank_errors(s) == 1;
    text_overlay_set(BANK, "rcss: 4\n");
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

static void ui_wiring_checks()
{
    ui_errors_recorded();
    ui_events_offered();
    preview_logs_actions();
    preview_follows_document();
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
    bool reported = rml_element_is_class_set(report, "shown");
    s->status_time = s->time - 4;
    frames(&f, 2);
    reported &= !rml_element_is_class_set(report, "shown");

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

static void key_event(SDL_Scancode key, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP };
    event.key.windowID = SDL_GetWindowID(cf_app_get_window());
    event.key.scancode = key;
    event.key.key = SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, true);
    event.key.down = down;
    assert(SDL_PushEvent(&event));
}

static bool logged_last(const studio *s, const char *event)
{
    const studio_canvas *c = s->canvas;
    return acount(c->actions) &&
           !strcmp(c->actions[acount(c->actions) - 1].event, event);
}

/* The Canvas preview takes the keys typed while its area has the keyboard,
   so the asset's data-keys bindings send their events, and not while
   another area has it. */
static void preview_hears_keys()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    text_overlay_set(
        BANK,
        "rml: |\n  <div id=\"extract\" autofocus "
        "data-keys=\"C:Close\">Extract</div>\n"
        "rcss: |\n  #extract { display: block; height: 100%; tab-index: auto; "
        "}\n"
    );
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

/* A press and release on `el`, which focuses it as a click does. */
static void press(studio *s, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    rml_input input = { .x = at.x + size.x * 0.5f, .y = at.y + size.y * 0.5f };
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
    /* UI wiring (workstream K) */
    ui_wiring_checks();
    /* Authored status bar (workstream J) */
    status_bar_follows_studio();
    /* Workstream I: code intelligence in the Text Editor. */
    test_code_intelligence_studio();
    /* Wave 2 follow-ups (workstream P) */
    preview_hears_keys();
    readout_follows_camera();
    /* Authored top bar and area headers (workstream T) */
    chrome_follows_studio();
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
