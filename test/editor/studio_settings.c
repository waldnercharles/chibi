/* Studio's settings table: the preferences file and the saved keys authored
 * panels bind go through it, and the Settings page's `settings` variable
 * shows and edits it. */
#include "editor/studio/studio_internal.h"
#include "render/ui.h"
#include "support/array.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *text_at(yyjson_val *root, const char *pointer)
{
    return yyjson_get_str(yyjson_ptr_get(root, pointer));
}

/* A file the previous build saved still loads, every setting round-trips
 * under its old key, and the layout keeps its hand-written fields. */
static void prefs_round_trip()
{
    const char *old_file = "{\"version\":1,\"theme\":\"dark\",\"density\":"
                           "\"comfortable\",\"uiScale\":1.5,"
                           "\"reduceMotion\":true,\"wires\":\"orthogonal\","
                           "\"nodes\":{\"shortBindings\":false,"
                           "\"titleFlow\":false,\"compact\":false},\"code\":{"
                           "\"vim\":true,\"applyLive\":false,"
                           "\"fontSize\":20},\"layout\":{\"sideWidth\":400,"
                           "\"drawer\":{\"open\":true},"
                           "\"areas\":[\"scene\",\"behavior\",\"outliner\","
                           "\"properties\",{\"panel\":\"Data\"}]}}";
    studio_prefs p = studio_prefs_read(old_file);
    bool loaded = p.theme == STUDIO_THEME_DARK && p.comfortable &&
                  p.ui_scale == 1.5f && p.reduced_motion &&
                  p.wires == KIT_WIRE_ORTHOGONAL && !p.short_bindings &&
                  !p.title_flow && !p.compact_nodes && p.code_vim &&
                  !p.code_apply_live && p.code_font_size == 20 &&
                  p.side_width == 400 && p.drawer_open &&
                  p.editors[STUDIO_AREA_DRAWER] == STUDIO_EDITOR_PANEL &&
                  !strcmp(p.panels[STUDIO_AREA_DRAWER], "Data");
    char *json = studio_prefs_write(&p);
    studio_prefs back = studio_prefs_read(json);
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    bool keys = !strcmp(text_at(root, "/density"), "comfortable") &&
                !strcmp(text_at(root, "/wires"), "orthogonal") &&
                yyjson_is_false(yyjson_ptr_get(root, "/nodes/compact")) &&
                yyjson_is_false(yyjson_ptr_get(root, "/code/applyLive")) &&
                yyjson_get_num(yyjson_ptr_get(root, "/code/fontSize")) == 20 &&
                yyjson_get_num(yyjson_ptr_get(root, "/uiScale")) == 1.5;
    test_check(loaded && keys && studio_prefs_equal(&p, &back), "preferences load a previous build's file and round-trip through the settings table");
    yyjson_doc_free(doc);
    free(json);
}

static void invalid_values()
{
    studio_prefs defaults = studio_prefs_default();
    const char *invalid = "{\"theme\":\"purple\",\"density\":7,\"uiScale\":1.3,"
                          "\"reduceMotion\":\"yes\","
                          "\"wires\":\"zigzag\",\"nodes\":{\"shortBindings\":1,"
                          "\"titleFlow\":null},"
                          "\"code\":{\"vim\":\"true\",\"fontSize\":2,"
                          "\"applyLive\":[]}}";
    studio_prefs p = studio_prefs_read(invalid);
    studio_prefs out_of_range = studio_prefs_read("{\"uiScale\":9,\"code\":{\"fontSize\":99}}");
    studio_prefs garbage = studio_prefs_read("not json");
    studio_prefs none = studio_prefs_read(NULL);
    test_check(
        studio_prefs_equal(&p, &defaults) &&
            studio_prefs_equal(&out_of_range, &defaults) &&
            studio_prefs_equal(&garbage, &defaults) &&
            studio_prefs_equal(&none, &defaults),
        "a preference of the wrong type, out of range or not a choice keeps "
        "its default"
    );
}

/* Authored panels bind the preferences as saved keys: a panel's change
 * applies, an invalid one is dropped, and the save shows the preferences. */
static void save_mirror()
{
    game_save *save = game_save_new();
    uint64_t seen = UINT64_MAX;
    studio_prefs p = studio_prefs_default();
    studio_prefs_share(&p, save, &seen);
    bool mirrored = !strcmp(game_save_get(save, "theme"), "system") &&
                    !strcmp(game_save_get(save, "codeVim"), "false") &&
                    !strcmp(game_save_get(save, "uiScale"), "1") &&
                    !strcmp(game_save_get(save, "codeFontSize"), "13") &&
                    !strcmp(game_save_get(save, "density"), "compact");
    game_save_set(save, "codeVim", "true");
    game_save_set(save, "theme", "dark");
    game_save_set(save, "uiScale", "7");
    studio_prefs_share(&p, save, &seen);
    bool applied = p.code_vim && p.theme == STUDIO_THEME_DARK && p.ui_scale == 1 &&
                   !strcmp(game_save_get(save, "uiScale"), "1");
    p.comfortable = true;
    studio_prefs_share(&p, save, &seen);
    test_check(mirrored && applied && !strcmp(game_save_get(save, "density"), "comfortable"), "preferences mirror to saved keys, and a panel's valid change applies back");
    game_save_free(save);
}

/* ---------- The page's data ---------- */

typedef struct
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *page;
    studio_prefs prefs;
    studio_command_info commands[3];
    studio_settings_view view;
} page_fixture;

static void page_open(page_fixture *f)
{
    f->doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(f->doc, yyjson_mut_obj(f->doc));
    studio_settings_add(f->doc);
    f->page = yyjson_mut_obj_get(yyjson_mut_doc_get_root(f->doc), "settings");
    f->prefs = studio_prefs_default();
    f->commands[0] = (studio_command_info){ .id = "save",
                                            .label = "Save",
                                            .shortcut = "\xe2\x8c\x98S" };
    f->commands[1] = (studio_command_info){ .id = "search",
                                            .label = "Search",
                                            .shortcut = "Ctrl+Shift+P" };
    f->commands[2] = (studio_command_info){ .id = "reload",
                                            .label = "Reload",
                                            .keywords = "revert disk" };
    f->view = (studio_settings_view){ .prefs = &f->prefs,
                                      .commands = f->commands,
                                      .command_count = 3,
                                      .project = "Arena" };
    apush(f->view.documents, "Hud");
    apush(f->view.documents, "Shell");
    studio_settings_sync(&f->view, f->doc, false);
}

static void page_close(page_fixture *f)
{
    afree(f->view.documents);
    yyjson_mut_doc_free(f->doc);
}

static yyjson_mut_val *member(yyjson_mut_val *object, const char *name)
{
    return yyjson_mut_obj_get(object, name);
}

static void set_text(page_fixture *f, yyjson_mut_val *object, const char *name, const char *text)
{
    ui_data_set_text(f->doc, member(object, name), text);
}

static const char *text_of_page(page_fixture *f, const char *name)
{
    return yyjson_mut_get_str(member(f->page, name));
}

static yyjson_mut_val *row_named(page_fixture *f, const char *key)
{
    yyjson_mut_val *rows = member(f->page, "rows");
    for (size_t i = 0; i < yyjson_mut_arr_size(rows); i++) {
        yyjson_mut_val *row = yyjson_mut_arr_get(rows, i);
        if (!strcmp(yyjson_mut_get_str(member(row, "key")), key)) {
            return row;
        }
    }
    return NULL;
}

/* The keys of the rows shown, joined by spaces. */
static void shown(page_fixture *f, char *out, size_t size)
{
    yyjson_mut_val *rows = member(f->page, "rows");
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < yyjson_mut_arr_size(rows); i++) {
        yyjson_mut_val *row = yyjson_mut_arr_get(rows, i);
        if (yyjson_mut_get_bool(member(row, "shown"))) {
            used += (size_t)snprintf(
                out + used,
                size - used,
                "%s%s",
                used ? " " : "",
                yyjson_mut_get_str(member(row, "key"))
            );
        }
    }
}

static const char *category_text(page_fixture *f, studio_settings_category c, const char *name)
{
    yyjson_mut_val *item = yyjson_mut_arr_get(member(f->page, "categories"), (size_t)c);
    return yyjson_mut_get_str(member(item, name));
}

static bool category_flag(page_fixture *f, studio_settings_category c, const char *name)
{
    yyjson_mut_val *item = yyjson_mut_arr_get(member(f->page, "categories"), (size_t)c);
    return yyjson_mut_get_bool(member(item, name));
}

static bool keys_are(yyjson_mut_val *row, const char *const *keys, int count)
{
    yyjson_mut_val *list = member(row, "keys");
    bool same = (int)yyjson_mut_arr_size(list) == count;
    for (int i = 0; same && i < count; i++) {
        same = !strcmp(yyjson_mut_get_str(yyjson_mut_arr_get(list, (size_t)i)), keys[i]);
    }
    return same;
}

static void page_rows()
{
    page_fixture f;
    page_open(&f);
    char keys[512];
    shown(&f, keys, sizeof keys);
    int rows = (int)yyjson_mut_arr_size(member(f.page, "rows"));
    test_check(
        rows == studio_setting_count + 3 + 1 &&
            !strcmp(keys, "theme density uiScale reduceMotion") &&
            !strcmp(yyjson_mut_get_str(member(f.page, "title")), "Appearance") &&
            category_flag(&f, STUDIO_SETTINGS_PROJECT, "hidden"),
        "the Settings page shows the first category's settings under the "
        "Editor tab"
    );

    set_text(&f, f.page, "query", "vim");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    yyjson_mut_val *vim = row_named(&f, "code.vim");
    test_check(
        !strcmp(keys, "code.vim") && yyjson_mut_get_bool(member(vim, "head")) &&
            !strcmp(category_text(&f, STUDIO_SETTINGS_TEXT, "badge"), "1") &&
            category_flag(&f, STUDIO_SETTINGS_APPEARANCE, "dim") &&
            yyjson_mut_get_num(member(f.page, "matches")) == 1,
        "searching shows the matching settings of every category, headed and "
        "counted"
    );
    set_text(&f, f.page, "query", "animation");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    bool described = !strcmp(keys, "reduceMotion");
    set_text(&f, f.page, "query", "  graph  pins ");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    test_check(described && !strcmp(keys, "nodes.titleFlow"), "search matches keywords and descriptions, every word of the query");

    yyjson_mut_set_bool(member(vim, "value"), true);
    studio_settings_sync(&f.view, f.doc, true);
    bool toggled = f.prefs.code_vim &&
                   !strcmp(yyjson_mut_get_str(member(vim, "value")), "true") &&
                   yyjson_mut_get_bool(member(vim, "modified")) &&
                   category_flag(&f, STUDIO_SETTINGS_TEXT, "dot");
    set_text(&f, vim, "value", yyjson_mut_get_str(member(vim, "default")));
    studio_settings_sync(&f.view, f.doc, true);
    test_check(
        toggled && !f.prefs.code_vim && !yyjson_mut_get_bool(member(vim, "modified")) &&
            !category_flag(&f, STUDIO_SETTINGS_TEXT, "dot"),
        "a row's switch sets its preference and marks it modified; Reset puts "
        "the default back"
    );

    yyjson_mut_val *theme = row_named(&f, "theme"), *scale = row_named(&f, "uiScale");
    set_text(&f, theme, "value", "purple");
    set_text(&f, scale, "value", "1.25");
    studio_settings_sync(&f.view, f.doc, true);
    test_check(
        f.prefs.theme == STUDIO_THEME_SYSTEM &&
            !strcmp(yyjson_mut_get_str(member(theme, "value")), "system") &&
            f.prefs.ui_scale == 1.25f &&
            yyjson_mut_get_bool(member(scale, "modified")),
        "an invalid value is dropped and its row shows the setting again"
    );

    yyjson_mut_val *font = row_named(&f, "code.fontSize");
    bool fraction = !strcmp(yyjson_mut_get_str(member(font, "position")), "0.208333");
    yyjson_mut_set_real(member(font, "position"), 0.2501);
    studio_settings_sync(&f.view, f.doc, true);
    test_check(
        fraction && f.prefs.code_font_size == 14 &&
            !strcmp(yyjson_mut_get_str(member(font, "value")), "14") &&
            !strcmp(yyjson_mut_get_str(member(font, "position")), "0.25"),
        "a slider moves from 0 to 1 and sets its number on a step"
    );
    page_close(&f);
}

static void page_keyboard_and_project()
{
    page_fixture f;
    page_open(&f);
    set_text(&f, f.page, "category", "keyboard");
    studio_settings_sync(&f.view, f.doc, true);
    char keys[512];
    shown(&f, keys, sizeof keys);
    static const char *const SAVE[] = { "\xe2\x8c\x98", "S" };
    static const char *const SEARCH[] = { "Ctrl", "Shift", "P" };
    bool listed = !strcmp(keys, "command.save command.search command.reload") &&
                  keys_are(row_named(&f, "command.save"), SAVE, 2) &&
                  keys_are(row_named(&f, "command.search"), SEARCH, 3) &&
                  keys_are(row_named(&f, "command.reload"), NULL, 0);
    set_text(&f, f.page, "query", "revert");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    test_check(listed && !strcmp(keys, "command.reload"), "Keyboard lists each command's shortcut as keys, and search finds commands");

    set_text(&f, f.page, "query", "");
    set_text(&f, f.page, "scope", "project");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    yyjson_mut_val *root = row_named(&f, "project.ui");
    bool scoped = !strcmp(keys, "project.ui") &&
                  !strcmp(yyjson_mut_get_str(member(f.page, "category")), "project") &&
                  category_flag(&f, STUDIO_SETTINGS_APPEARANCE, "hidden") &&
                  !strcmp(text_of_page(&f, "project"), "Arena") &&
                  yyjson_mut_arr_size(member(root, "choices")) == 3;
    set_text(&f, root, "value", "Hud");
    studio_settings_sync(&f.view, f.doc, true);
    bool chosen = !strcmp(f.view.root, "Hud") &&
                  yyjson_mut_get_bool(member(root, "modified"));
    set_text(&f, root, "value", "Missing");
    studio_settings_sync(&f.view, f.doc, true);
    test_check(
        scoped && chosen && !strcmp(f.view.root, "Hud") &&
            !strcmp(yyjson_mut_get_str(member(root, "value")), "Hud"),
        "the Project tab sets the root document to one of the project's ui "
        "documents"
    );
    page_close(&f);
}

/* ---------- The authored page ---------- */

typedef struct
{
    rml_backend *backend;
    kit *k;
    kit_frame_input in;
    ui_mount *mount;
    rml_listener *listener;
    ui_data data;
    uint64_t writes, save_seen;
    game_save *save; /* the Studio world's, which panels bind */
    rml_element *host;
    page_fixture page;
} mount_fixture;

static void on_event(void *user, const rml_event *event)
{
    ui_mount_listen(user, event);
}

static void on_emit(void *user, const rml_event *event, const char *name, const char *value)
{
    ui_mount_emit(user, event->current, name, value);
}

/* A frame in studio_ui_frame's order: the saved keys, the page, then the
 * preferences shared back to the saved keys. */
static void mount_frame(mount_fixture *f)
{
    ui_data_sync(&f->data, f->save);
    uint64_t writes = rml_data_model_writes(f->data.model);
    if (studio_settings_sync(&f->page.view, f->data.doc, writes != f->writes)) {
        rml_data_model_dirty(f->data.model, "settings");
    }
    f->writes = writes;
    studio_prefs_share(&f->page.prefs, f->save, &f->save_seen);
    kit_begin(f->k, &f->in);
    kit_root(f->k, "app");
    f->host = kit_modal_host(f->k, "Settings");
    if (ui_mount_sync(f->mount, f->host, f->listener, f->in.time)) {
        kit_style_sheet(f->k, "Settings", ui_mount_style(f->mount));
    }
    kit_end(f->k);
    rml_backend_update(f->backend, 1.0 / 60.0);
    f->in.time += 1.0 / 60.0;
}

/* The visible elements `selector` matches in the page. */
static int visible(mount_fixture *f, const char *selector, rml_element **first)
{
    rml_element *matches[256];
    int count = rml_element_query_selector_all(f->host, selector, matches, 256);
    int shown = 0;
    for (int i = 0; i < count && i < 256; i++) {
        if (rml_element_is_visible(matches[i], true)) {
            *first = shown++ ? *first : matches[i];
        }
    }
    return shown;
}

static void click(mount_fixture *f, rml_element *el)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float x = at.x + size.x * 0.5f, y = at.y + size.y * 0.5f;
    rml_input_type types[] = { RML_INPUT_POINTER_MOVE,
                               RML_INPUT_POINTER_DOWN,
                               RML_INPUT_POINTER_UP };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        rml_backend_process_input(f->backend, &(rml_input){ .type = types[i], .x = x, .y = y });
    }
}

static void page_mount()
{
    mount_fixture f = { .in = { .dp_ratio = 1, .width = 1280, .height = 800 },
                        .save = game_save_new() };
    f.backend = rml_backend_create(1280, 800, 1);
    f.k = test_kit_create(rml_backend_context(f.backend), "studio.rml");
    f.mount = ui_mount_new("projects/studio/content", "Settings");
    f.listener = rml_listener_create(on_event, f.mount);
    page_open(&f.page);
    ui_data_init(&f.data, rml_backend_context(f.backend), f.page.doc, on_emit, f.mount);
    f.page.page = yyjson_mut_obj_get(yyjson_mut_doc_get_root(f.page.doc), "settings");
    for (int i = 0; i < 4; i++) {
        mount_frame(&f);
    }
    rml_element *first = NULL;
    int rows = visible(&f, ".setting", &first);
    bool loaded = !ui_mount_error(f.mount)[0] && rows == 4 &&
                  f.page.prefs.code_font_size == 13;
    set_text(&f.page, f.page.page, "query", "vim");
    rml_data_model_dirty(f.data.model, "settings");
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    rml_element *vim = NULL;
    bool one = visible(&f, ".switch input", &vim) == 1;
    if (one) {
        click(&f, vim);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    test_check(loaded && one && f.page.prefs.code_vim, "the Settings page loads its widgets, filters by search and a switch sets its preference");

    rml_element *reset = NULL;
    bool offered = visible(&f, ".setting-reset", &reset) == 1;
    if (offered) {
        click(&f, reset);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    test_check(
        offered && !f.page.prefs.code_vim && !rml_element_has_attribute(vim, "checked"),
        "Reset puts a switched setting back"
    );

    rml_element *items[8];
    int count = rml_element_query_selector_all(f.host, ".nav-item input", items, 8);
    rml_element *graph = NULL;
    for (int i = 0; i < count && i < 8; i++) {
        char value[32];
        if (rml_element_attribute(items[i], "value", value, sizeof value) > 0 &&
            !strcmp(value, "graph")) {
            graph = items[i];
        }
    }
    if (graph) {
        click(&f, graph);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    rml_element *select = NULL, *radio = NULL;
    bool graph_shown = graph && !strcmp(text_of_page(&f.page, "category"), "graph") &&
                       !text_of_page(&f.page, "query")[0] &&
                       visible(&f, "select", &select) == 1 &&
                       visible(&f, ".switch input", &radio) == 3;
    uint64_t settled = rml_data_model_writes(f.data.model);
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    test_check(graph_shown && rml_data_model_writes(f.data.model) == settled, "choosing a category leaves search and shows its controls, which write nothing until used");
    kit_destroy(f.k);
    ui_data_free(&f.data);
    rml_backend_destroy(f.backend);
    ui_mount_free(f.mount);
    rml_listener_destroy(f.listener);
    afree(f.page.view.documents);
    game_save_free(f.save);
}

void test_studio_settings()
{
    prefs_round_trip();
    invalid_values();
    save_mirror();
    page_rows();
    page_keyboard_and_project();
    page_mount();
}
