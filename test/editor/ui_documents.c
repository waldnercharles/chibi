/* UI documents: a ui asset's or stylesheet's text open for editing, its undo
 * history, live apply through overlays, canonical saves and disk conflicts;
 * then as Studio document tabs and in the Text Editor. */
#define _GNU_SOURCE
#include "assets/ui_document.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/generated.h"
#include "support/asset_text.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/suite_internal.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SETTLE (UI_BUFFER_SETTLE + 0.01)
#define FRAME_SECONDS (1.0 / 60.0)

/* A ui asset as a person might write it: a comment, four-space blocks. */
static const char *const WRITTEN = "# the pause panel\n"
                                   "rml: |\n"
                                   "    <div class=\"panel\">Hello</div>\n"
                                   "\n"
                                   "rcss: |\n"
                                   "    .panel { color: #fff; }\n";
static const char *const RML = "<div class=\"panel\">Hello</div>\n";
static const char *const RCSS = ".panel { color: #fff; }\n";

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static char *read_text(const char *path)
{
    char *text = text_file_read_disk(path);
    assert(text);
    return text;
}

/* An empty folder, as an absolute path without symbolic links. */
static void temporary_folder(char *out, size_t size)
{
    char pattern[] = "/tmp/chibi-ui-documents-XXXXXX";
    assert(mkdtemp(pattern));
    assert(realpath(pattern, out) && strlen(out) < size);
}

static int remove_entry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

static void remove_folder(const char *folder)
{
    assert(nftw(folder, remove_entry, 8, FTW_DEPTH | FTW_PHYS) == 0);
}

static void join(char *out, size_t size, const char *folder, const char *name)
{
    snprintf(out, size, "%s/%s", folder, name);
}

static bool canonical(const char *text)
{
    char error[128];
    char *formatted = asset_text_to_disk(text, error, sizeof error);
    bool same = formatted && !strcmp(formatted, text);
    free(formatted);
    return same;
}

static bool overlay_is(const char *path, const char *text)
{
    const char *overlay = text_overlay_text(path);
    return text ? overlay && !strcmp(overlay, text) : !overlay;
}

/* ---------- The buffer ---------- */

static void opens_parts(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "Pause.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    test_check(
        b && !strcmp(ui_buffer_text(b, UI_PART_RML), RML) &&
            !strcmp(ui_buffer_text(b, UI_PART_RCSS), RCSS),
        "a ui asset opens as its RML and RCSS parts"
    );
    test_check(
        !ui_buffer_dirty(b) && !ui_buffer_can_undo(b) &&
            b->part == UI_PART_RML && !b->error[0],
        "an opened ui asset is clean, valid and shows its RML"
    );
    ui_buffer_free(b);
    join(path, sizeof path, folder, "List.yaml");
    write_text(path, "- not a mapping\n");
    test_check(
        !ui_buffer_open(path, error, sizeof error) && strstr(error, "mapping"),
        "a file that is not a ui asset does not open"
    );
    join(path, sizeof path, folder, "Markless.yaml");
    write_text(path, "rcss: \".a { color: red; }\"\n");
    b = ui_buffer_open(path, error, sizeof error);
    test_check(
        b && !strcmp(ui_buffer_text(b, UI_PART_RML), "") && strstr(b->error, "rml"),
        "a ui asset without markup opens with why it does not read"
    );
    ui_buffer_free(b);
}

static void edits_and_history(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "History.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f00; }\n", 1);
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f80; }\n", 1.5);
    test_check(
        ui_buffer_dirty(b) && b->part == UI_PART_RCSS && acount(b->undo) == 1,
        "typing within a second is one undo step, in the part typed into"
    );
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f88; }\n", 3);
    test_check(acount(b->undo) == 2, "typing after a pause starts an undo step");
    text_edit edit = { 0, 0, "<!-- top -->" };
    ui_buffer_edit(b, UI_PART_RML, &edit, 1);
    test_check(
        acount(b->undo) == 3 && b->part == UI_PART_RML &&
            !strncmp(ui_buffer_text(b, UI_PART_RML), "<!-- top --><div", 16),
        "a text edit applies as its own undo step"
    );
    ui_buffer_undo(b);
    ui_buffer_undo(b);
    test_check(
        !strcmp(ui_buffer_text(b, UI_PART_RCSS), ".panel { color: #f80; }\n") &&
            !strcmp(ui_buffer_text(b, UI_PART_RML), RML) && b->part == UI_PART_RCSS,
        "undo restores the text and turns to the part it changed"
    );
    ui_buffer_undo(b);
    test_check(!ui_buffer_dirty(b) && !ui_buffer_can_undo(b), "undoing every step leaves the file's text");
    ui_buffer_redo(b);
    test_check(
        ui_buffer_dirty(b) && ui_buffer_can_redo(b) &&
            !strcmp(ui_buffer_text(b, UI_PART_RCSS), ".panel { color: #f80; }\n"),
        "redo reapplies a step"
    );
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #000; }\n", 10);
    test_check(!ui_buffer_can_redo(b), "a new change drops the steps undone");
    ui_buffer_free(b);
}

static void live_apply(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "Live.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    test_check(!ui_buffer_update(b, 0, true) && overlay_is(path, NULL), "an unchanged document leaves its file");
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f00; }\n", 1);
    ui_buffer_update(b, 1, true);
    test_check(overlay_is(path, NULL), "an edit waits to settle before it applies");
    test_check(ui_buffer_update(b, 1 + SETTLE, true), "a settled edit applies");
    char *good = ui_buffer_compose(b);
    char *read = text_file_read(path);
    test_check(overlay_is(path, good) && !strcmp(read, good), "the file reads as the edited text");
    free(read);
    text_edit brace = { 0, 0, "}" };
    ui_buffer_edit(b, UI_PART_RCSS, &brace, 1);
    ui_buffer_update(b, 2, true);
    test_check(
        !ui_buffer_update(b, 2 + SETTLE, true) && overlay_is(path, good) &&
            strstr(b->error, "}"),
        "an invalid edit keeps the last valid text and says why"
    );
    ui_buffer_undo(b);
    ui_buffer_update(b, 3, true);
    test_check(
        ui_buffer_update(b, 3 + SETTLE, true) && overlay_is(path, good) && !b->error[0],
        "undoing the invalid edit applies the valid text again"
    );
    test_check(ui_buffer_update(b, 4, false) && overlay_is(path, NULL), "applying on save lets the file stand");
    test_check(ui_buffer_update(b, 5, true) && overlay_is(path, good), "applying live again publishes the edits");
    ui_buffer_undo(b);
    ui_buffer_update(b, 6, true);
    test_check(ui_buffer_update(b, 6 + SETTLE, true) && overlay_is(path, NULL), "text back to the file's lets the file stand");
    ui_buffer_redo(b);
    ui_buffer_update(b, 7, true);
    ui_buffer_update(b, 7 + SETTLE, true);
    ui_buffer_free(b);
    test_check(overlay_is(path, NULL), "closing a document removes its overlay");
    free(good);
}

static void saves_canonical_yaml(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "Saved.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f00; }\n.title { margin: 2dp; }\n", 1);
    ui_buffer_update(b, 1 + SETTLE, true);
    char *text = ui_buffer_compose(b);
    test_check(ui_buffer_on_disk(b), "a file nobody changed is the one loaded");
    write_text(path, text);
    ui_buffer_saved(b, text);
    test_check(
        !ui_buffer_dirty(b) && overlay_is(path, NULL) && ui_buffer_on_disk(b) &&
            !ui_buffer_poll_disk(b, 0),
        "a saved document is clean, its overlay gone, and its own save no "
        "change on disk"
    );
    char *written = read_text(path);
    test_check(
        canonical(written) && !strncmp(written, "rml: |\n  <div", 13) &&
            !strstr(written, "pause panel"),
        "saving writes canonical YAML"
    );
    ui_buffer *again = ui_buffer_open(path, error, sizeof error);
    test_check(
        again && !strcmp(ui_buffer_text(again, UI_PART_RCSS), ui_buffer_text(b, UI_PART_RCSS)) &&
            !strcmp(ui_buffer_text(again, UI_PART_RML), RML),
        "a saved ui asset reads back as the text saved"
    );
    ui_buffer_free(again);
    ui_buffer_undo(b);
    test_check(ui_buffer_dirty(b), "undo past a save makes the document dirty");
    ui_buffer_redo(b);
    test_check(!ui_buffer_dirty(b), "redo back to the save makes it clean");
    free(written);
    free(text);
    ui_buffer_free(b);
}

static void follows_disk(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "Disk.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    test_check(!ui_buffer_poll_disk(b, 0), "an unchanged file is no change");
    write_text(path, "rml: <div>Theirs</div>\n");
    test_check(!ui_buffer_on_disk(b), "another program's write leaves the file not the one loaded");
    test_check(!ui_buffer_poll_disk(b, 0.5), "the disk is polled at most once a second");
    test_check(ui_buffer_poll_disk(b, 1) && !ui_buffer_poll_disk(b, 2), "a change on disk reports once");
    ui_buffer_type(b, UI_PART_RML, "<div>Mine</div>\n", 3);
    test_check(
        ui_buffer_reload(b, error, sizeof error) && !ui_buffer_dirty(b) &&
            !ui_buffer_can_undo(b) &&
            !strcmp(ui_buffer_text(b, UI_PART_RML), "<div>Theirs</div>") &&
            ui_buffer_on_disk(b),
        "reload takes the file's text and drops the edits and history"
    );
    ui_buffer_free(b);
}

static void stylesheets(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "theme.rcss");
    write_text(path, ".a { color: red; }\n");
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    test_check(
        b && b->stylesheet && !ui_buffer_text(b, UI_PART_RML) && b->part == UI_PART_RCSS,
        "a stylesheet opens as its one RCSS part"
    );
    char *text = ui_buffer_compose(b);
    test_check(!strcmp(text, ".a { color: red; }\n"), "a stylesheet's file is its text");
    free(text);
    ui_buffer_type(b, UI_PART_RCSS, ".a { color: red; }}\n", 1);
    ui_buffer_update(b, 1, true);
    test_check(
        !ui_buffer_update(b, 1 + SETTLE, true) && strstr(b->error, "line 1") &&
            overlay_is(path, NULL),
        "an invalid stylesheet says where and applies nothing"
    );
    ui_buffer_free(b);
}

static void checkpoints(const char *folder)
{
    char path[PATH_MAX], error[256];
    join(path, sizeof path, folder, "Checkpoint.yaml");
    write_text(path, WRITTEN);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #f00; }\n", 1);
    ui_buffer_type(b, UI_PART_RML, "<div>Two</div>\n", 3);
    ui_buffer_undo(b);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, ui_buffer_checkpoint(b, doc));
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc *read = yyjson_read(json, strlen(json), 0);
    ui_buffer *restored = ui_buffer_restore(yyjson_doc_get_root(read));
    test_check(
        restored && !strcmp(restored->path, b->path) &&
            !strcmp(ui_buffer_text(restored, UI_PART_RCSS), ui_buffer_text(b, UI_PART_RCSS)) &&
            ui_buffer_dirty(restored) && restored->part == b->part &&
            acount(restored->undo) == 1 && acount(restored->redo) == 1,
        "a checkpoint keeps a document's text, saved state, part and history"
    );
    ui_buffer_redo(restored);
    test_check(!strcmp(ui_buffer_text(restored, UI_PART_RML), "<div>Two</div>\n"), "a restored history replays");
    yyjson_doc_free(read);
    free(json);
    ui_buffer_free(restored);
    ui_buffer_free(b);
}

static void new_assets(const char *folder)
{
    char error[256];
    test_check(!ui_buffer_new_path(folder, UI_NEW_WIDGET, "", error, sizeof error), "a new asset needs a name");
    test_check(
        !ui_buffer_new_path(folder, UI_NEW_WIDGET, "9Lives", error, sizeof error) &&
            !ui_buffer_new_path(folder, UI_NEW_WIDGET, "Two Words", error, sizeof error) &&
            !ui_buffer_new_path(folder, UI_NEW_WIDGET, "../Up", error, sizeof error),
        "a new asset's name starts with a letter and holds only letters, "
        "digits, - and _"
    );
    for (int kind = 0; kind < UI_NEW_KIND_COUNT; kind++) {
        char name[16];
        snprintf(name, sizeof name, "Fresh_%d", kind);
        char *path = ui_buffer_new_path(folder, (ui_new_kind)kind, name, error, sizeof error);
        assert(path);
        char *text = ui_buffer_template((ui_new_kind)kind);
        write_text(path, text);
        ui_buffer *b = ui_buffer_open(path, error, sizeof error);
        test_check(b && !b->error[0], "a new UI widget, document or stylesheet reads");
        test_check(kind == UI_NEW_STYLESHEET || canonical(text), "a new ui asset is canonical YAML");
        test_check(
            kind != UI_NEW_WIDGET || strstr(ui_buffer_text(b, UI_PART_RML), "<slot/>"),
            "a new widget has a slot for its use's content"
        );
        test_check(
            !ui_buffer_new_path(folder, (ui_new_kind)kind, name, error, sizeof error) &&
                strstr(error, "exists"),
            "a new asset's name must not be taken"
        );
        ui_buffer_free(b);
        free(text);
        free(path);
    }
}

void test_ui_documents()
{
    char folder[PATH_MAX];
    temporary_folder(folder, sizeof folder);
    opens_parts(folder);
    edits_and_history(folder);
    live_apply(folder);
    saves_canonical_yaml(folder);
    follows_disk(folder);
    stylesheets(folder);
    checkpoints(folder);
    new_assets(folder);
    text_overlay_clear();
    remove_folder(folder);
}

/* ---------- Document tabs and the Text Editor (hidden window) ---------- */

#define PROJECT "test/fixtures/studio"

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
} tabs_fixture;

static tabs_fixture fixture_new(bool with_studio)
{
    tabs_fixture f = { .world = ecs_world_new() };
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
    assert(app_scene_authoring_load(f.scene, PROJECT "/content/scenes/Main.yaml") == 0);
    f.studio = with_studio ? studio_new(f.scene, f.viewport, PROJECT) : NULL;
    return f;
}

static void fixture_free(tabs_fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
}

static void frames(tabs_fixture *f, int count)
{
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

static scene_document_tab *active(app_scene_authoring *scene)
{
    return &scene->tabs[scene->active_tab];
}

static void document_tabs(const char *folder)
{
    tabs_fixture f = fixture_new(false);
    app_scene_authoring *scene = f.scene;
    char path[PATH_MAX], spelled[PATH_MAX];
    join(path, sizeof path, folder, "Tab.yaml");
    write_text(path, WRITTEN);
    /* Another project's asset, however its path is spelled. */
    snprintf(spelled, sizeof spelled, "%s/../%s/Tab.yaml", folder, strrchr(folder, '/') + 1);
    int tabs = acount(scene->tabs);
    test_check(
        scene_tabs_open_ui(scene, spelled) == 0 &&
            active(scene)->kind == APP_EDITOR_DOCUMENT_UI &&
            scene->document.kind == APP_EDITOR_DOCUMENT_UI &&
            !strcmp(scene->document.id, path),
        "a ui asset opens as the active document, keyed by its absolute path"
    );
    test_check(
        scene_tabs_open_ui(scene, path) == 0 && acount(scene->tabs) == tabs + 1,
        "opening an open ui asset activates its tab"
    );
    scene_document_tab *tab = active(scene);
    ui_buffer_type(tab->ui, UI_PART_RCSS, ".panel { color: #0f0; }\n", 1);
    test_check(scene_tabs_dirty(scene, tab), "a ui document's edits make its tab dirty");
    test_check(
        scene_tabs_activate(scene, 0) == 0 &&
            scene->document.kind == APP_EDITOR_DOCUMENT_SCENE,
        "the scene tab activates"
    );
    int index = (int)(scene_tabs_ui(scene, path) - scene->tabs);
    test_check(
        scene_tabs_activate(scene, index) == 0 &&
            scene->document.kind == APP_EDITOR_DOCUMENT_UI &&
            scene_tabs_dirty(scene, active(scene)),
        "a ui document's tab returns with its edits"
    );
    tab = active(scene);
    test_check(save_active_document(scene) == 0 && !scene_tabs_dirty(scene, tab), "Save writes the active ui document");
    char *saved = read_text(path);
    test_check(canonical(saved) && strstr(saved, "#0f0"), "the saved ui asset is canonical YAML with the edit");
    free(saved);
    write_text(path, "rml: <div>Theirs</div>\n");
    ui_buffer_type(tab->ui, UI_PART_RML, "<div>Mine</div>\n", 5);
    test_check(
        save_active_document(scene) != 0 &&
            strstr(app_scene_authoring_message(scene), "changed on disk"),
        "Save refuses a file another program changed"
    );
    app_disk_change *changes = app_scene_authoring_sync_disk(scene, 100);
    test_check(
        acount(changes) == 1 && changes[0].result == APP_DISK_UNSAVED &&
            !strcmp(changes[0].path, path),
        "a changed file under unsaved edits waits for Reload"
    );
    app_disk_changes_free(changes);
    test_check(
        app_scene_authoring_reload_path(scene, path) == APP_DISK_RELOADED &&
            !scene_tabs_dirty(scene, tab) &&
            !strcmp(ui_buffer_text(tab->ui, UI_PART_RML), "<div>Theirs</div>"),
        "Reload replaces the edits with the file"
    );
    write_text(path, "rml: <div>Again</div>\n");
    changes = app_scene_authoring_sync_disk(scene, 200);
    test_check(
        acount(changes) == 1 && changes[0].result == APP_DISK_RELOADED &&
            !strcmp(ui_buffer_text(tab->ui, UI_PART_RML), "<div>Again</div>"),
        "a clean ui document follows its file"
    );
    app_disk_changes_free(changes);
    ui_buffer_type(tab->ui, UI_PART_RML, "<div>Live</div>\n", 300);
    ui_buffer_update(tab->ui, 300, true);
    ui_buffer_update(tab->ui, 300 + SETTLE, true);
    test_check(text_overlay_text(path) != NULL, "live edits stand in for the file");
    scene_tabs_close(scene, APP_EDITOR_DOCUMENT_UI, path);
    test_check(
        !scene_tabs_ui(scene, path) && !text_overlay_text(path) &&
            scene->document.kind != APP_EDITOR_DOCUMENT_UI,
        "closing a ui document's tab drops its edits and overlay"
    );
    fixture_free(&f);
}

static void opens_from_index(const char *folder)
{
    tabs_fixture f = fixture_new(false);
    char content[PATH_MAX], ui[PATH_MAX], path[PATH_MAX];
    join(content, sizeof content, folder, "content");
    join(ui, sizeof ui, content, "ui");
    assert(mkdir(content, 0777) == 0 && mkdir(ui, 0777) == 0);
    join(path, sizeof path, ui, "Indexed.yaml");
    write_text(path, WRITTEN);
    assert(app_scene_authoring_set_content_root(f.scene, content) == 0);
    app_scene_authoring_refresh_asset_index(f.scene);
    const reference_index *index = app_scene_authoring_asset_index(f.scene);
    int asset = reference_index_find_name(index, REFERENCE_ASSET_UI, "Indexed");
    test_check(
        asset >= 0 && app_scene_authoring_open_asset(f.scene, asset) == 0 &&
            active(f.scene)->kind == APP_EDITOR_DOCUMENT_UI &&
            !strcmp(content_basename(active(f.scene)->path), "Indexed.yaml"),
        "Assets, the palette and Go to Definition open a ui asset as a document"
    );
    fixture_free(&f);
}

static void checkpoint_roundtrip(const char *folder)
{
    tabs_fixture f = fixture_new(false);
    char path[PATH_MAX];
    join(path, sizeof path, folder, "Held.yaml");
    write_text(path, WRITTEN);
    assert(scene_tabs_open_ui(f.scene, path) == 0);
    ui_buffer_type(active(f.scene)->ui, UI_PART_RCSS, ".panel { color: #abc; }\n", 1);
    char *json = app_scene_authoring_checkpoint(f.scene);
    assert(json);
    fixture_free(&f);
    f = fixture_new(false);
    test_check(app_scene_authoring_restore_checkpoint(f.scene, json) == 0, "a workspace with a ui document restores");
    scene_document_tab *tab = scene_tabs_ui(f.scene, path);
    test_check(
        tab && tab == active(f.scene) && f.scene->document.kind == APP_EDITOR_DOCUMENT_UI &&
            scene_tabs_dirty(f.scene, tab) && ui_buffer_can_undo(tab->ui) &&
            strstr(ui_buffer_text(tab->ui, UI_PART_RCSS), "#abc"),
        "the restored ui document keeps its tab, edits and history"
    );
    free(json);
    fixture_free(&f);
}

static void text_editor(const char *folder)
{
    tabs_fixture f = fixture_new(true);
    studio *s = f.studio;
    char path[PATH_MAX];
    join(path, sizeof path, folder, "Edited.yaml");
    write_text(path, WRITTEN);
    s->prefs.code_apply_live = true;
    test_check(studio_open_ui_source(s, path, 1), "Edit Source opens a ui asset's RML");
    frames(&f, 3);
    bool shown = false;
    for (int area = 0; area < STUDIO_AREA_COUNT; area++) {
        shown |= s->prefs.editors[area] == STUDIO_EDITOR_CODE &&
                 (area != STUDIO_AREA_DRAWER || s->prefs.drawer_open);
    }
    test_check(shown && !strcmp(s->code.path, path), "the Text Editor shows the opened document");
    ui_buffer *b = studio_ui_buffer(s);
    ui_buffer_type(b, UI_PART_RCSS, ".panel { color: #123; }\n", s->time);
    frames(&f, 1);
    test_check(!text_overlay_text(path), "an edit applies once it settles");
    frames(&f, 15);
    test_check(text_overlay_text(path) && strstr(text_overlay_text(path), "#123"), "a settled edit applies live across Studio");
    studio_history(s, false);
    test_check(!ui_buffer_dirty(b), "Undo steps the active ui document");
    studio_history(s, true);
    s->prefs.code_apply_live = false;
    frames(&f, 2);
    test_check(!text_overlay_text(path), "On Save leaves the file standing");
    studio_save(s);
    frames(&f, 1);
    char *saved = read_text(path);
    test_check(!ui_buffer_dirty(b) && strstr(saved, "#123") && canonical(saved), "Save writes the text as canonical YAML");
    free(saved);
    test_check(
        studio_open_ui_part(s, path, UI_PART_RCSS, 2) &&
            b->part == UI_PART_RCSS && s->code.reveal == 2,
        "a style origin opens its line of the rcss block"
    );
    frames(&f, 4);
    test_check(!s->code.reveal, "the Text Editor selects the line once it is laid out");
    /* Workstream C: the code editor over the part types into the buffer and
       follows edits made elsewhere. */
    kit_code_editor *e = s->code.editor;
    test_check(e && !strcmp(kit_code_text(e, NULL), ui_buffer_text(b, UI_PART_RCSS)), "the code editor shows the part");
    s->prefs.code_vim = false;
    frames(&f, 1);
    rml_backend_process_input(
        s->backend,
        &(rml_input){ .type = RML_INPUT_TEXT_INPUT, .text = "x", .text_size = 1 }
    );
    frames(&f, 1);
    test_check(strstr(ui_buffer_text(b, UI_PART_RCSS), "\nx") != NULL, "typing at the revealed line reaches the buffer");
    ui_buffer_edit(b, UI_PART_RCSS, &(text_edit){ 0, 0, "/* top */\n" }, 1);
    frames(&f, 1);
    test_check(!strcmp(kit_code_text(e, NULL), ui_buffer_text(b, UI_PART_RCSS)), "the code editor follows an edit made elsewhere");
    frames(&f, 1);
    const char *text = kit_code_text(e, NULL);
    bool selector = false;
    for (int i = 0; i < acount(s->code.spans); i++) {
        const kit_code_span *span = &s->code.spans[i];
        selector |= !strcmp(span->cls, "tok-keyword tok-selector") &&
                    !strncmp(text + span->start, ".panel", 6);
    }
    test_check(selector, "the code editor highlights the part as ui_source parses it");
    fixture_free(&f);
}

void test_ui_document_tabs()
{
    char folder[PATH_MAX];
    temporary_folder(folder, sizeof folder);
    document_tabs(folder);
    opens_from_index(folder);
    checkpoint_roundtrip(folder);
    text_editor(folder);
    text_overlay_clear();
    remove_folder(folder);
}
