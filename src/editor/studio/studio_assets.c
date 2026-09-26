/* Assets: every indexed asset as a virtualized list, filtered by kind and name.
 * Prototypes drag into the Scene or the Outliner to place an instance; behaviors
 * drag onto an Outliner row to attach. Double-click opens the asset. */
#include "editor/studio/studio_internal.h"
#include "editor/workspace/asset_index.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const KIND_LABELS[ASSET_INDEX_KIND_COUNT] = {
    "Scenes", "Prototypes", "Behaviors", "Events",  "Materials",
    "Shapes", "Fields",     "Libraries", "Systems", "UI",
};
static const char *const KIND_NAMES[ASSET_INDEX_KIND_COUNT] = {
    "scene", "prototype", "behavior", "event",  "material",
    "shape", "field",     "library",  "system", "ui",
};
static const uint32_t KIND_ICONS[ASSET_INDEX_KIND_COUNT] = {
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
    const char *kinds[ASSET_INDEX_KIND_COUNT + 1] = { "All" };
    for (int i = 0; i < ASSET_INDEX_KIND_COUNT; i++) {
        kinds[i + 1] = KIND_LABELS[i];
    }
    int choice = s->assets_kind + 1;
    kit_push(k, "kind", NULL, "assets-kind");
    if (kit_choice(k, "kind", &choice, kinds, ASSET_INDEX_KIND_COUNT + 1, 0)) {
        s->assets_kind = choice - 1;
    }
    kit_pop(k);
    kit_push(k, "filter", NULL, "assets-filter");
    kit_search(k, "search", s->assets_filter, sizeof s->assets_filter, "Filter");
    kit_pop(k);
}

/* Rows of the list: a group header (asset < 0, kind set) or an asset. */
typedef struct
{
    int asset;
    int kind;
} asset_row;

static const asset_index *sort_index;

static int by_name(const void *a, const void *b)
{
    const asset_index_asset *x = asset_index_asset_at(sort_index, *(const int *)a);
    const asset_index_asset *y = asset_index_asset_at(sort_index, *(const int *)b);
    return x->kind != y->kind ? (int)x->kind - (int)y->kind
                              : strcasecmp(x->name, y->name);
}

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon)
{
    kit_menu_item_desc d = { .label = label, .icon = icon, .checked = -1 };
    return kit_menu_item(k, key, &d);
}

/* A row's menu opens the asset or lists what references it (Find References). */
static void asset_menu(studio *s, const char *key)
{
    kit *k = s->kit;
    const asset_index *index = app_scene_authoring_asset_index(s->scene);
    const asset_index_asset *asset = asset_index_asset_at(index, s->assets_menu);
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    if (asset) {
        kit_menu_group(k, "group", asset->name);
    }
    if (asset && menu_item(k, "open", "Open", KIT_ICON_EXTERNAL)) {
        finish_transform_edit(s->scene, false);
        app_scene_authoring_open_asset(s->scene, s->assets_menu);
    }
    if (asset && menu_item(k, "refs", "Find References", KIT_ICON_REFERENCES)) {
        studio_find_asset_references(s, s->assets_menu);
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
    const asset_index *index = app_scene_authoring_asset_index(scene);
    int count = index ? asset_index_asset_count(index) : 0;
    int *uses = xcalloc((size_t)(count ? count : 1), sizeof *uses);
    for (int r = 0; index && r < asset_index_reference_count(index); r++) {
        const asset_index_reference *ref = asset_index_reference_at(index, r);
        if (ref->target >= 0 && ref->target < count && ref->target != ref->source) {
            uses[ref->target]++;
        }
    }
    int *matches = NULL;
    for (int i = 0; i < count; i++) {
        const asset_index_asset *asset = asset_index_asset_at(index, i);
        if (asset->name && asset->kind >= 0 && asset->kind < ASSET_INDEX_KIND_COUNT &&
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
        int kind = (int)asset_index_asset_at(index, matches[m])->kind;
        if (grouped &&
            (m == 0 || (int)asset_index_asset_at(index, matches[m - 1])->kind != kind)) {
            apush(rows, ((asset_row){ -1, kind }));
        }
        if (!grouped || !(s->assets_collapsed & (1u << kind)) || s->assets_filter[0]) {
            apush(rows, ((asset_row){ matches[m], kind }));
        }
    }
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
                total += (int)asset_index_asset_at(index, matches[m])->kind == kind;
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
        const asset_index_asset *asset = asset_index_asset_at(index, rows[r].asset);
        int used = uses[rows[r].asset];
        snprintf(key, sizeof key, "a%d", rows[r].asset);
        /* Scenes are entry points: nothing references them, so usage says nothing. */
        bool counted = asset->kind != ASSET_INDEX_SCENE;
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
                  : asset->kind == ASSET_INDEX_PROTOTYPE
                      ? studio_prototype_icon(s, asset->name)
                      : KIND_ICONS[asset->kind],
            .depth = grouped,
            .twisty = -1,
            .meta = grouped ? meta : KIND_NAMES[asset->kind],
        };
        unsigned events = kit_row_begin(k, key, &row);
        kit_row_end(k);
        kit_flag(k, "data-unused", counted && !used);
        kit_flag(k, "data-unresolved", asset->unresolved);
        if (asset->kind == ASSET_INDEX_PROTOTYPE || asset->kind == ASSET_INDEX_BEHAVIOR) {
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
