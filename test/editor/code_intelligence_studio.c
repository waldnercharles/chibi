/* The Text Editor's code intelligence in Studio (workstream I, hidden
 * window): problems appear and go as the part is typed and once RmlUi checks
 * it, a completion typed and accepted steps back with the typing through one
 * history from the editor's keys and Studio's Undo, F12 opens a widget's
 * asset, and Shift+F12 lists a class's places in Output's Find view.
 * Workstream Q: Output lists the problems, Back and Forward follow F12, and
 * the Hierarchy moves a row with the keyboard. */
#define _GNU_SOURCE
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FRAME_SECONDS (1.0 / 60.0)
#define PROJECT "test/fixtures/studio"

static const char *const DOC_RML = "<use ui=\"Dialog\">\n"
                                   "  <div class=\"shared\">Hi</div>\n"
                                   "</use>\n";
static const char *const DOC_RCSS = ".shared { margin: 2dp; }\n"
                                    ".shared { colour: red; }\n";

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
    char root[PATH_MAX], doc[PATH_MAX], dialog[PATH_MAX], other[PATH_MAX];
} fixture;

static int remove_entry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

static void write_project(fixture *f)
{
    char pattern[] = "/tmp/chibi-code-studio-XXXXXX", folder[PATH_MAX];
    assert(mkdtemp(pattern) && realpath(pattern, f->root));
    snprintf(folder, sizeof folder, "%s/ui", f->root);
    assert(mkdir(folder, 0777) == 0);
    snprintf(f->doc, sizeof f->doc, "%s/ui/Doc.rml", f->root);
    snprintf(f->dialog, sizeof f->dialog, "%s/ui/Dialog.rml", f->root);
    snprintf(f->other, sizeof f->other, "%s/ui/Other.rml", f->root);
    test_write_ui(folder, "Doc", DOC_RML, DOC_RCSS);
    test_write_ui(folder, "Dialog", "<div class=\"dialog\"><slot/></div>\n", NULL);
    test_write_ui(folder, "Other", "<div class=\"shared\">x</div>\n", ".shared { color: #f00; }\n");
}

static fixture fixture_new()
{
    fixture f = { .world = ecs_world_new() };
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
    assert(app_scene_authoring_load(f.scene, PROJECT "/content/scenes/Main.kdl") == 0);
    f.studio = studio_new(f.scene, f.viewport, PROJECT);
    f.studio->prefs.code_vim = false;
    f.studio->prefs.code_apply_live = false;
    write_project(&f);
    return f;
}

static void fixture_free(fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
    assert(nftw(f->root, remove_entry, 8, FTW_DEPTH | FTW_PHYS) == 0);
}

static void frames(fixture *f, int count)
{
    /* This fixture drives the backend and document commands directly, bypassing
       the SDL queue that normally wakes Studio for their next update. */
    f->studio->ui_schedule.wake = true;
    for (int i = 0; i < count; i++) {
        f->time += FRAME_SECONDS;
        cf_app_update(NULL);
        app_input_frame();
        editor_tick(f->graph, f->time);
        assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
        studio_frame(f->studio, FRAME_SECONDS);
        cf_app_draw_onto_screen(true);
        /* The user's preferences stay as they are. */
        f->studio->prefs_changed_at = 0;
    }
}

/* Types a character a frame, as a person does. */
static void type(fixture *f, const char *text)
{
    for (const char *c = text; *c; c++) {
        rml_backend_process_input(
            f->studio->backend,
            &(rml_input){ .type = RML_INPUT_TEXT_INPUT, .text = c, .text_size = 1 }
        );
        frames(f, 1);
    }
}

static void press(fixture *f, rml_input_key key, unsigned modifiers)
{
    rml_input input = { .type = RML_INPUT_KEY_PRESS, .key = key, .modifiers = modifiers };
    rml_backend_process_input(f->studio->backend, &input);
    input.type = RML_INPUT_KEY_RELEASE;
    rml_backend_process_input(f->studio->backend, &input);
    frames(f, 1);
}

static bool marked(const studio *s, ui_part part, int line, const char *text)
{
    const kit_code_marker *markers = s->code.markers[part];
    for (int i = 0; i < acount(markers); i++) {
        if (markers[i].line == line && strstr(markers[i].message, text)) {
            return true;
        }
    }
    return false;
}

static const char *shown_text(const studio *s)
{
    return kit_code_text(s->code.editor, NULL);
}

/* Places the cursor in the shown part: at `needle`'s `skip`th byte. */
static void cursor_at(fixture *f, const char *needle, int skip)
{
    const char *text = shown_text(f->studio);
    const char *at = strstr(text, needle);
    assert(at);
    kit_code_set_cursor(f->studio->code.editor, (int)(at - text) + skip);
    frames(f, 1);
}

/* Workstream Q: whether rows hold the RCSS warning about `colour`, at the
   bytes the Text Editor underlines. */
static bool lists_colour(const studio_ui_problem *rows, const char *path)
{
    const char *part = ".shared { margin: 2dp; }\n.shared { colour: red; }\n";
    int offset = (int)(strstr(part, "colour") - part);
    for (int i = 0; i < acount(rows); i++) {
        const studio_ui_problem *p = &rows[i];
        if (!strcmp(p->at.path, path) && p->at.part == UI_PART_RCSS && p->at.line == 2 &&
            p->at.offset == offset && !p->error && strstr(p->message, "colour")) {
            return true;
        }
    }
    return false;
}

/* Output's Problems list the shown document's problems, and the project's
   every ui asset's; a row opens its place in the Text Editor. */
static void output_problems(fixture *f)
{
    studio *s = f->studio;
    test_check(lists_colour(studio_code_problems(s), f->doc), "Output's Problems list the shown UI document's RmlUi warning at its place");
    studio_ui_problem *project = NULL;
    studio_code_project_problems(s, &project);
    test_check(lists_colour(project, f->doc), "and the project's, every ui asset checked");
    afree(project);
}

static void problems(fixture *f)
{
    studio *s = f->studio;
    frames(f, 30);
    test_check(
        marked(s, UI_PART_RCSS, 2, "colour") &&
            acount(s->code.markers[UI_PART_RML]) == 0,
        "RmlUi's warning marks its RCSS line once the document rests"
    );
    output_problems(f);
    cursor_at(f, "Hi", 0);
    type(f, "<span class=");
    test_check(
        acount(s->code.markers[UI_PART_RML]) > 0 &&
            s->code.markers[UI_PART_RML][0].severity == KIT_CODE_ERROR,
        "a syntax error shows as it is typed"
    );
    for (int i = 0; i < 12; i++) {
        press(f, RML_INPUT_KEY_BACKSPACE, 0);
    }
    test_check(
        acount(s->code.markers[UI_PART_RML]) == 0 &&
            strstr(ui_buffer_text(studio_ui_buffer(s), UI_PART_RML), ">Hi<"),
        "and goes when it is taken back"
    );
}

static void completion_and_undo(fixture *f)
{
    studio *s = f->studio;
    ui_buffer *b = studio_ui_buffer(s);
    char *before = xstrdup(ui_buffer_text(b, UI_PART_RML));
    cursor_at(f, "</use>", 6);
    type(f, "<use ui=\"Di");
    bool offered = s->code.completing && acount(s->code.completions.items) == 1 &&
                   !strcmp(s->code.completions.items[0].label, "Dialog");
    test_check(offered, "typing a widget's name offers the ui assets it can name");
    frames(f, 1);
    press(f, RML_INPUT_KEY_ENTER, 0);
    test_check(
        strstr(ui_buffer_text(b, UI_PART_RML), "</use><use ui=\"Dialog") &&
            !s->code.completing,
        "Enter accepts the completion into the document"
    );
    press(f, RML_INPUT_KEY_Z, RML_INPUT_MOD_CONTROL);
    test_check(
        strstr(ui_buffer_text(b, UI_PART_RML), "</use><use ui=\"Di\n") &&
            !strcmp(shown_text(s), ui_buffer_text(b, UI_PART_RML)),
        "the editor's undo key takes back the completion alone, in the "
        "document's history"
    );
    press(f, RML_INPUT_KEY_Z, RML_INPUT_MOD_CONTROL);
    test_check(
        !strcmp(ui_buffer_text(b, UI_PART_RML), before) && !strcmp(shown_text(s), before),
        "then the typing"
    );
    studio_history(s, true);
    frames(f, 1);
    test_check(
        strstr(shown_text(s), "</use><use ui=\"Di\n") != NULL,
        "Studio's Redo steps the same history, and the editor follows"
    );
    studio_history(s, false);
    frames(f, 1);
    test_check(!strcmp(shown_text(s), before), "as does its Undo");
    free(before);
}

/* Typing and an external edit can arrive before the host consumes either. */
static void shorter_external_text(fixture *f)
{
    studio *s = f->studio;
    ui_buffer *b = studio_ui_buffer(s);
    char *before = xstrdup(ui_buffer_text(b, UI_PART_RML));
    const char *prefix = "<!-- A long external preface that moves the pending "
                         "edit beyond the shorter document. -->\n";
    text_edit prepend = { 0, 0, prefix };
    ui_buffer_edit(b, UI_PART_RML, &prepend, 1);
    frames(f, 1);
    cursor_at(f, "Hi", 2);
    rml_backend_process_input(
        s->backend,
        &(rml_input){ .type = RML_INPUT_TEXT_INPUT, .text = "x", .text_size = 1 }
    );
    text_edit remove = { 0, (int)strlen(prefix), "" };
    ui_buffer_edit(b, UI_PART_RML, &remove, 1);
    frames(f, 1);
    const char *text = shown_text(s);
    const char *typed = strstr(text, "Hix");
    test_check(
        typed && !strstr(text, prefix) &&
            !strcmp(text, ui_buffer_text(b, UI_PART_RML)) &&
            kit_code_cursor(s->code.editor) == (int)(typed - text) + 3,
        "pending typing rebases onto shorter external text with its caret at "
        "the typed bytes"
    );
    cursor_at(f, "Dialog", 2);
    press(f, RML_INPUT_KEY_F12, 0);
    frames(f, 3);
    test_check(!strcmp(s->code.path, f->dialog), "navigation after shorter external text opens the widget definition");
    press(f, RML_INPUT_KEY_OEM_4, RML_INPUT_MOD_CONTROL);
    frames(f, 3);
    test_check(!strcmp(s->code.path, f->doc), "Back returns to the shortened document");
    studio_history(s, false);
    frames(f, 1);
    test_check(!strstr(shown_text(s), "Hix") && !strstr(shown_text(s), prefix), "undo removes the pending typing while retaining the external shortening");
    text_edit restore = { 0, (int)strlen(ui_buffer_text(b, UI_PART_RML)), before };
    ui_buffer_edit(b, UI_PART_RML, &restore, 1);
    frames(f, 1);
    free(before);
}

static void navigation(fixture *f)
{
    studio *s = f->studio;
    cursor_at(f, "Dialog", 2);
    press(f, RML_INPUT_KEY_F12, 0);
    frames(f, 3);
    test_check(
        !strcmp(s->code.path, f->dialog) && studio_ui_buffer(s)->part == UI_PART_RML,
        "F12 on a widget opens its ui asset in the Text Editor"
    );
    /* Workstream Q: Back and Forward follow the jump. */
    press(f, RML_INPUT_KEY_OEM_4, RML_INPUT_MOD_CONTROL);
    frames(f, 3);
    const char *text = shown_text(s);
    test_check(
        !strcmp(s->code.path, f->doc) && kit_code_cursor(s->code.editor) ==
                                             (int)(strstr(text, "Dialog") - text) + 2,
        "Back (Mod+[) after F12 returns to the place it left"
    );
    test_check(studio_nav_can_step(s, 1), "and Forward can go again");
    studio_nav_step(s, 1);
    frames(f, 3);
    test_check(!strcmp(s->code.path, f->dialog), "Forward returns to where it landed");
    test_check(studio_open_ui_source(s, f->doc, 1), "the document opens again");
    frames(f, 4);
    cursor_at(f, "shared", 1);
    press(f, RML_INPUT_KEY_F12, RML_INPUT_MOD_SHIFT);
    int definitions = 0;
    for (int i = 0; i < acount(s->references); i++) {
        definitions += s->references[i].ui.definition;
    }
    test_check(
        acount(s->references) == 5 && definitions == 3 &&
            s->references[2].ui.definition && !s->references[3].ui.definition &&
            !strcmp(s->references_title, "Class .shared"),
        "Shift+F12 lists a class's places in Output's Find view, definitions "
        "first"
    );
    const studio_reference *other = NULL;
    for (int i = 0; i < acount(s->references) && !other; i++) {
        other = !strcmp(s->references[i].ui.path, f->other) &&
                        s->references[i].ui.definition
                  ? &s->references[i]
                  : NULL;
    }
    test_check(other != NULL, "the rows name the other asset's rule");
    if (other) {
        studio_open_reference(s, other);
        frames(f, 4);
        const char *text = shown_text(s);
        test_check(
            !strcmp(s->code.path, f->other) && studio_ui_buffer(s)->part == UI_PART_RCSS &&
                kit_code_cursor(s->code.editor) == (int)(strstr(text, "shared") - text),
            "a row opens its part in the Text Editor at the place"
        );
    }
}

/* Workstream Q: dnd-kit's keyboard sensor over the Hierarchy. */
static void keyboard_reorder(fixture *f)
{
    studio *s = f->studio;
    test_check(studio_open_ui_source(s, f->doc, 1), "the document opens for the designer");
    frames(f, 4);
    ui_buffer *b = studio_ui_buffer(s);
    char *before = xstrdup(ui_buffer_text(b, UI_PART_RML));
    designer_select(s, 1);
    studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_SPACE, 0);
    studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_ESCAPE, 0);
    frames(f, 1);
    test_check(!strcmp(ui_buffer_text(b, UI_PART_RML), before), "Escape puts a carried row back unmoved");
    studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_SPACE, 0);
    studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_UP, 0);
    studio_designer_key(s, STUDIO_EDITOR_OUTLINER, SDLK_SPACE, 0);
    frames(f, 1);
    test_check(
        !strncmp(ui_buffer_text(b, UI_PART_RML), "<div class=\"shared\">", 20) &&
            !strcmp(shown_text(s), ui_buffer_text(b, UI_PART_RML)),
        "Space picks the selected row up, Up moves it before its parent, and "
        "Space drops it there"
    );
    studio_history(s, false);
    frames(f, 1);
    test_check(!strcmp(ui_buffer_text(b, UI_PART_RML), before), "the move is one undo step");
    free(before);
}

void test_code_intelligence_studio()
{
    fixture f = fixture_new();
    test_check(studio_open_ui_source(f.studio, f.doc, 1), "a ui document opens in the Text Editor");
    frames(&f, 4);
    problems(&f);
    completion_and_undo(&f);
    shorter_external_text(&f);
    navigation(&f);
    keyboard_reorder(&f);
    text_overlay_clear();
    fixture_free(&f);
}
