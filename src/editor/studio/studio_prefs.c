/* Studio preferences: one plain store persisted as a whole (zustand's persist
 * middleware). Reading keeps defaults for missing or invalid fields, so older
 * files migrate without a version switch. Desktop writes a JSON file in the
 * user directory; the browser uses localStorage. */
#include "editor/studio/studio_internal.h"
#include "platform/cute.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(char *, prefs_read_browser, (), {
    try {
        const value = localStorage.getItem("chibi.studio");
        return value ? stringToNewUTF8(value) : 0;
}
catch(_)
{
    return 0;
}
})
EM_JS(bool, prefs_write_browser, (const char *json), {
    try {
        localStorage.setItem("chibi.studio", UTF8ToString(json));
        return true;
}
catch(_)
{
    return false;
}
})
EM_JS(bool, prefs_browser_reduced_motion, (), {
    try {
        return matchMedia("(prefers-reduced-motion: reduce)").matches;
}
catch(_)
{
    return false;
}
})
#endif

#define PREFS_VERSION 1

static const char *const THEMES[STUDIO_THEME_COUNT] = { "system", "dark", "light" };
/* UI Scale's range, as Blender's Resolution Scale allows a useful subset of. */
static const float UI_SCALE_MIN = 0.5f, UI_SCALE_MAX = 2.0f;
static const char *const NODE_FLAGS[] = { "shortBindings", "titleFlow", "compact" };
static const char *const WIRES[KIT_WIRE_STYLE_COUNT] = { "curved",
                                                         "orthogonal",
                                                         "straight",
                                                         "subway" };
/* An area showing a panel saves as {"panel": "<name>"}. */
static const char *const EDITORS[STUDIO_EDITOR_COUNT] = {
    "scene",  "behavior",    "outliner", "properties", "output",
    "assets", "performance", "members",  "panel"
};

studio_prefs studio_prefs_default()
{
    studio_prefs p = {
        .theme = STUDIO_THEME_SYSTEM,
        .ui_scale = 1,
        .side_width = 372,
        .main_split = 0.62f,
        .side_split = 0.38f,
        .drawer_height = 240,
        .short_bindings = true,
        .title_flow = true,
        .compact_nodes = true,
        .editors = { STUDIO_EDITOR_SCENE,
                     STUDIO_EDITOR_BEHAVIOR,
                     STUDIO_EDITOR_OUTLINER,
                     STUDIO_EDITOR_PROPERTIES,
                     STUDIO_EDITOR_OUTPUT },
    };
#ifdef __EMSCRIPTEN__
    p.reduced_motion = prefs_browser_reduced_motion();
#endif
    return p;
}

bool studio_prefs_equal(const studio_prefs *a, const studio_prefs *b)
{
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (a->editors[i] != b->editors[i] || strcmp(a->panels[i], b->panels[i]) != 0) {
            return false;
        }
    }
    return a->theme == b->theme && a->comfortable == b->comfortable &&
           a->ui_scale == b->ui_scale && a->reduced_motion == b->reduced_motion &&
           a->side_width == b->side_width && a->main_split == b->main_split &&
           a->side_split == b->side_split && a->drawer_open == b->drawer_open &&
           a->drawer_height == b->drawer_height && a->wires == b->wires &&
           a->short_bindings == b->short_bindings &&
           a->title_flow == b->title_flow && a->compact_nodes == b->compact_nodes;
}

static int name_index(const char *const *names, int count, yyjson_val *value, int fallback)
{
    const char *name = yyjson_get_str(value);
    for (int i = 0; name && i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return fallback;
}

static float number(yyjson_val *value, float fallback, float minimum, float maximum)
{
    if (!yyjson_is_num(value)) {
        return fallback;
    }
    double v = yyjson_get_num(value);
    return v >= minimum && v <= maximum ? (float)v : fallback;
}

static char *read_json()
{
#ifdef __EMSCRIPTEN__
    return prefs_read_browser();
#else
    const char *directory = cf_fs_get_user_directory("chibi", "editor");
    char path[1024];
    if (!directory || snprintf(path, sizeof path, "%sstudio.json", directory) >=
                          (int)sizeof path) {
        return NULL;
    }
    return text_file_read(path);
#endif
}

studio_prefs studio_prefs_load()
{
    studio_prefs p = studio_prefs_default();
    char *json = read_json();
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    free(json);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return p;
    }
    p.theme = (studio_theme)
        name_index(THEMES, STUDIO_THEME_COUNT, yyjson_obj_get(root, "theme"), p.theme);
    yyjson_val *density = yyjson_obj_get(root, "density");
    if (yyjson_is_str(density)) {
        p.comfortable = strcmp(yyjson_get_str(density), "comfortable") == 0;
    }
    p.ui_scale = number(yyjson_obj_get(root, "uiScale"), p.ui_scale, UI_SCALE_MIN, UI_SCALE_MAX);
    yyjson_val *motion = yyjson_obj_get(root, "reduceMotion");
    if (yyjson_is_bool(motion)) {
        p.reduced_motion = yyjson_get_bool(motion);
    }
    p.wires = (kit_wire_style)
        name_index(WIRES, KIT_WIRE_STYLE_COUNT, yyjson_obj_get(root, "wires"), p.wires);
    yyjson_val *nodes = yyjson_obj_get(root, "nodes");
    bool *flags[] = { &p.short_bindings, &p.title_flow, &p.compact_nodes };
    for (int i = 0; i < 3; i++) {
        yyjson_val *flag = yyjson_obj_get(nodes, NODE_FLAGS[i]);
        if (yyjson_is_bool(flag)) {
            *flags[i] = yyjson_get_bool(flag);
        }
    }
    yyjson_val *layout = yyjson_obj_get(root, "layout");
    p.side_width = number(yyjson_obj_get(layout, "sideWidth"), p.side_width, 200, 1600);
    p.main_split = number(yyjson_obj_get(layout, "mainSplit"), p.main_split, 0.05f, 0.95f);
    p.side_split = number(yyjson_obj_get(layout, "sideSplit"), p.side_split, 0.05f, 0.95f);
    yyjson_val *drawer = yyjson_obj_get(layout, "drawer");
    yyjson_val *open = yyjson_obj_get(drawer, "open");
    if (yyjson_is_bool(open)) {
        p.drawer_open = yyjson_get_bool(open);
    }
    p.drawer_height = number(yyjson_obj_get(drawer, "height"), p.drawer_height, 80, 2000);
    yyjson_val *areas = yyjson_obj_get(layout, "areas");
    for (int i = 0; i < STUDIO_AREA_COUNT && i < (int)yyjson_arr_size(areas); i++) {
        yyjson_val *area = yyjson_arr_get(areas, (size_t)i);
        const char *panel = yyjson_get_str(yyjson_obj_get(area, "panel"));
        if (panel && panel[0] && strlen(panel) < sizeof p.panels[i]) {
            p.editors[i] = STUDIO_EDITOR_PANEL;
            snprintf(p.panels[i], sizeof p.panels[i], "%s", panel);
            continue;
        }
        int editor = name_index(EDITORS, STUDIO_EDITOR_COUNT, area, p.editors[i]);
        p.editors[i] = editor == STUDIO_EDITOR_PANEL ? p.editors[i]
                                                     : (studio_editor)editor;
    }
    yyjson_doc_free(doc);
    return p;
}

bool studio_prefs_save(const studio_prefs *p)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", PREFS_VERSION);
    yyjson_mut_obj_add_str(doc, root, "theme", THEMES[p->theme]);
    yyjson_mut_obj_add_str(doc, root, "density", p->comfortable ? "comfortable" : "compact");
    yyjson_mut_obj_add_real(doc, root, "uiScale", p->ui_scale);
    yyjson_mut_obj_add_bool(doc, root, "reduceMotion", p->reduced_motion);
    yyjson_mut_obj_add_str(doc, root, "wires", WIRES[p->wires]);
    yyjson_mut_val *nodes = yyjson_mut_obj_add_obj(doc, root, "nodes");
    bool flags[] = { p->short_bindings, p->title_flow, p->compact_nodes };
    for (int i = 0; i < 3; i++) {
        yyjson_mut_obj_add_bool(doc, nodes, NODE_FLAGS[i], flags[i]);
    }
    yyjson_mut_val *layout = yyjson_mut_obj_add_obj(doc, root, "layout");
    yyjson_mut_obj_add_real(doc, layout, "sideWidth", p->side_width);
    yyjson_mut_obj_add_real(doc, layout, "mainSplit", p->main_split);
    yyjson_mut_obj_add_real(doc, layout, "sideSplit", p->side_split);
    yyjson_mut_val *drawer = yyjson_mut_obj_add_obj(doc, layout, "drawer");
    yyjson_mut_obj_add_bool(doc, drawer, "open", p->drawer_open);
    yyjson_mut_obj_add_real(doc, drawer, "height", p->drawer_height);
    yyjson_mut_val *areas = yyjson_mut_obj_add_arr(doc, layout, "areas");
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (p->editors[i] == STUDIO_EDITOR_PANEL) {
            yyjson_mut_obj_add_str(doc, yyjson_mut_arr_add_obj(doc, areas), "panel", p->panels[i]);
            continue;
        }
        yyjson_mut_arr_add_str(doc, areas, EDITORS[p->editors[i]]);
    }
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(doc);
    if (!json) {
        return false;
    }
#ifdef __EMSCRIPTEN__
    bool ok = prefs_write_browser(json);
#else
    const char *directory = cf_fs_get_user_directory("chibi", "editor");
    char path[1024];
    FILE *file = directory && snprintf(path, sizeof path, "%sstudio.json", directory) <
                                  (int)sizeof path
                   ? fopen(path, "wb")
                   : NULL;
    bool ok = file && fputs(json, file) >= 0;
    if (file) {
        ok &= fclose(file) == 0;
    }
#endif
    free(json);
    return ok;
}

/* ---------- Shared with authored panels ---------- */

static const char *const DENSITIES[] = { "compact", "comfortable" };

static void share_name(game_save *save, const char *key, const char *const *names, int count, int *value)
{
    const char *text = game_save_get(save, key);
    for (int i = 0; text && i < count; i++) {
        if (!strcmp(text, names[i])) {
            *value = i;
        }
    }
}

static void share_scale(game_save *save, float *value)
{
    const char *text = game_save_get(save, "uiScale");
    char *end;
    float scale = text ? strtof(text, &end) : 0;
    if (text && end != text && !*end && scale >= UI_SCALE_MIN && scale <= UI_SCALE_MAX) {
        *value = scale;
    }
}

static void share_flag(game_save *save, const char *key, bool *value)
{
    const char *text = game_save_get(save, key);
    if (text) {
        *value = !strcmp(text, "true");
    }
}

void studio_prefs_share(studio_prefs *p, game_save *save, uint64_t *seen)
{
    if (game_save_revision(save) != *seen) {
        int theme = (int)p->theme, density = p->comfortable, wires = (int)p->wires;
        share_name(save, "theme", THEMES, STUDIO_THEME_COUNT, &theme);
        share_name(save, "density", DENSITIES, 2, &density);
        share_name(save, "wires", WIRES, KIT_WIRE_STYLE_COUNT, &wires);
        p->theme = (studio_theme)theme;
        p->comfortable = density == 1;
        p->wires = (kit_wire_style)wires;
        share_flag(save, "shortBindings", &p->short_bindings);
        share_flag(save, "titleFlow", &p->title_flow);
        share_flag(save, "compactNodes", &p->compact_nodes);
        share_flag(save, "reduceMotion", &p->reduced_motion);
        share_scale(save, &p->ui_scale);
    }
    game_save_set(save, "theme", THEMES[p->theme]);
    game_save_set(save, "density", DENSITIES[p->comfortable]);
    game_save_set(save, "wires", WIRES[p->wires]);
    game_save_set(save, "shortBindings", p->short_bindings ? "true" : "false");
    game_save_set(save, "titleFlow", p->title_flow ? "true" : "false");
    game_save_set(save, "compactNodes", p->compact_nodes ? "true" : "false");
    game_save_set(save, "reduceMotion", p->reduced_motion ? "true" : "false");
    char scale[16];
    snprintf(scale, sizeof scale, "%g", p->ui_scale);
    game_save_set(save, "uiScale", scale);
    *seen = game_save_revision(save);
}
