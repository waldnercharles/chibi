/* Properties: prototype components, enum, vector and live fields, and an
 * asset dropped onto a field. */

#include "assets/ui_events.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/editor/studio_documents.h"
#include "test/suite_internal.h"
#include "ui/input_feel.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Find a Properties row by its visible label so the test follows the authored
   pane's slots instead of depending on a generated kit path. */
static rml_element *property_row(studio *s, const char *label)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int d = 0; d < rml_context_document_count(context); d++) {
        rml_element *rows[128];
        int count = rml_element_query_selector_all(
            rml_context_document(context, d),
            "[data-ui=Properties] .prop",
            rows,
            128
        );
        for (int i = 0; i < count; i++) {
            rml_element *text = rml_element_query_selector(rows[i], ".label span");
            char shown[128] = "";
            if (text && rml_element_inner_rml(text, shown, sizeof shown) &&
                !strcmp(shown, label)) {
                return rows[i];
            }
        }
    }
    return NULL;
}

/* Keep the selected actor's Object fields visible when this fixture also has
   an active graph node, which normally drives the inspector to Node. */
static void show_object_properties(studio *s)
{
    studio_show_editor(s, STUDIO_EDITOR_PROPERTIES);
    s->shown_node = studio_graph_node(s) ? studio_graph_node(s)
                                         : studio_graph_comment(s);
    s->shown_member = studio_member_shown(s);
    s->shown_actor = s->scene->selected;
    s->tab = STUDIO_TAB_OBJECT;
}

/* Optional visual evidence for the focused Properties run. */
static void capture_properties_screenshot(studio_fixture *f, const char *name)
{
    const char *directory = getenv("CHIBI_PROPERTIES_CAPTURE_DIR");
    if (!directory || !directory[0]) {
        return;
    }
    frames(f, 12);
    char path[PATH_MAX];
    int length = snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(length > 0 && (size_t)length < sizeof path);
    assert(app_save_screenshot(path) == 0);
}

/* Compute a line's baseline from the same face metrics RmlUi uses to draw it.
   Comparing a property row's outer boxes misses differences in line-height. */
static float text_baseline(rml_element *line)
{
    rml_vec2 at = rml_element_absolute_offset(line, RML_BOX_CONTENT);
    rml_vec2 size = rml_element_box_size(line, RML_BOX_CONTENT);
    uintptr_t face = rml_font_face("inter", RML_FONT_NORMAL, 400, 12);
    assert(face);
    rml_font_metrics metrics = rml_font_face_metrics(face);
    return at.y + (size.y - metrics.line_spacing) * 0.5f + metrics.ascent;
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

/* Prototype Properties opens on its own context and keeps a valid component
   tab selected while that document remains active. */
static void prototype_properties_follow_document()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    s->maximized = s->focus_area = STUDIO_AREA_PROPERTIES;
    s->tab = STUDIO_TAB_SCENE;
    char error[256];
    assert(component_presentation_read(
        "components { GAME.GameUi { tab \"Gameplay\"; }; }\n",
        &f.scene->registry,
        &s->presentation.hints,
        error,
        sizeof error
    ));
    assert(app_scene_authoring_open_prototype(f.scene, "Vault") == 0);
    frames(&f, 3);
    kit_rect rect;
    bool object = s->tab == STUDIO_TAB_OBJECT &&
                  !kit_find(s->kit, "app/workspace/main/properties/body/properties/tabs/tab0", &rect);
    s->tab = STUDIO_TAB_GAMEPLAY;
    frames(&f, 2);
    bool retained = s->tab == STUDIO_TAB_GAMEPLAY &&
                    kit_find(s->kit, "app/workspace/main/properties/body/properties/tabs/tab4", &rect);
    test_check(object, "opening a prototype leaves the unrelated Scene tab and selects Object");
    test_check(retained, "Properties keeps a valid component tab selected for the active prototype");
    fixture_free(&f);
}

/* Add Component searches the components the prototype lacks and adds the
   chosen one; its panel's menu removes it, and the toast's Undo returns it. */
static void prototype_components()
{
    studio_fixture f = side_open();
    studio *s = f.studio;
    /* Properties fills the workspace. Scroll its final action into view. */
    s->maximized = s->focus_area = STUDIO_AREA_PROPERTIES;
    assert(app_scene_authoring_open_prototype(f.scene, "Vault") == 0);
    frames(&f, 3);
    const char *scroll = "app/workspace/main/properties/body/properties/scroll";
    char path[256];
    snprintf(path, sizeof path, "%s/add/component", scroll);
    rml_element *add_button = studio_element(s, ".prop-scroll .add-row .btn");
    assert(add_button);
    rml_element_scroll_into_view(add_button, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    frames(&f, 2);
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
    test_check(
        button && palette && gained,
        "Add Component searches the components a prototype lacks and adds "
        "the one chosen"
    );
    test_check(
        shown && menu && removed && undo && restored && undone && redone,
        "a component panel's menu removes it with Undo in a toast, and "
        "Undo and Redo step both"
    );
    fixture_free(&f);
}

/* A large reflected enum keeps its name in Properties and can still be
   changed from the control. GameControl's Aim Stick is past the old cap. */
static void large_enum_property()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Source");
    frames(&f, 4);

    rml_element *tabs = studio_element(s, "[data-ui=Properties] #tabs");
    rml_element *scroll = studio_element(s, "[data-ui=Properties] #scroll");
    rml_element *row = property_row(s, "Source");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Source");
    }
    rml_element *label = row ? rml_element_query_selector(row, ".label span") : NULL;
    rml_element *field = row ? rml_element_query_selector(row, ".field.select") : NULL;
    rml_element *value = row ? rml_element_query_selector(row, ".field.select .value")
                             : NULL;
    char shown[64] = "";
    if (value) {
        rml_element_inner_rml(value, shown, sizeof shown);
    }
    bool mounted = tabs && scroll;
    bool readable = row && field && !strcmp(shown, "Aim Stick");
    float label_baseline = label ? text_baseline(label) : 0;
    float value_baseline = value ? text_baseline(value) : 0;
    bool aligned = label && value && fabsf(label_baseline - value_baseline) <= 1.5f;

    if (field) {
        press(s, field);
        frames(&f, 2);
    }
    rml_element *dropdown = studio_element(s, ".popup.dropdown");
    bool opened = dropdown != NULL;
    rml_element *groups[4], *icons[64];
    int group_count = dropdown
                        ? rml_element_query_selector_all(dropdown, ".menu-group", groups, 4)
                        : 0;
    int icon_count = dropdown
                       ? rml_element_query_selector_all(dropdown, ".menu-item .lead", icons, 64)
                       : 0;
    bool grouped = group_count == 3 && icon_count > 32;
    if (grouped) {
        const char *expected[] = { "Mouse", "Keyboard", "Controller" };
        for (int i = 0; i < group_count; i++) {
            char group[32] = "";
            rml_element_inner_rml(groups[i], group, sizeof group);
            grouped &= !strcmp(group, expected[i]);
        }
        for (int i = 0; i < icon_count; i++) {
            char icon[16] = "";
            rml_element_inner_rml(icons[i], icon, sizeof icon);
            grouped &= icon[0] != '\0';
        }
    }
    capture_properties_screenshot(&f, "properties-enum-dropdown");
    text_event("L");
    frames(&f, 2);
    int search_group_count = dropdown
                               ? rml_element_query_selector_all(dropdown, ".menu-group", groups, 4)
                               : 0;
    int search_icon_count = dropdown
                              ? rml_element_query_selector_all(dropdown, ".menu-item .lead", icons, 64)
                              : 0;
    bool all_search_groups = search_group_count == 3 && search_icon_count > 2;
    if (all_search_groups) {
        const char *expected[] = { "Mouse", "Keyboard", "Controller" };
        for (int i = 0; i < search_group_count; i++) {
            char group[32] = "";
            rml_element_inner_rml(groups[i], group, sizeof group);
            bool known = false;
            for (int j = 0; j < 3; j++) {
                known |= !strcmp(group, expected[j]);
            }
            all_search_groups &= known;
        }
        for (int i = 0; i < search_icon_count; i++) {
            char icon[16] = "";
            rml_element_inner_rml(icons[i], icon, sizeof icon);
            all_search_groups &= icon[0] != '\0';
        }
    }
    rml_element *search = dropdown
                            ? rml_element_query_selector(dropdown, ".field.search .value")
                            : NULL;
    char query[16] = "";
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    all_search_groups &= !strcmp(query, "L");
    capture_properties_screenshot(&f, "properties-grouped-search");
    tap(SDL_SCANCODE_BACKSPACE);
    frames(&f, 1);
    text_event("Mouse Left");
    frames(&f, 2);
    rml_element *mouse_left = dropdown
                                ? rml_element_query_selector(dropdown, ".dropdown-results .menu-item .label")
                                : NULL;
    char option[64] = "";
    if (mouse_left) {
        rml_element_inner_rml(mouse_left, option, sizeof option);
    }
    bool filtered = mouse_left && !strcmp(option, "Mouse Left");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    pin_value source = { 0 };
    bool editable = filtered &&
                    ecs_component_read_field(
                        f.scene->world,
                        f.scene->actors[0].entity,
                        GAME_COMPONENT_GameActionBinding.id,
                        0,
                        &source
                    ) == ECS_OK &&
                    source.type == PIN_ENUM && source.e == GAME_CONTROL_MOUSE_LEFT;
    if (!(mounted && readable && aligned && opened && filtered && editable &&
          grouped && all_search_groups)) {
        printf(
            "  %d %d %d %d %d %d %d %d (baselines %g / %g, groups %d, icons "
            "%d, search groups %d, icons %d, query '%s')\n",
            mounted,
            readable,
            aligned,
            opened,
            filtered,
            editable,
            grouped,
            all_search_groups,
            label_baseline,
            value_baseline,
            group_count,
            icon_count,
            search_group_count,
            search_icon_count,
            query
        );
    }
    test_check(mounted, "Properties uses its authored tab and scroll slots");
    test_check(aligned, "Properties label and value text baselines align");
    test_check(
        readable && opened && filtered && editable && grouped && all_search_groups,
        "a reflected enum beyond 32 options stays named, grouped, iconed "
        "and editable in Properties"
    );
    fixture_free(&f);
}

/* A reflected vector component uses one joined number control and keeps the
   rest of the vector intact through Undo and Redo. */
static void component_vector_property()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Target");
    frames(&f, 4);

    rml_element *row = property_row(s, "Target");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Target");
    }
    rml_element *axes[4];
    int axis_count = row ? rml_element_query_selector_all(row, ".field.number", axes, 4)
                         : 0;
    bool controls = axis_count == 2;
    bool clicked = false;
    if (controls) {
        press(s, axes[0]);
        clicked = rml_element_has_attribute(axes[0], "data-editing");
    }
    text_event("9");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    const GameDrive
        *drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool edited = drive && drive->target.x == 9.0f && drive->target.y == 4.0f;
    capture_properties_screenshot(&f, "properties-vector-edited");
    studio_history(s, false);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool undone = drive && drive->target.x == 3.0f && drive->target.y == 4.0f;
    studio_history(s, true);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool redone = drive && drive->target.x == 9.0f && drive->target.y == 4.0f;
    if (!(controls && clicked && edited && undone && redone)) {
        printf(
            "  %d %d %d %d %d (%g, %g)\n",
            controls,
            clicked,
            edited,
            undone,
            redone,
            drive ? drive->target.x : -1.0f,
            drive ? drive->target.y : -1.0f
        );
    }
    test_check(controls && clicked && edited, "a reflected component vector is editable by axis in Properties");
    test_check(
        undone && redone,
        "component vector edits retain owned before and "
        "after values for Undo and Redo"
    );
    fixture_free(&f);
}

/* While the scene runs, a Properties field edits the running world without
   marking the scene unsaved or writing history, and the header says Live;
   Keep Changes (K) applies the running value when the scene stops, as one
   Undo step. */
static void live_property_edit()
{
    studio_fixture f = fixture_open(PROJECT, PROJECT "/content/scenes/Binding.kdl", "Weapons");
    f.studio = studio_new(f.scene, f.viewport, "projects/studio");
    studio *s = f.studio;
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Target");
    scene_history_clear(f.scene->history);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 2);
    studio_runtime(s, APP_RUNTIME_PLAY);
    f.scene->scene_dirty = false;
    frames(&f, 3);
    bool live = studio_live(s) && studio_element(s, ".area-head.live") != NULL;
    rml_element *row = property_row(s, "Target");
    if (row) {
        rml_element_scroll_into_view(row, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        frames(&f, 2);
        row = property_row(s, "Target");
    }
    rml_element *axes[4];
    int axis_count = row ? rml_element_query_selector_all(row, ".field.number", axes, 4)
                         : 0;
    bool editable = axis_count == 2 &&
                    !rml_element_has_attribute(axes[0], "data-readonly");
    if (editable) {
        press(s, axes[0]);
    }
    text_event("9");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    const GameDrive
        *drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool written = drive && drive->target.x == 9.0f;
    bool clean = !f.scene->scene_dirty && !scene_history_can_undo(f.scene->history);
    bool chord = studio_command_for_key(&s->prefs, SDLK_K, 0, STUDIO_EDITOR_PROPERTIES, s->mac) ==
                     STUDIO_COMMAND_KEEP_CHANGES &&
                 studio_command_for_key(&s->prefs, SDLK_K, 0, STUDIO_EDITOR_BEHAVIOR, s->mac) !=
                     STUDIO_COMMAND_KEEP_CHANGES;
    studio_command_run(s, STUDIO_COMMAND_KEEP_CHANGES);
    frames(&f, 2);
    bool kept = acount(f.scene->kept) == 1 && s->keep.toast;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 2);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool applied = drive && drive->target.x == 9.0f && f.scene->scene_dirty &&
                   s->undo_toast && !studio_live(s);
    studio_history(s, false);
    drive = GAME_GameDrive_get_const(f.scene->world, f.scene->actors[0].entity);
    bool undone = drive && drive->target.x == 3.0f &&
                  !scene_history_can_undo(f.scene->history);
    if (!(live && editable && written && clean && chord && kept && applied && undone)) {
        printf("  %d %d %d %d %d %d %d %d\n", live, editable, written, clean, chord, kept, applied, undone);
    }
    test_check(
        live && editable && written && clean,
        "while the scene runs, Properties edits the running world without "
        "an unsaved mark or Undo step"
    );
    test_check(
        chord && kept && applied && undone,
        "Keep Changes (K) applies a running value when the scene stops, as "
        "one Undo step"
    );
    fixture_free(&f);
}

/* The center of `el` in dp, as the pointer reaches it. */
static void element_center(studio *s, rml_element *el, float *x, float *y)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    *x = (at.x + size.x * 0.5f) / ratio;
    *y = (at.y + size.y * 0.5f) / ratio;
}

/* The Vault's ui document, as its GameUi names it. */
static const char *vault_document(studio_fixture *f)
{
    return GAME_GameUi_get_const(f->scene->world, f->scene->actors[0].entity)->document;
}

/* Carries the Bank asset's row onto Properties' Document field, the pointer
   held, Escape pressed there when `cancel`; whether the field took the drag
   and showed Bank while over it. */
static bool carry_bank(studio_fixture *f, bool cancel)
{
    studio *s = f->studio;
    char first[256];
    list_rows(s, "#assets-list > .list-spacer", first, sizeof first);
    rml_element *window = studio_element(s, "#assets-list > .list-spacer");
    rml_element *row = NULL;
    for (int i = 0; window && i < rml_element_child_count(window); i++) {
        char text[4096] = "";
        rml_element *child = rml_element_child(window, i);
        rml_element_inner_rml(child, text, sizeof text);
        row = strstr(text, "Bank") && !strstr(text, ">UI<") ? child : row;
    }
    rml_element *prop = property_row(s, "Document");
    rml_element *picker = prop ? rml_element_query_selector(prop, ".field.picker")
                               : NULL;
    if (!row || !picker) {
        printf("  carry: row=%d picker=%d\n", row != NULL, picker != NULL);
        return false;
    }
    float x, y, px, py;
    element_center(s, row, &x, &y);
    element_center(s, picker, &px, &py);
    mouse_move(s, x, y);
    frames(f, 1);
    graph_drag_button(s, x, y, true);
    frames(f, 1);
    mouse_move(s, x + 12, y);
    frames(f, 2);
    mouse_move(s, px, py);
    frames(f, 3);
    picker = rml_element_query_selector(property_row(s, "Document"), ".field.picker");
    bool over = picker && rml_element_has_attribute(picker, "data-drop-over") &&
                rml_element_has_attribute(picker, "data-drop-preview");
    if (cancel) {
        tap(SDL_SCANCODE_ESCAPE);
        frames(f, 1);
    }
    graph_drag_button(s, px, py, false);
    frames(f, 3);
    return over;
}

/* Typed asset assignment (studio_drop.c): a ui asset dragged from Assets
   onto Properties' Document field shows there while over it, the authored
   value untouched, and lands as one Undo step; Escape puts the drag back. */
static void asset_drop_assigns()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_active_layout(s)->editors[STUDIO_AREA_BOTTOM] = STUDIO_EDITOR_ASSETS;
    s->prefs.assets_tile = STUDIO_ASSETS_LIST;
    s->applied = s->prefs;
    snprintf(s->assets_filter, sizeof s->assets_filter, "Bank");
    select_instance(f.scene, 0);
    show_object_properties(s);
    snprintf(s->property_search, sizeof s->property_search, "Document");
    pin_value elsewhere = { .type = PIN_STR, .str = "Elsewhere" };
    ecs_component_write_field(
        f.scene->world,
        f.scene->actors[0].entity,
        GAME_COMPONENT_GameUi.id,
        0,
        elsewhere
    );
    scene_history_clear(f.scene->history);
    frames(&f, 6);
    bool cancelled = carry_bank(&f, true) &&
                     !strcmp(vault_document(&f), "Elsewhere") &&
                     !scene_history_can_undo(f.scene->history);
    bool over = carry_bank(&f, false);
    bool assigned = !strcmp(vault_document(&f), "Bank") &&
                    scene_history_can_undo(f.scene->history);
    studio_history(s, false);
    bool undone = !strcmp(vault_document(&f), "Elsewhere");
    if (!(cancelled && over && assigned && undone)) {
        printf("  asset drop: %d %d %d %d (%s)\n", cancelled, over, assigned, undone, vault_document(&f));
    }
    test_check(cancelled, "Escape puts a dragged asset back: the field keeps its value and history is untouched");
    test_check(over && assigned && undone, "a ui asset dragged onto its Properties field shows there, lands as one Undo step and Undo restores it");
    fixture_free(&f);
}

void studio_documents_property_field_checks()
{
    large_enum_property();
    component_vector_property();
    live_property_edit();
}

void studio_documents_properties_checks()
{
    prototype_properties_follow_document();
    prototype_components();
    studio_documents_property_field_checks();
    asset_drop_assigns();
}
