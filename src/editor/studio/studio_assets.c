/* Assets: every indexed asset as a virtualized list, filtered by kind and name.
 * Prototypes drag into the Scene or the Outliner to place an instance;
 * behaviors drag onto an Outliner row to attach. Double-click opens the asset.
 * New makes an asset of any kind Studio indexes and opens it. */
#include "editor/studio/studio_internal.h"
#include "editor/workspace/asset_index.h"
#include "support/array.h"
#include "support/asset_text.h"
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
    /* Visual designer (workstream H): a UI document's Library. */
    if (studio_designer_active(s)) {
        studio_designer_library_header(s, area);
        return;
    }
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
    studio_new_asset_button(s);
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
    /* Visual designer (workstream H): a UI document's Library. */
    if (studio_designer_active(s)) {
        studio_designer_library(s, area);
        return;
    }
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

/* ---------- New assets ---------- */

/* Each kind New makes, in the order of new_asset_kind and then ui_new_kind,
   grouped as Blender's Add and Unity's Create menus group theirs. Its New …
   command gives its label and icon. */
static const struct
{
    const char *group;
    const char *name;          /* its default name, numbered when taken */
    reference_asset_kind kind; /* the Assets filter that lists it first */
} NEW_KINDS[STUDIO_NEW_KIND_COUNT] = {
    { "Scene", "NewScene", REFERENCE_ASSET_SCENE },
    { "Scene", "NewPrototype", REFERENCE_ASSET_PROTOTYPE },
    { "Logic", "NewBehavior", REFERENCE_ASSET_BEHAVIOR },
    { "Logic", "NewEvent", REFERENCE_ASSET_EVENT },
    { "Logic", "NewSystem", REFERENCE_ASSET_SYSTEM },
    { "Logic", "NewLibrary", REFERENCE_ASSET_LIBRARY },
    { "Rendering", "NewMaterial", REFERENCE_ASSET_MATERIAL },
    { "Rendering", "NewShape", REFERENCE_ASSET_SHAPE },
    { "Rendering", "NewField", REFERENCE_ASSET_FIELD },
    { "Rendering", "NewShaderLibrary", REFERENCE_ASSET_LIBRARY },
    { "UI", "NewWidget", REFERENCE_ASSET_UI },
    { "UI", "NewDocument", REFERENCE_ASSET_UI },
    { "UI", "NewStylesheet", REFERENCE_ASSET_UI },
};
static const mode_t FOLDER_MODE = 0777;
#define KIND_MENU "new-asset"
#define NAME_POPOVER "new-asset-name"

static bool is_ui(int kind)
{
    return kind >= NEW_ASSET_KIND_COUNT;
}

static studio_command new_command(int kind)
{
    return (studio_command)(STUDIO_COMMAND_NEW_SCENE + kind);
}

/* Whether an asset of the project has the name; a name only referenced is
   free, so making the asset resolves its references. */
static bool named(studio *s, const char *name)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    for (int i = 0; i < reference_index_asset_count(index); i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        if (!asset->unresolved && asset->name && !strcmp(asset->name, name)) {
            return true;
        }
    }
    return false;
}

static char *yaml_path(const char *folder, const char *name, char *error, size_t size)
{
    size_t length = strlen(folder) + strlen(name) + sizeof "/" ASSET_EXTENSION;
    char *path = xmalloc(length);
    snprintf(path, length, "%s/%s" ASSET_EXTENSION, folder, name);
    struct stat info;
    if (stat(path, &info) != 0) {
        return path;
    }
    snprintf(error, size, "%s already exists", content_basename(path));
    free(path);
    return NULL;
}

/* The new asset's file: a .yaml in its kind's folder, a ui asset's .rml or a
   stylesheet's .rcss; NULL with why in `error` when the name is invalid or
   taken. */
static char *new_path(studio *s, int kind, const char *name, char *error, size_t size)
{
    if (!new_asset_name_valid(name, error, size)) {
        return NULL;
    }
    if (named(s, name)) {
        snprintf(error, size, "%s already exists", name);
        return NULL;
    }
    const char *folder_name = is_ui(kind) ? "ui"
                                          : new_asset_folder((new_asset_kind)kind);
    char *folder = content_path_join(s->scene->content_root, folder_name);
    char *path = is_ui(kind)
                   ? ui_buffer_new_path(folder, (ui_new_kind)(kind - NEW_ASSET_KIND_COUNT), name, error, size)
                   : yaml_path(folder, name, error, size);
    free(folder);
    return path;
}

/* The kind's name, numbered from 2 until no asset or file has it. */
static void default_name(studio *s, int kind, char *out, size_t size)
{
    const char *base = NEW_KINDS[kind].name;
    snprintf(out, size, "%s", base);
    for (int i = 2;; i++) {
        char error[160];
        char *path = new_path(s, kind, out, error, sizeof error);
        bool free_name = path != NULL;
        free(path);
        if (free_name) {
            return;
        }
        snprintf(out, size, "%s%d", base, i);
    }
}

void studio_new_asset_start(studio *s, int kind)
{
    studio_new_asset *n = &s->new_asset;
    *n = (studio_new_asset){ .kind = kind, .opening = true, .focus = true };
    app_scene_authoring_refresh_asset_index(s->scene);
    default_name(s, kind, n->name, sizeof n->name);
}

static bool write_ui(ui_new_kind kind, const char *path)
{
    int result = 0;
    for (int p = 0; p < UI_PART_COUNT && !result; p++) {
        const char *text = ui_buffer_template(kind, (ui_part)p);
        char *file = text ? ui_buffer_part_file(path, (ui_part)p) : NULL;
        result = file ? write_file_atomic(file, text) : 0;
        free(file);
    }
    return result == 0;
}

/* The asset as its model writes it (assets/new_asset.h), which loads back
   unchanged and is what --format writes. */
static bool write_yaml(new_asset_kind kind, const char *name, const char *path)
{
    char *json = new_asset_json(kind, name);
    bool written = write_text_file_atomic(path, json) == 0;
    free(json);
    return written;
}

/* Studio takes the new file up, as it takes up any file that appears, and
   opens it as Assets does: a UI document in the Text Editor. */
static void open_new(studio *s, int kind, const char *path)
{
    if (is_ui(kind)) {
        content_refresh(s->scene);
        app_scene_authoring_refresh_asset_index(s->scene);
        studio_open_ui_source(s, path, 1);
        return;
    }
    scene_disk_follow(s->scene, &(scene_move){ .to = path }, 1);
    app_scene_authoring_refresh_asset_index(s->scene);
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    finish_transform_edit(s->scene, false);
    int asset = reference_index_find_asset(index, NEW_KINDS[kind].kind, path, NULL);
    if (app_scene_authoring_open_asset(s->scene, asset) != 0) {
        studio_error(s, "Could not open", app_scene_authoring_message(s->scene));
    }
}

/* Writes the new asset in its kind's folder and opens it; false when the
   name is invalid or taken. */
static bool create(studio *s)
{
    studio_new_asset *n = &s->new_asset;
    app_scene_authoring_refresh_asset_index(s->scene);
    char *path = new_path(s, n->kind, n->name, n->error, sizeof n->error);
    if (!path) {
        return false;
    }
    char *folder = xstrdup(path);
    *strrchr(folder, '/') = '\0';
    mkdir(folder, FOLDER_MODE);
    free(folder);
    bool written = is_ui(n->kind)
                     ? write_ui((ui_new_kind)(n->kind - NEW_ASSET_KIND_COUNT), path)
                     : write_yaml((new_asset_kind)n->kind, n->name, path);
    if (written) {
        open_new(s, n->kind, path);
    } else {
        studio_error(s, "Could not create", content_basename(path));
    }
    free(path);
    return true;
}

/* Every kind in its group; with the Assets filter on a kind, that kind's
   first. */
static void kind_menu(studio *s)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, KIND_MENU, "menu")) {
        return;
    }
    bool primary = false;
    for (int i = 0; i < STUDIO_NEW_KIND_COUNT; i++) {
        if ((int)NEW_KINDS[i].kind == s->assets_kind) {
            studio_command_item(s, new_command(i));
            primary = true;
        }
    }
    if (primary) {
        kit_menu_separator(k, "primary");
    }
    const char *group = NULL;
    for (int i = 0; i < STUDIO_NEW_KIND_COUNT; i++) {
        if ((int)NEW_KINDS[i].kind == s->assets_kind) {
            continue;
        }
        if (!group || strcmp(group, NEW_KINDS[i].group)) {
            group = NEW_KINDS[i].group;
            kit_menu_group(k, group, group);
        }
        studio_command_item(s, new_command(i));
    }
    kit_popup_end(k);
}

static bool enter_pressed(const studio *s)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if ((s->keys[i].key == SDLK_RETURN || s->keys[i].key == SDLK_KP_ENTER) &&
            !s->keys[i].repeat) {
            return true;
        }
    }
    return false;
}

static void name_popover(studio *s)
{
    kit *k = s->kit;
    studio_new_asset *n = &s->new_asset;
    if (!kit_popup_begin(k, NAME_POPOVER, "menu new-asset")) {
        n->shown = false;
        return;
    }
    char title[64];
    snprintf(
        title,
        sizeof title,
        "New %s",
        studio_command_describe(s, new_command(n->kind)).menu
    );
    kit_menu_group(k, "title", title);
    kit_push(k, "field", NULL, "new-asset-field");
    kit_text_field(k, "name", n->name, sizeof n->name, "Name", 0);
    if (n->focus) {
        kit_focus(k);
        n->focus = false;
    }
    kit_pop(k);
    if (n->error[0]) {
        kit_push(k, "error", NULL, "new-asset-error");
        kit_icon(k, "icon", NULL, KIT_ICON_WARNING);
        kit_label(k, "label", "label", n->error);
        kit_pop(k);
    }
    kit_menu_separator(k, "sep");
    bool chosen = menu_item(k, "create", "Create", KIT_ICON_ADD, NULL);
    /* Enter makes the asset, with the name as typed or as offered; the
       Enter that ran a New command opened the popover and is not for it. */
    bool entered = n->shown && enter_pressed(s);
    kit_popup_end(k);
    n->shown = true;
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

void studio_new_asset_button(studio *s)
{
    kit *k = s->kit;
    kit_button_desc add = { .icon = KIT_ICON_ADD, .tip = "New Asset" };
    if (kit_button(k, "new", &add)) {
        kit_popup_open(k, KIND_MENU);
    }
    /* A kind chosen from the menu asks for its name under the button. */
    if (s->new_asset.opening) {
        kit_popup_open(k, NAME_POPOVER);
        s->new_asset.opening = false;
    }
}

void studio_new_asset_frame(studio *s)
{
    /* A New … command asks at the pointer when no Assets header shows. */
    if (s->new_asset.opening) {
        kit_popup_open_at(s->kit, NAME_POPOVER, s->in.mouse_x, s->in.mouse_y);
        s->new_asset.opening = false;
    }
    kind_menu(s);
    name_popover(s);
}
