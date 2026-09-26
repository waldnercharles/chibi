/* Studio preferences: one plain store persisted as a whole (zustand's persist
 * middleware). Reading keeps defaults for missing or invalid fields, so older
 * files migrate without a version switch. Desktop writes a JSON file in the
 * user directory; the browser uses localStorage. Settings go through the
 * settings table (studio_settings.c); the area layout is kept here. */
#include "editor/studio/studio_internal.h"
#include "platform/cute.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
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

/* An area showing a panel saves as {"panel": "<name>"}. */
static const char *const EDITORS[STUDIO_EDITOR_COUNT] = {
    "scene",  "behavior",    "outliner", "properties", "output",
    "assets", "performance", "members",  "code",       "panel"
};

studio_prefs studio_prefs_default()
{
    studio_prefs p = {
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
    for (int i = 0; i < studio_setting_count; i++) {
        studio_setting_set(&p, &studio_settings[i], studio_settings[i].fallback);
    }
#ifdef __EMSCRIPTEN__
    p.reduced_motion = prefs_browser_reduced_motion();
#endif
    return p;
}

bool studio_prefs_equal(const studio_prefs *a, const studio_prefs *b)
{
    for (int i = 0; i < studio_setting_count; i++) {
        const studio_setting *s = &studio_settings[i];
        if (studio_setting_get(a, s) != studio_setting_get(b, s)) {
            return false;
        }
    }
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (a->editors[i] != b->editors[i] || strcmp(a->panels[i], b->panels[i]) != 0) {
            return false;
        }
    }
    return a->side_width == b->side_width && a->main_split == b->main_split &&
           a->side_split == b->side_split && a->drawer_open == b->drawer_open &&
           a->drawer_height == b->drawer_height;
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

/* The value at a dotted path ("code.vim") under `root`, or NULL. */
static yyjson_val *value_at(yyjson_val *root, const char *path)
{
    for (const char *at = path;; at++) {
        size_t length = strcspn(at, ".");
        root = yyjson_obj_getn(root, at, length);
        at += length;
        if (!*at) {
            return root;
        }
    }
}

/* Keeps the default for a missing value, or one of the wrong type or range. */
static void read_setting(studio_prefs *p, const studio_setting *s, yyjson_val *root)
{
    yyjson_val *value = value_at(root, s->key);
    if (s->type == STUDIO_SETTING_NUMBER && yyjson_is_num(value)) {
        char text[32];
        snprintf(text, sizeof text, "%.9g", yyjson_get_num(value));
        studio_setting_parse(p, s, text);
        return;
    }
    if (s->type == STUDIO_SETTING_BOOL && yyjson_is_bool(value)) {
        studio_setting_set(p, s, yyjson_get_bool(value));
        return;
    }
    if (s->type == STUDIO_SETTING_CHOICE && yyjson_is_str(value)) {
        studio_setting_parse(p, s, yyjson_get_str(value));
    }
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

studio_prefs studio_prefs_read(const char *json)
{
    studio_prefs p = studio_prefs_default();
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return p;
    }
    for (int i = 0; i < studio_setting_count; i++) {
        read_setting(&p, &studio_settings[i], root);
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

studio_prefs studio_prefs_load()
{
    char *json = read_json();
    studio_prefs p = studio_prefs_read(json);
    free(json);
    return p;
}

/* The object holding the member at a dotted path ("code.vim"), made as
 * needed; *name becomes the member's name. */
static yyjson_mut_val *object_for(yyjson_mut_doc *doc, yyjson_mut_val *root, const char **name)
{
    for (const char *dot; (dot = strchr(*name, '.')); *name = dot + 1) {
        size_t length = (size_t)(dot - *name);
        yyjson_mut_val *child = yyjson_mut_obj_getn(root, *name, length);
        if (!child) {
            child = yyjson_mut_obj(doc);
            yyjson_mut_obj_add(root, yyjson_mut_strncpy(doc, *name, length), child);
        }
        root = child;
    }
    return root;
}

static void write_setting(
    const studio_prefs *p,
    const studio_setting *s,
    yyjson_mut_doc *doc,
    yyjson_mut_val *root
)
{
    const char *name = s->key;
    yyjson_mut_val *object = object_for(doc, root, &name);
    float value = studio_setting_get(p, s);
    switch (s->type) {
        case STUDIO_SETTING_BOOL:
            yyjson_mut_obj_add_bool(doc, object, name, value != 0);
            return;
        case STUDIO_SETTING_CHOICE:
            yyjson_mut_obj_add_str(doc, object, name, s->choices[(int)value].value);
            return;
        case STUDIO_SETTING_NUMBER:
            yyjson_mut_obj_add_real(doc, object, name, value);
            return;
    }
}

char *studio_prefs_write(const studio_prefs *p)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", PREFS_VERSION);
    for (int i = 0; i < studio_setting_count; i++) {
        write_setting(p, &studio_settings[i], doc, root);
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
    assert(json);
    return json;
}

bool studio_prefs_save(const studio_prefs *p)
{
    char *json = studio_prefs_write(p);
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

void studio_prefs_share(studio_prefs *p, game_save *save, uint64_t *seen)
{
    if (game_save_revision(save) != *seen) {
        for (int i = 0; i < studio_setting_count; i++) {
            const char *text = game_save_get(save, studio_settings[i].save);
            if (text) {
                studio_setting_parse(p, &studio_settings[i], text);
            }
        }
    }
    for (int i = 0; i < studio_setting_count; i++) {
        char text[32];
        studio_setting_text(p, &studio_settings[i], text, sizeof text);
        game_save_set(save, studio_settings[i].save, text);
    }
    *seen = game_save_revision(save);
}
