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
static const char *const EDITORS[STUDIO_EDITOR_COUNT] = {
    "scene",  "behavior", "outliner",   "properties",
    "output", "assets",   "performance"
};

studio_prefs studio_prefs_default()
{
    studio_prefs p = {
        .theme = STUDIO_THEME_SYSTEM,
        .side_width = 372,
        .main_split = 0.62f,
        .side_split = 0.38f,
        .drawer_height = 240,
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
        if (a->editors[i] != b->editors[i]) {
            return false;
        }
    }
    return a->theme == b->theme && a->comfortable == b->comfortable &&
           a->reduced_motion == b->reduced_motion &&
           a->side_width == b->side_width && a->main_split == b->main_split &&
           a->side_split == b->side_split && a->drawer_open == b->drawer_open &&
           a->drawer_height == b->drawer_height && a->wires == b->wires;
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
    yyjson_val *motion = yyjson_obj_get(root, "reduceMotion");
    if (yyjson_is_bool(motion)) {
        p.reduced_motion = yyjson_get_bool(motion);
    }
    static const char *const WIRES[] = { "curved", "straight", "angular" };
    p.wires = (kit_wire_style)name_index(WIRES, 3, yyjson_obj_get(root, "wires"), p.wires);
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
        p.editors[i] = (studio_editor)name_index(
            EDITORS,
            STUDIO_EDITOR_COUNT,
            yyjson_arr_get(areas, (size_t)i),
            p.editors[i]
        );
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
    yyjson_mut_obj_add_bool(doc, root, "reduceMotion", p->reduced_motion);
    static const char *const WIRES[] = { "curved", "straight", "angular" };
    yyjson_mut_obj_add_str(doc, root, "wires", WIRES[p->wires]);
    yyjson_mut_val *layout = yyjson_mut_obj_add_obj(doc, root, "layout");
    yyjson_mut_obj_add_real(doc, layout, "sideWidth", p->side_width);
    yyjson_mut_obj_add_real(doc, layout, "mainSplit", p->main_split);
    yyjson_mut_obj_add_real(doc, layout, "sideSplit", p->side_split);
    yyjson_mut_val *drawer = yyjson_mut_obj_add_obj(doc, layout, "drawer");
    yyjson_mut_obj_add_bool(doc, drawer, "open", p->drawer_open);
    yyjson_mut_obj_add_real(doc, drawer, "height", p->drawer_height);
    yyjson_mut_val *areas = yyjson_mut_obj_add_arr(doc, layout, "areas");
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
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
