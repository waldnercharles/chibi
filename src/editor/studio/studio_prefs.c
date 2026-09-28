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

static const char *const WORKSPACES[STUDIO_WORKSPACE_COUNT] = { "layout", "ui" };

/* Layout stacks Scene over Behavior; UI sets the Canvas (the Scene area on a
   ui document) beside the Text Editor, half and half. */
studio_layout studio_layout_default(studio_workspace workspace)
{
    assert(workspace >= 0 && workspace < STUDIO_WORKSPACE_COUNT);
    bool ui = workspace == STUDIO_WORKSPACE_UI;
    return (studio_layout){
        .side_open = true,
        .side_width = ui ? 320 : 372,
        .main_split = ui ? 0.5f : 0.62f,
        .side_split = 0.38f,
        .drawer_height = 240,
        .editors = { STUDIO_EDITOR_SCENE,
                     ui ? STUDIO_EDITOR_CODE : STUDIO_EDITOR_BEHAVIOR,
                     STUDIO_EDITOR_OUTLINER,
                     STUDIO_EDITOR_PROPERTIES,
                     STUDIO_EDITOR_OUTPUT },
    };
}

studio_layout *studio_active_layout(studio *s)
{
    return &s->prefs.layouts[s->prefs.workspace];
}

studio_prefs studio_prefs_default()
{
    studio_prefs p = { 0 };
    for (int w = 0; w < STUDIO_WORKSPACE_COUNT; w++) {
        p.layouts[w] = studio_layout_default(w);
    }
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
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        if (strcmp(a->shortcuts[c], b->shortcuts[c])) {
            return false;
        }
    }
    for (int w = 0; w < STUDIO_WORKSPACE_COUNT; w++) {
        const studio_layout *x = &a->layouts[w], *y = &b->layouts[w];
        for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
            if (x->editors[i] != y->editors[i] || strcmp(x->panels[i], y->panels[i])) {
                return false;
            }
        }
        if (x->side_open != y->side_open || x->side_width != y->side_width ||
            x->main_split != y->main_split || x->side_split != y->side_split ||
            x->drawer_open != y->drawer_open || x->drawer_height != y->drawer_height) {
            return false;
        }
    }
    return a->workspace == b->workspace;
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

static void read_layout(studio_layout *l, yyjson_val *layout)
{
    yyjson_val *side_open = yyjson_obj_get(layout, "sideOpen");
    if (yyjson_is_bool(side_open)) {
        l->side_open = yyjson_get_bool(side_open);
    }
    l->side_width = number(yyjson_obj_get(layout, "sideWidth"), l->side_width, 200, 1600);
    l->main_split = number(yyjson_obj_get(layout, "mainSplit"), l->main_split, 0.05f, 0.95f);
    l->side_split = number(yyjson_obj_get(layout, "sideSplit"), l->side_split, 0.05f, 0.95f);
    yyjson_val *drawer = yyjson_obj_get(layout, "drawer");
    yyjson_val *open = yyjson_obj_get(drawer, "open");
    if (yyjson_is_bool(open)) {
        l->drawer_open = yyjson_get_bool(open);
    }
    l->drawer_height = number(yyjson_obj_get(drawer, "height"), l->drawer_height, 80, 2000);
    yyjson_val *areas = yyjson_obj_get(layout, "areas");
    for (int i = 0; i < STUDIO_AREA_COUNT && i < (int)yyjson_arr_size(areas); i++) {
        yyjson_val *area = yyjson_arr_get(areas, (size_t)i);
        const char *panel = yyjson_get_str(yyjson_obj_get(area, "panel"));
        if (panel && panel[0] && strlen(panel) < sizeof l->panels[i]) {
            l->editors[i] = STUDIO_EDITOR_PANEL;
            snprintf(l->panels[i], sizeof l->panels[i], "%s", panel);
            continue;
        }
        int editor = name_index(EDITORS, STUDIO_EDITOR_COUNT, area, l->editors[i]);
        l->editors[i] = editor == STUDIO_EDITOR_PANEL ? l->editors[i]
                                                      : (studio_editor)editor;
    }
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
    yyjson_val *shortcuts = yyjson_obj_get(root, "shortcuts");
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        const char *chord = yyjson_get_str(
            yyjson_obj_get(shortcuts, studio_command_id(c))
        );
        if (studio_chord_valid(chord) &&
            strcmp(chord, studio_command_default_chord(c))) {
            snprintf(p.shortcuts[c], sizeof p.shortcuts[c], "%s", chord);
        }
    }
    yyjson_val *layout = yyjson_obj_get(root, "layout");
    p.workspace = name_index(
        WORKSPACES,
        STUDIO_WORKSPACE_COUNT,
        yyjson_obj_get(layout, "workspace"),
        STUDIO_WORKSPACE_LAYOUT
    );
    yyjson_val *presets = yyjson_obj_get(layout, "presets");
    for (int w = 0; w < STUDIO_WORKSPACE_COUNT; w++) {
        read_layout(&p.layouts[w], yyjson_obj_get(presets, WORKSPACES[w]));
    }
    /* A file from before presets holds one arrangement: Layout's. */
    if (!presets) {
        read_layout(&p.layouts[STUDIO_WORKSPACE_LAYOUT], layout);
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

/* Adds the members of `saved` this build does not know, in objects both
   hold too, as VS Code's settings.json keeps settings it does not know. In
   `shortcuts`, a command this build knows without an override has its
   default chord, so its old override goes. */
static void keep_unknown(yyjson_mut_doc *doc, yyjson_mut_val *into, yyjson_val *saved, bool shortcuts)
{
    size_t i, max;
    yyjson_val *key, *value;
    yyjson_obj_foreach(saved, i, max, key, value)
    {
        const char *name = yyjson_get_str(key);
        yyjson_mut_val *known = yyjson_mut_obj_get(into, name);
        if (!known && !(shortcuts && studio_command_named(name) >= 0)) {
            yyjson_mut_obj_add(into, yyjson_val_mut_copy(doc, key), yyjson_val_mut_copy(doc, value));
        } else if (yyjson_mut_is_obj(known) && yyjson_is_obj(value)) {
            keep_unknown(doc, known, value, !strcmp(name, "shortcuts"));
        }
    }
}

static void write_layout(const studio_layout *l, yyjson_mut_doc *doc, yyjson_mut_val *layout)
{
    yyjson_mut_obj_add_bool(doc, layout, "sideOpen", l->side_open);
    yyjson_mut_obj_add_real(doc, layout, "sideWidth", l->side_width);
    yyjson_mut_obj_add_real(doc, layout, "mainSplit", l->main_split);
    yyjson_mut_obj_add_real(doc, layout, "sideSplit", l->side_split);
    yyjson_mut_val *drawer = yyjson_mut_obj_add_obj(doc, layout, "drawer");
    yyjson_mut_obj_add_bool(doc, drawer, "open", l->drawer_open);
    yyjson_mut_obj_add_real(doc, drawer, "height", l->drawer_height);
    yyjson_mut_val *areas = yyjson_mut_obj_add_arr(doc, layout, "areas");
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (l->editors[i] == STUDIO_EDITOR_PANEL) {
            yyjson_mut_obj_add_str(doc, yyjson_mut_arr_add_obj(doc, areas), "panel", l->panels[i]);
            continue;
        }
        yyjson_mut_arr_add_str(doc, areas, EDITORS[l->editors[i]]);
    }
}

char *studio_prefs_write(const studio_prefs *p, const char *saved)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", PREFS_VERSION);
    for (int i = 0; i < studio_setting_count; i++) {
        write_setting(p, &studio_settings[i], doc, root);
    }
    yyjson_mut_val *shortcuts = yyjson_mut_obj_add_obj(doc, root, "shortcuts");
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        if (p->shortcuts[c][0]) {
            yyjson_mut_obj_add_str(doc, shortcuts, studio_command_id(c), p->shortcuts[c]);
        }
    }
    yyjson_mut_val *layout = yyjson_mut_obj_add_obj(doc, root, "layout");
    yyjson_mut_obj_add_str(doc, layout, "workspace", WORKSPACES[p->workspace]);
    yyjson_mut_val *presets = yyjson_mut_obj_add_obj(doc, layout, "presets");
    for (int w = 0; w < STUDIO_WORKSPACE_COUNT; w++) {
        write_layout(&p->layouts[w], doc, yyjson_mut_obj_add_obj(doc, presets, WORKSPACES[w]));
    }
    yyjson_doc *previous = saved ? yyjson_read(saved, strlen(saved), 0) : NULL;
    if (yyjson_is_obj(yyjson_doc_get_root(previous))) {
        keep_unknown(doc, root, yyjson_doc_get_root(previous), false);
    }
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_doc_free(previous);
    yyjson_mut_doc_free(doc);
    assert(json);
    return json;
}

bool studio_prefs_save(const studio_prefs *p)
{
    char *saved = read_json();
    char *json = studio_prefs_write(p, saved);
    free(saved);
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
