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
    const char
        *old_file = "{\"version\":1,\"theme\":\"dark\",\"density\":"
                    "\"comfortable\",\"uiScale\":1.5,"
                    "\"reduceMotion\":true,\"wires\":\"orthogonal\","
                    "\"nodes\":{\"shortBindings\":false,"
                    "\"titleFlow\":false,\"compact\":false},\"code\":{"
                    "\"vim\":true,\"applyLive\":false,"
                    "\"fontSize\":20},\"shortcuts\":{"
                    "\"save\":\"Ctrl+Shift+S\"},\"layout\":{\"sideWidth\":400,"
                    "\"drawer\":{\"open\":true},"
                    "\"areas\":[\"scene\",\"behavior\",\"outliner\","
                    "\"properties\",{\"panel\":\"Data\"}]}}";
    studio_prefs p = studio_prefs_read(old_file);
    studio_prefs defaults = studio_prefs_default();
    studio_prefs with_override = defaults;
    snprintf(with_override.shortcuts[STUDIO_COMMAND_SAVE], STUDIO_CHORD_SIZE, "Ctrl+Shift+S");
    bool loaded = p.theme == STUDIO_THEME_DARK && p.comfortable &&
                  p.ui_scale == 1.5f && p.reduced_motion &&
                  p.wires == KIT_WIRE_ORTHOGONAL && !p.short_bindings &&
                  !p.title_flow && !p.compact_nodes && p.code_vim &&
                  !p.code_apply_live && p.code_font_size == 20 &&
                  !strcmp(p.shortcuts[STUDIO_COMMAND_SAVE], "Ctrl+Shift+S") &&
                  p.side_width == 400 && p.drawer_open &&
                  p.editors[STUDIO_AREA_DRAWER] == STUDIO_EDITOR_PANEL &&
                  !strcmp(p.panels[STUDIO_AREA_DRAWER], "Data");
    char *json = studio_prefs_write(&p, NULL);
    studio_prefs back = studio_prefs_read(json);
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    bool keys = !strcmp(text_at(root, "/density"), "comfortable") &&
                !strcmp(text_at(root, "/wires"), "orthogonal") &&
                yyjson_is_false(yyjson_ptr_get(root, "/nodes/compact")) &&
                yyjson_is_false(yyjson_ptr_get(root, "/code/applyLive")) &&
                !strcmp(text_at(root, "/shortcuts/save"), "Ctrl+Shift+S") &&
                yyjson_get_num(yyjson_ptr_get(root, "/code/fontSize")) == 20 &&
                yyjson_get_num(yyjson_ptr_get(root, "/uiScale")) == 1.5;
    test_check(
        loaded && keys && !studio_prefs_equal(&with_override, &defaults) &&
            studio_prefs_equal(&p, &back),
        "preferences load a previous build's file, include shortcut overrides "
        "in change detection, and round-trip through the settings table"
    );
    yyjson_doc_free(doc);
    free(json);

    /* A newer build's settings stay; a reset shortcut does not come back. */
    const char
        *newer = "{\"code\":{\"vim\":true,\"minimap\":true},\"future\":1,"
                 "\"shortcuts\":{\"save\":\"Ctrl+Shift+S\",\"later\":\"F9\"}}";
    json = studio_prefs_write(&defaults, newer);
    doc = yyjson_read(json, strlen(json), 0);
    root = yyjson_doc_get_root(doc);
    bool kept = yyjson_is_true(yyjson_ptr_get(root, "/code/minimap")) &&
                yyjson_is_false(yyjson_ptr_get(root, "/code/vim")) &&
                yyjson_get_int(yyjson_ptr_get(root, "/future")) == 1 &&
                !strcmp(text_at(root, "/shortcuts/later"), "F9") &&
                !yyjson_ptr_get(root, "/shortcuts/save");
    test_check(kept, "saving keeps the settings a newer build wrote and drops a reset shortcut");
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
                          "\"applyLive\":[]},\"shortcuts\":{"
                          "\"save\":\"Mod+Missing\",\"search\":9}}";
    studio_prefs p = studio_prefs_read(invalid);
    studio_prefs out_of_range = studio_prefs_read("{\"uiScale\":9,\"code\":{\"fontSize\":99}}");
    studio_prefs garbage = studio_prefs_read("not json");
    studio_prefs none = studio_prefs_read(NULL);
    test_check(
        studio_prefs_equal(&p, &defaults) &&
            studio_prefs_equal(&out_of_range, &defaults) &&
            studio_prefs_equal(&garbage, &defaults) &&
            studio_prefs_equal(&none, &defaults) &&
            !p.shortcuts[STUDIO_COMMAND_SAVE][0] &&
            !p.shortcuts[STUDIO_COMMAND_SEARCH][0],
        "a preference of the wrong type, out of range or not a choice keeps "
        "its default"
    );
}

static void shortcut_chords()
{
    studio_prefs prefs = studio_prefs_default();
    bool defaults_valid = true;
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        const char *chord = studio_command_default_chord((studio_command)c);
        defaults_valid &= !chord || !chord[0] || studio_chord_valid(chord);
    }
    studio_key key = { 0 };
    bool parsed = studio_chord_parse("Mod+Shift+P", false, &key) &&
                  key.key == SDLK_P && key.mod == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    bool mac = studio_chord_parse("Mod+Shift+P", true, &key) &&
               key.key == SDLK_P && key.mod == (SDL_KMOD_GUI | SDL_KMOD_SHIFT);
    bool matching = studio_chord_matches("Mod+Shift+P", SDLK_P, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, false) &&
                    studio_chord_matches("Mod+Shift+P", SDLK_P, SDL_KMOD_GUI | SDL_KMOD_SHIFT, true) &&
                    !studio_chord_matches("Mod+Shift+P", SDLK_P, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, true) &&
                    studio_chord_matches("Ctrl+P", SDLK_P, SDL_KMOD_CTRL, true);
    char shown[64];
    studio_chord_format(shown, sizeof shown, "Mod+Shift+P", false);
    bool windows_format = !strcmp(shown, "Ctrl+Shift+P");
    studio_chord_format(shown, sizeof shown, "Mod+Shift+P", true);
    bool mac_format = !strcmp(shown, "\xe2\x87\xa7\xe2\x8c\x98P");
    bool valid = studio_chord_valid("Mod+Shift+P") &&
                 studio_chord_valid("Ctrl+P") && !studio_chord_valid("") &&
                 !studio_chord_valid("Mod+") && !studio_chord_valid("NoSuchKey+P");

    prefs.shortcuts[STUDIO_COMMAND_SETTINGS][0] = '\0';
    int conflict = studio_command_conflict(&prefs, STUDIO_COMMAND_SETTINGS, "Mod+S", false);
    bool global_conflict = conflict == STUDIO_COMMAND_SAVE &&
                           studio_command_conflict(&prefs, STUDIO_COMMAND_SAVE, "Mod+S", false) <
                               0;

    test_check(parsed && mac && matching && windows_format && mac_format && valid && defaults_valid, "shortcut chords and every command default parse, match and format on each platform");
    test_check(global_conflict, "a global shortcut conflict names the other command and ignores itself");
}

static void shortcut_dispatch()
{
    studio_prefs prefs = studio_prefs_default();
    SDL_Keymod primary = SDL_KMOD_CTRL;
    int zoom_equals = studio_command_for_key(&prefs, SDLK_EQUALS, primary, STUDIO_EDITOR_SCENE, false);
    int zoom_plus = studio_command_for_key(&prefs, SDLK_PLUS, primary, STUDIO_EDITOR_SCENE, false);
    int zoom_keypad_plus = studio_command_for_key(&prefs, SDLK_KP_PLUS, primary, STUDIO_EDITOR_SCENE, false);
    int zoom_keypad_minus = studio_command_for_key(&prefs, SDLK_KP_MINUS, primary, STUDIO_EDITOR_SCENE, false);
    int zoom_keypad_zero = studio_command_for_key(&prefs, SDLK_KP_0, primary, STUDIO_EDITOR_SCENE, false);
    int scene_frame = studio_command_for_key(&prefs, SDLK_F, 0, STUDIO_EDITOR_SCENE, false);
    int outliner_frame = studio_command_for_key(&prefs, SDLK_F, 0, STUDIO_EDITOR_OUTLINER, false);
    int behavior_frame = studio_command_for_key(&prefs, SDLK_F, 0, STUDIO_EDITOR_BEHAVIOR, false);
    int scene_play = studio_command_for_key(&prefs, SDLK_SPACE, 0, STUDIO_EDITOR_SCENE, false);
    int behavior_play = studio_command_for_key(&prefs, SDLK_SPACE, 0, STUDIO_EDITOR_BEHAVIOR, false);
    int search_alias = studio_command_for_key(&prefs, SDLK_P, SDL_KMOD_CTRL, STUDIO_EDITOR_SCENE, false);
    bool contexts = scene_frame == STUDIO_COMMAND_FRAME &&
                    outliner_frame == STUDIO_COMMAND_FRAME &&
                    behavior_frame < 0 && scene_play == STUDIO_COMMAND_PLAY &&
                    behavior_play < 0 && search_alias == STUDIO_COMMAND_SEARCH &&
                    zoom_equals == STUDIO_COMMAND_ZOOM_IN &&
                    zoom_plus == STUDIO_COMMAND_ZOOM_IN &&
                    zoom_keypad_plus == STUDIO_COMMAND_ZOOM_IN &&
                    zoom_keypad_minus == STUDIO_COMMAND_ZOOM_OUT &&
                    zoom_keypad_zero == STUDIO_COMMAND_ZOOM_RESET;

    snprintf(prefs.shortcuts[STUDIO_COMMAND_SAVE], STUDIO_CHORD_SIZE, "Mod+P");
    bool symmetric = studio_command_conflict(
                         &prefs,
                         STUDIO_COMMAND_SEARCH,
                         studio_command_chord(&prefs, STUDIO_COMMAND_SEARCH),
                         false
                     ) == STUDIO_COMMAND_SAVE &&
                     studio_command_conflict(&prefs, STUDIO_COMMAND_SAVE, "Mod+P", false) ==
                         STUDIO_COMMAND_SEARCH;
    snprintf(prefs.shortcuts[STUDIO_COMMAND_SEARCH], STUDIO_CHORD_SIZE, "Mod+Shift+P");
    bool override_disables_alias = studio_command_for_key(&prefs, SDLK_P, SDL_KMOD_CTRL, STUDIO_EDITOR_SCENE, false) ==
                                       STUDIO_COMMAND_SAVE &&
                                   studio_command_conflict(&prefs, STUDIO_COMMAND_SAVE, "Mod+P", false) <
                                       0;

    test_check(contexts, "shortcut dispatch respects area contexts and keeps Search's unmodified platform alias");
    test_check(symmetric && override_disables_alias, "shortcut conflicts include aliases symmetrically, and an override replaces its defaults");
}

static void shortcut_recording()
{
    studio_prefs prefs = studio_prefs_default();
    int recording = STUDIO_COMMAND_SEARCH + 1;
    bool modifier = studio_shortcut_record(
        &prefs,
        &recording,
        (studio_key){ .key = SDLK_LCTRL, .mod = SDL_KMOD_CTRL },
        false
    );
    bool repeat = studio_shortcut_record(
        &prefs,
        &recording,
        (studio_key){ .key = SDLK_P, .mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT, .repeat = true },
        false
    );
    bool still_recording = modifier && repeat &&
                           recording == STUDIO_COMMAND_SEARCH + 1 &&
                           !prefs.shortcuts[STUDIO_COMMAND_SEARCH][0];
    bool saved = studio_shortcut_record(
                     &prefs,
                     &recording,
                     (studio_key){ .key = SDLK_P, .mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT },
                     false
                 ) &&
                 recording == 0 &&
                 !strcmp(prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+Shift+P");
    bool matches = studio_chord_matches(
        prefs.shortcuts[STUDIO_COMMAND_SEARCH],
        SDLK_P,
        SDL_KMOD_CTRL | SDL_KMOD_SHIFT,
        false
    );

    recording = STUDIO_COMMAND_SEARCH + 1;
    bool canceled = studio_shortcut_record(
                        &prefs,
                        &recording,
                        (studio_key){ .key = SDLK_ESCAPE, .mod = SDL_KMOD_CTRL },
                        false
                    ) &&
                    recording == 0 &&
                    !strcmp(prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+Shift+P");

    recording = STUDIO_COMMAND_SAVE + 1;
    bool mac_saved = studio_shortcut_record(
                         &prefs,
                         &recording,
                         (studio_key){ .key = SDLK_S,
                                       .mod = SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_SHIFT },
                         true
                     ) &&
                     recording == 0 &&
                     !strcmp(prefs.shortcuts[STUDIO_COMMAND_SAVE], "Mod+Ctrl+Shift+S") &&
                     studio_chord_matches(
                         prefs.shortcuts[STUDIO_COMMAND_SAVE],
                         SDLK_S,
                         SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_SHIFT,
                         true
                     );

    recording = STUDIO_COMMAND_SEARCH + 1;
    bool reset_to_default = studio_shortcut_record(
                                &prefs,
                                &recording,
                                (studio_key){ .key = SDLK_K, .mod = SDL_KMOD_CTRL },
                                false
                            ) &&
                            recording == 0 &&
                            !prefs.shortcuts[STUDIO_COMMAND_SEARCH][0];
    test_check(
        still_recording && saved && matches && canceled && mac_saved && reset_to_default,
        "shortcut recording ignores modifiers and repeats, cancels on Escape, "
        "handles Mac modifiers and stores the default as no override"
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
    studio_command_info commands[4];
    studio_settings_view view;
} page_fixture;

static void page_open(page_fixture *f)
{
    f->doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(f->doc, yyjson_mut_obj(f->doc));
    studio_settings_add(f->doc);
    f->page = yyjson_mut_obj_get(yyjson_mut_doc_get_root(f->doc), "settings");
    f->prefs = studio_prefs_default();
    f->commands[0] = (studio_command_info){
        .id = "save",
        .label = "Save",
        .shortcut = studio_command_default_chord(STUDIO_COMMAND_SAVE)
    };
    f->commands[1] = (studio_command_info){
        .id = "search",
        .label = "Search",
        .shortcut = studio_command_default_chord(STUDIO_COMMAND_SEARCH)
    };
    f->commands[2] = (studio_command_info){ .id = "reload",
                                            .label = "Reload",
                                            .keywords = "revert disk" };
    f->commands[3] = (studio_command_info){
        .id = "frame",
        .label = "Frame Selected",
        .shortcut = studio_command_default_chord(STUDIO_COMMAND_FRAME)
    };
    f->view = (studio_settings_view){ .prefs = &f->prefs,
                                      .commands = f->commands,
                                      .command_count = 4,
                                      .mac = false,
                                      .project = "Arena" };
    snprintf(f->view.fields[STUDIO_PROJECT_NAME], sizeof f->view.fields[0], "Arena");
    snprintf(f->view.fields[STUDIO_PROJECT_START_SCENE], sizeof f->view.fields[0], "scenes/Main.yaml");
    apush(
        f->view.choices[STUDIO_PROJECT_START_SCENE],
        ((studio_setting_choice){ "scenes/Main.yaml", "Main" })
    );
    apush(
        f->view.choices[STUDIO_PROJECT_START_SCENE],
        ((studio_setting_choice){ "scenes/Boss.yaml", "Boss" })
    );
    apush(f->view.choices[STUDIO_PROJECT_UI], ((studio_setting_choice){ "Hud", "Hud" }));
    apush(f->view.choices[STUDIO_PROJECT_UI], ((studio_setting_choice){ "Shell", "Shell" }));
    studio_settings_sync(&f->view, f->doc, false);
}

static void page_view_free(studio_settings_view *view)
{
    for (int f = 0; f < STUDIO_PROJECT_FIELD_COUNT; f++) {
        afree(view->choices[f]);
    }
}

static void page_close(page_fixture *f)
{
    page_view_free(&f->view);
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
        rows == studio_setting_count + f.view.command_count + STUDIO_PROJECT_FIELD_COUNT &&
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
    static const char *const SAVE[] = { "Ctrl", "S" };
    static const char *const SEARCH[] = { "Ctrl", "K" };
    yyjson_mut_val *save = row_named(&f, "command.save");
    yyjson_mut_val *search = row_named(&f, "command.search");
    yyjson_mut_val *frame = row_named(&f, "command.frame");
    bool listed = !strcmp(keys, "command.save command.search command.reload command.frame") &&
                  keys_are(row_named(&f, "command.save"), SAVE, 2) &&
                  keys_are(row_named(&f, "command.search"), SEARCH, 2) &&
                  keys_are(row_named(&f, "command.reload"), NULL, 0) &&
                  !strcmp(yyjson_mut_get_str(member(save, "value")), "Mod+S") &&
                  !strcmp(yyjson_mut_get_str(member(save, "default")), "Mod+S") &&
                  yyjson_mut_get_str(member(frame, "context"))[0] != '\0';
    set_text(&f, f.page, "query", "revert");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    test_check(listed && !strcmp(keys, "command.reload"), "Keyboard lists each command's shortcut as keys, and search finds commands");

    set_text(&f, f.page, "query", "");
    set_text(&f, search, "value", "Mod+S");
    studio_settings_sync(&f.view, f.doc, true);
    bool override = !strcmp(f.prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+S") &&
                    !strcmp(yyjson_mut_get_str(member(search, "conflict")), "Also assigned to Save");

    yyjson_mut_set_bool(member(search, "record"), true);
    studio_settings_sync(&f.view, f.doc, true);
    bool recording = f.view.recording == STUDIO_COMMAND_SEARCH + 1 &&
                     yyjson_mut_get_bool(member(search, "recording"));
    bool recorded = studio_shortcut_record(
        &f.prefs,
        &f.view.recording,
        (studio_key){ .key = SDLK_P, .mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT },
        f.view.mac
    );
    /* A data-model write can still report the pre-recording value. The
       settings row's bound value must prevent it from replacing the capture. */
    studio_settings_sync(&f.view, f.doc, true);
    bool captured = recorded && f.view.recording == 0 &&
                    !strcmp(f.prefs.shortcuts[STUDIO_COMMAND_SEARCH], "Mod+Shift+P") &&
                    !strcmp(yyjson_mut_get_str(member(search, "value")), "Mod+Shift+P") &&
                    !yyjson_mut_get_bool(member(search, "recording"));

    set_text(&f, search, "value", "Mod+K");
    studio_settings_sync(&f.view, f.doc, true);
    bool reset = !f.prefs.shortcuts[STUDIO_COMMAND_SEARCH][0] &&
                 !strcmp(yyjson_mut_get_str(member(search, "value")), "Mod+K");
    test_check(override && recording && captured && reset, "Keyboard edits a chord, reports a conflict, records a replacement and stores defaults without an override");

    yyjson_mut_set_bool(member(search, "record"), true);
    studio_settings_sync(&f.view, f.doc, true);
    bool started = f.view.recording == STUDIO_COMMAND_SEARCH + 1;
    set_text(&f, f.page, "query", "");
    set_text(&f, f.page, "scope", "project");
    studio_settings_sync(&f.view, f.doc, true);
    shown(&f, keys, sizeof keys);
    yyjson_mut_val *root = row_named(&f, "project.ui");
    test_check(started && !f.view.recording, "leaving a shortcut row cancels recording");
    bool scoped = !strcmp(keys, "project.name project.startScene project.ui") &&
                  !strcmp(yyjson_mut_get_str(member(f.page, "category")), "project") &&
                  category_flag(&f, STUDIO_SETTINGS_APPEARANCE, "hidden") &&
                  !strcmp(text_of_page(&f, "project"), "Arena") &&
                  yyjson_mut_arr_size(member(root, "choices")) == 3;
    set_text(&f, root, "value", "Hud");
    studio_settings_sync(&f.view, f.doc, true);
    bool chosen = !strcmp(f.view.fields[STUDIO_PROJECT_UI], "Hud") &&
                  yyjson_mut_get_bool(member(root, "modified"));
    set_text(&f, root, "value", "Missing");
    studio_settings_sync(&f.view, f.doc, true);
    test_check(
        scoped && chosen && !strcmp(f.view.fields[STUDIO_PROJECT_UI], "Hud") &&
            !strcmp(yyjson_mut_get_str(member(root, "value")), "Hud"),
        "the Project tab sets the root document to one of the project's ui "
        "documents"
    );

    yyjson_mut_val *name = row_named(&f, "project.name");
    yyjson_mut_val *start = row_named(&f, "project.startScene");
    bool shown_name = !strcmp(yyjson_mut_get_str(member(name, "value")), "Arena") &&
                      yyjson_mut_arr_size(member(name, "text")) == 1 &&
                      !yyjson_mut_get_bool(member(name, "modified"));
    set_text(&f, name, "value", "Neon Swarm");
    set_text(&f, start, "value", "scenes/Boss.yaml");
    studio_settings_sync(&f.view, f.doc, true);
    bool renamed = !strcmp(f.view.fields[STUDIO_PROJECT_NAME], "Neon Swarm") &&
                   !strcmp(f.view.fields[STUDIO_PROJECT_START_SCENE], "scenes/Boss.yaml");
    set_text(&f, name, "value", "");
    set_text(&f, start, "value", "scenes/Missing.yaml");
    studio_settings_sync(&f.view, f.doc, true);
    yyjson_mut_val *first = yyjson_mut_arr_get(member(start, "choices"), 0);
    test_check(
        shown_name && renamed &&
            !strcmp(f.view.fields[STUDIO_PROJECT_NAME], "Neon Swarm") &&
            !strcmp(f.view.fields[STUDIO_PROJECT_START_SCENE], "scenes/Boss.yaml") &&
            yyjson_mut_arr_size(member(start, "choices")) == 2 &&
            !strcmp(yyjson_mut_get_str(member(first, "label")), "Main") &&
            !yyjson_mut_get_bool(member(start, "modified")),
        "the Project tab names the project and picks its start scene, which "
        "neither can leave empty"
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

    count = rml_element_query_selector_all(f.host, ".nav-item input", items, 8);
    rml_element *keyboard = NULL;
    for (int i = 0; i < count && i < 8; i++) {
        char value[32];
        if (rml_element_attribute(items[i], "value", value, sizeof value) > 0 &&
            !strcmp(value, "keyboard")) {
            keyboard = items[i];
        }
    }
    if (keyboard) {
        click(&f, keyboard);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    set_text(&f.page, f.page.page, "query", "Search");
    rml_data_model_dirty(f.data.model, "settings");
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    rml_element *record_button = NULL;
    bool record_visible = keyboard &&
                          visible(&f, ".record-shortcut", &record_button) == 1;
    if (record_visible) {
        click(&f, record_button);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    bool listening = record_visible &&
                     f.page.view.recording == STUDIO_COMMAND_SEARCH + 1 &&
                     visible(&f, ".recording", &record_button) == 1 &&
                     visible(&f, ".record-shortcut", &record_button) == 0;
    bool captured = studio_shortcut_record(
        &f.page.prefs,
        &f.page.view.recording,
        (studio_key){ .key = SDLK_P, .mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT },
        f.page.view.mac
    );
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    rml_element *shortcut_reset = NULL;
    bool reset_visible = visible(&f, ".setting-reset", &shortcut_reset) == 1;
    if (reset_visible) {
        click(&f, shortcut_reset);
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    test_check(
        listening && captured && reset_visible &&
            !f.page.prefs.shortcuts[STUDIO_COMMAND_SEARCH][0],
        "the mounted Keyboard row records a chord and Reset restores its "
        "default"
    );

    set_text(&f.page, f.page.page, "query", "");
    set_text(&f.page, f.page.page, "scope", "project");
    rml_data_model_dirty(f.data.model, "settings");
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    rml_element *name = NULL;
    bool project = visible(&f, ".text-control input", &name) == 1 &&
                   visible(&f, "select", &select) == 2;
    if (project) {
        click(&f, name);
        rml_backend_process_input(
            f.backend,
            &(rml_input){ .type = RML_INPUT_TEXT_INPUT, .text = " Lab", .text_size = 4 }
        );
        click(&f, rml_element_query_selector(f.host, ".page-title"));
    }
    for (int i = 0; i < 3; i++) {
        mount_frame(&f);
    }
    test_check(
        project && !strcmp(f.page.view.fields[STUDIO_PROJECT_NAME], "Arena Lab"),
        "the Project tab shows the name as a text field, commits it on blur, "
        "and shows start scene and root document selects"
    );

    kit_destroy(f.k);
    ui_data_free(&f.data);
    rml_backend_destroy(f.backend);
    ui_mount_free(f.mount);
    rml_listener_destroy(f.listener);
    page_view_free(&f.page.view);
    game_save_free(f.save);
}

void test_studio_settings()
{
    prefs_round_trip();
    invalid_values();
    shortcut_chords();
    shortcut_dispatch();
    shortcut_recording();
    save_mirror();
    page_rows();
    page_keyboard_and_project();
    page_mount();
}
