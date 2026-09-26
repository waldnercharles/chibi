/* Assets: every indexed asset as a virtualized list, filtered by kind and name.
 * Prototypes drag into the Scene or the Outliner to place an instance;
 * behaviors drag onto an Outliner row to attach. Double-click opens the asset.
 * New makes a UI widget, document or stylesheet and opens it. */
#include "editor/studio/studio_internal.h"
#include "editor/workspace/asset_index.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *const KIND_LABELS[REFERENCE_ASSET_KIND_COUNT] = {
    "Scenes", "Prototypes", "Behaviors", "Events",  "Materials",
    "Shapes", "Fields",     "Libraries", "Systems", "UI",
};
static const char *const KIND_NAMES[REFERENCE_ASSET_KIND_COUNT] = {
    "scene", "prototype", "behavior", "event",  "material",
    "shape", "field",     "library",  "system", "ui",
};
static const uint32_t KIND_ICONS[REFERENCE_ASSET_KIND_COUNT] = {
    KIT_ICON_SCENE,    KIT_ICON_PROTOTYPE, KIT_ICON_GRAPH, KIT_ICON_EVENT,
    KIT_ICON_MATERIAL, KIT_ICON_HEXAGON,   KIT_ICON_GRID,  KIT_ICON_LIBRARY,
    KIT_ICON_SYSTEM,   KIT_ICON_LAYOUT,
};

const char *studio_asset_kind_name(int kind)
{
    return KIND_NAMES[kind];
}

uint32_t studio_asset_kind_icon(int kind)
{
    return KIND_ICONS[kind];
}

const char *studio_asset_kind_label(int kind)
{
    return KIND_LABELS[kind];
}

static bool contains(const char *text, const char *filter)
{
    if (!filter[0]) {
        return true;
    }
    for (const char *t = text; *t; t++) {
        const char *a = t, *b = filter;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}

void studio_assets_header(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    const char *kinds[REFERENCE_ASSET_KIND_COUNT + 1] = { "All" };
    for (int i = 0; i < REFERENCE_ASSET_KIND_COUNT; i++) {
        kinds[i + 1] = KIND_LABELS[i];
    }
    int choice = s->assets_kind + 1;
    kit_push(k, "kind", NULL, "assets-kind");
    if (kit_choice(k, "kind", &choice, kinds, REFERENCE_ASSET_KIND_COUNT + 1, 0)) {
        s->assets_kind = choice - 1;
    }
    kit_pop(k);
    kit_push(k, "filter", NULL, "assets-filter");
    kit_search(k, "search", s->assets_filter, sizeof s->assets_filter, "Filter");
    kit_pop(k);
    studio_new_ui_button(s);
}

/* Rows of the list: a group header (asset < 0, kind set) or an asset. */
typedef struct
{
    int asset;
    int kind;
} asset_row;

static const reference_index *sort_index;

static int by_name(const void *a, const void *b)
{
    const reference_asset *x = reference_index_asset_at(sort_index, *(const int *)a);
    const reference_asset *y = reference_index_asset_at(sort_index, *(const int *)b);
    return x->kind != y->kind ? (int)x->kind - (int)y->kind
                              : strcasecmp(x->name, y->name);
}

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1 };
    return kit_menu_item(k, key, &d);
}

/* A row's menu opens the asset or lists what references it (Find References). */
static void asset_menu(studio *s, const char *key)
{
    kit *k = s->kit;
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    const reference_asset *asset = reference_index_asset_at(index, s->assets_menu);
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    if (asset) {
        kit_menu_group(k, "group", asset->name);
    }
    if (asset && menu_item(k, "open", "Open", KIT_ICON_EXTERNAL, NULL)) {
        finish_transform_edit(s->scene, false);
        app_scene_authoring_open_asset(s->scene, s->assets_menu);
    }
    if (asset && studio_asset_nameable(asset, REFERENCE_ASSET_UI) &&
        menu_item(k, "preview", "Preview", KIT_ICON_LAYOUT, NULL)) {
        studio_preview_ui(s, asset->name);
    }
    if (asset && menu_item(k, "refs", "Find References", KIT_ICON_REFERENCES, NULL)) {
        studio_find_asset_references(s, s->assets_menu);
    }
    if (studio_asset_refactorable(asset)) {
        if (menu_item(k, "rename", "Rename", KIT_ICON_EDIT, "F2")) {
            studio_refactor_rename_asset(s, s->assets_menu);
        }
        kit_menu_separator(k, "s0");
        if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X")) {
            studio_refactor_delete_asset(s, s->assets_menu);
        }
    }
    kit_popup_end(k);
}

/* All kinds show as collapsible groups sorted by name, each asset with how often
   it is used, so unused and unresolved assets stand out. One kind is a flat list. */
void studio_assets_body(studio *s, int area)
{
    kit *k = s->kit;
    char menu_key[24];
    snprintf(menu_key, sizeof menu_key, "asset:%d", area);
    app_scene_authoring *scene = s->scene;
    const reference_index *index = app_scene_authoring_asset_index(scene);
    int count = index ? reference_index_asset_count(index) : 0;
    int *uses = xcalloc((size_t)(count ? count : 1), sizeof *uses);
    for (int r = 0; index && r < reference_index_count(index); r++) {
        int used = reference_dependency(reference_index_at(index, r));
        if (used >= 0) {
            uses[used]++;
        }
    }
    int *matches = NULL;
    for (int i = 0; i < count; i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        if (asset->name && asset->kind >= 0 && asset->kind < REFERENCE_ASSET_KIND_COUNT &&
            (s->assets_kind < 0 || (int)asset->kind == s->assets_kind) &&
            contains(asset->name, s->assets_filter)) {
            apush(matches, i);
        }
    }
    sort_index = index;
    if (acount(matches)) {
        qsort(matches, (size_t)acount(matches), sizeof *matches, by_name);
    }
    bool grouped = s->assets_kind < 0;
    asset_row *rows = NULL;
    for (int m = 0; m < acount(matches); m++) {
        int kind = (int)reference_index_asset_at(index, matches[m])->kind;
        if (grouped &&
            (m == 0 ||
             (int)reference_index_asset_at(index, matches[m - 1])->kind != kind)) {
            apush(rows, ((asset_row){ -1, kind }));
        }
        if (!grouped || !(s->assets_collapsed & (1u << kind)) || s->assets_filter[0]) {
            apush(rows, ((asset_row){ matches[m], kind }));
        }
    }
    s->assets_hover = -1;
    kit_list_desc list = { .count = acount(rows),
                           .row_height = s->prefs.comfortable ? 26 : 22,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "assets", "assets-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(rows); r++) {
        char key[24], meta[24];
        if (rows[r].asset < 0) {
            int kind = rows[r].kind, total = 0;
            for (int m = 0; m < acount(matches); m++) {
                total += (int)reference_index_asset_at(index, matches[m])->kind == kind;
            }
            snprintf(key, sizeof key, "g%d", kind);
            snprintf(meta, sizeof meta, "%d", total);
            bool open = !(s->assets_collapsed & (1u << kind)) || s->assets_filter[0];
            kit_row_desc header = { .label = KIND_LABELS[kind],
                                    .icon = KIND_ICONS[kind],
                                    .twisty = open,
                                    .meta = meta };
            if (kit_row_begin(k, key, &header) & (KIT_ROW_TWISTY | KIT_ROW_CLICK)) {
                s->assets_collapsed ^= 1u << kind;
            }
            kit_row_end(k);
            continue;
        }
        const reference_asset *asset = reference_index_asset_at(index, rows[r].asset);
        int used = uses[rows[r].asset];
        snprintf(key, sizeof key, "a%d", rows[r].asset);
        /* Scenes are entry points: nothing references them, so usage says nothing. */
        bool counted = asset->kind != REFERENCE_ASSET_SCENE;
        if (!counted) {
            meta[0] = '\0';
        } else if (used) {
            snprintf(meta, sizeof meta, "%d %s", used, used == 1 ? "use" : "uses");
        } else {
            snprintf(meta, sizeof meta, "unused");
        }
        kit_row_desc row = {
            .label = asset->name,
            .icon = asset->unresolved ? KIT_ICON_WARNING
                  : asset->kind == REFERENCE_ASSET_PROTOTYPE
                      ? studio_prototype_icon(s, asset->name)
                      : KIND_ICONS[asset->kind],
            .depth = grouped,
            .twisty = -1,
            .meta = grouped ? meta : KIND_NAMES[asset->kind],
        };
        unsigned events = kit_row_begin(k, key, &row);
        kit_row_end(k);
        /* Overscan rows and rows scrolled under the header are clipped. */
        if (kit_item_hovered(k)) {
            s->assets_hover = rows[r].asset;
        }
        kit_flag(k, "data-unused", counted && !used);
        kit_flag(k, "data-unresolved", asset->unresolved);
        if (asset->kind == REFERENCE_ASSET_PROTOTYPE ||
            asset->kind == REFERENCE_ASSET_BEHAVIOR) {
            kit_drag_desc drag = { .type = KIND_NAMES[asset->kind],
                                   .payload = asset->name,
                                   .icon = row.icon };
            kit_drag_source(k, &drag);
        }
        if (events & KIT_ROW_DOUBLE_CLICK) {
            finish_transform_edit(scene, false);
            app_scene_authoring_open_asset(scene, rows[r].asset);
        }
        if (events & KIT_ROW_CONTEXT) {
            s->assets_menu = rows[r].asset;
            kit_popup_open_at(k, menu_key, s->in.mouse_x, s->in.mouse_y);
        }
    }
    kit_list_end(k);
    asset_menu(s, menu_key);
    if (!acount(rows)) {
        kit_label(k, "empty", "output-empty", count ? "No matches" : "No assets");
    }
    afree(rows);
    afree(matches);
    free(uses);
}

bool studio_assets_key(studio *s, SDL_Keycode key, SDL_Keymod mod)
{
    if (s->assets_hover < 0 || studio_primary(s, mod) ||
        (mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT))) {
        return false;
    }
    if (key == SDLK_F2) {
        studio_refactor_rename_asset(s, s->assets_hover);
        return true;
    }
    if (key == SDLK_X || key == SDLK_DELETE) {
        studio_refactor_delete_asset(s, s->assets_hover);
        return true;
    }
    return false;
}

/* ---------- New UI assets ---------- */

static const char *const NEW_LABELS[UI_NEW_KIND_COUNT] = { "UI Widget",
                                                           "UI Document",
                                                           "Stylesheet" };
static const uint32_t NEW_ICONS[UI_NEW_KIND_COUNT] = { KIT_ICON_LAYOUT,
                                                       KIT_ICON_LAYOUT,
                                                       KIT_ICON_FILE };
static const mode_t UI_FOLDER_MODE = 0777;
#define NAME_POPOVER "new-ui-name"

static bool enter_pressed(const studio *s)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if (s->keys[i].key == SDLK_RETURN || s->keys[i].key == SDLK_KP_ENTER) {
            return true;
        }
    }
    return false;
}

/* Writes the new asset in the project's ui folder and opens it; false when
   the name is invalid or taken. */
static bool create(studio *s)
{
    studio_new_ui *n = &s->new_ui;
    char *folder = content_path_join(s->scene->content_root, "ui");
    mkdir(folder, UI_FOLDER_MODE);
    char *path = ui_buffer_new_path(
        folder,
        (ui_new_kind)n->kind,
        n->name,
        n->error,
        sizeof n->error
    );
    free(folder);
    if (!path) {
        return false;
    }
    char *text = ui_buffer_template((ui_new_kind)n->kind);
    if (write_file_atomic(path, text) != 0) {
        studio_error(s, "Could not create", content_basename(path));
    } else {
        content_refresh(s->scene);
        studio_open_ui_source(s, path, 1);
    }
    free(text);
    free(path);
    return true;
}

static void kind_menu(studio *s)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, "new-ui", "menu")) {
        return;
    }
    kit_menu_group(k, "group", "New");
    for (int i = 0; i < UI_NEW_KIND_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof key, "k%d", i);
        if (menu_item(k, key, NEW_LABELS[i], NEW_ICONS[i], NULL)) {
            s->new_ui = (studio_new_ui){ .kind = i, .opening = true, .focus = true };
        }
    }
    kit_popup_end(k);
}

static void name_popover(studio *s)
{
    kit *k = s->kit;
    studio_new_ui *n = &s->new_ui;
    if (!kit_popup_begin(k, NAME_POPOVER, "menu new-ui")) {
        return;
    }
    char title[64];
    snprintf(title, sizeof title, "New %s", NEW_LABELS[n->kind]);
    kit_menu_group(k, "title", title);
    kit_push(k, "field", NULL, "new-ui-field");
    bool committed = kit_text_field(k, "name", n->name, sizeof n->name, "Name", 0);
    if (n->focus) {
        kit_focus(k);
        n->focus = false;
    }
    kit_pop(k);
    if (n->error[0]) {
        kit_push(k, "error", NULL, "new-ui-error");
        kit_icon(k, "icon", NULL, KIT_ICON_WARNING);
        kit_label(k, "label", "label", n->error);
        kit_pop(k);
    }
    kit_menu_separator(k, "sep");
    bool chosen = menu_item(k, "create", "Create", KIT_ICON_ADD, NULL);
    bool entered = committed && enter_pressed(s);
    kit_popup_end(k);
    if (!chosen && !entered) {
        return;
    }
    if (create(s)) {
        kit_popup_close(k, NAME_POPOVER);
        return;
    }
    /* A name that fails asks again, even after Create closed the popover. */
    n->opening = n->focus = true;
}

void studio_new_ui_button(studio *s)
{
    kit *k = s->kit;
    kit_button_desc add = { .icon = KIT_ICON_ADD, .tip = "New UI asset" };
    if (kit_button(k, "new", &add)) {
        kit_popup_open(k, "new-ui");
    }
    if (s->new_ui.opening) {
        kit_popup_open(k, NAME_POPOVER);
        s->new_ui.opening = false;
    }
    kind_menu(s);
    name_popover(s);
}
