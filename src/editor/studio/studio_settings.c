/* The editor's preferences as one table, as VS Code's settings registry: each
 * row says where a studio_prefs field lives in the preferences file and in
 * the saved keys authored panels bind, and how the Settings page shows it.
 * The area layout is not a setting; studio_prefs.c keeps it by hand. */
#include "editor/studio/studio_internal.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const studio_settings_category_info studio_settings_categories[STUDIO_SETTINGS_CATEGORY_COUNT] = {
    [STUDIO_SETTINGS_APPEARANCE] = { "appearance", "Appearance", KIT_ICON_MATERIAL, false },
    [STUDIO_SETTINGS_GRAPH] = { "graph", "Graph", KIT_ICON_GRAPH, false },
    [STUDIO_SETTINGS_TEXT] = { "text", "Text Editor", KIT_ICON_EXPRESSION, false },
    [STUDIO_SETTINGS_KEYBOARD] = { "keyboard", "Keyboard", KIT_ICON_KEYBOARD, false },
    [STUDIO_SETTINGS_PROJECT] = { "project", "General", KIT_ICON_SETTINGS, true },
};

_Static_assert(sizeof(studio_theme) == sizeof(int), "choices are int fields");
_Static_assert(sizeof(kit_wire_style) == sizeof(int), "choices are int fields");

static const studio_setting_choice THEMES[STUDIO_THEME_COUNT] = {
    [STUDIO_THEME_SYSTEM] = { "system", "System" },
    [STUDIO_THEME_DARK] = { "dark", "Dark" },
    [STUDIO_THEME_LIGHT] = { "light", "Light" },
};
static const studio_setting_choice DENSITIES[] = { { "compact", "Compact" },
                                                   { "comfortable", "Comfortable" } };
static const studio_setting_choice WIRES[KIT_WIRE_STYLE_COUNT] = {
    { "curved", "Curved" },
    { "orthogonal", "Orthogonal" },
    { "straight", "Straight" },
    { "subway", "Subway" },
};
/* View › UI Scale's steps (studio_zoom), as browsers step a page's zoom. */
static const studio_setting_choice UI_SCALES[] = {
    { "0.5", "50%" },   { "0.67", "67%" },  { "0.75", "75%" },
    { "0.8", "80%" },   { "0.9", "90%" },   { "1", "100%" },
    { "1.1", "110%" },  { "1.25", "125%" }, { "1.5", "150%" },
    { "1.75", "175%" }, { "2", "200%" },
};

#define FIELD(name)                                                            \
    offsetof(studio_prefs, name), sizeof(((studio_prefs *)0)->name)
#define CHOICES(list) list, (int)(sizeof list / sizeof *list)

const studio_setting studio_settings[] = {
    { .key = "theme",
      .save = "theme",
      .category = STUDIO_SETTINGS_APPEARANCE,
      .label = "Theme",
      .description = "System follows the operating system's light or dark "
                     "appearance.",
      .keywords = "color colour mode dark light appearance palette",
      .type = STUDIO_SETTING_CHOICE,
      FIELD(theme),
      CHOICES(THEMES),
      .fallback = STUDIO_THEME_SYSTEM },
    { .key = "density",
      .save = "density",
      .category = STUDIO_SETTINGS_APPEARANCE,
      .label = "Density",
      .description = "Comfortable makes rows and controls taller; text keeps "
                     "its size.",
      .keywords = "spacing size compact comfortable row height padding",
      .type = STUDIO_SETTING_CHOICE,
      FIELD(comfortable),
      CHOICES(DENSITIES) },
    { .key = "uiScale",
      .save = "uiScale",
      .category = STUDIO_SETTINGS_APPEARANCE,
      .label = "UI Scale",
      .description = "The size of the whole editor. Games keep their own UI "
                     "scale.",
      .keywords = "zoom size scale resolution larger smaller bigger",
      .type = STUDIO_SETTING_NUMBER,
      FIELD(ui_scale),
      CHOICES(UI_SCALES),
      .min = STUDIO_UI_SCALE_MIN,
      .max = STUDIO_UI_SCALE_MAX,
      .fallback = 1 },
    { .key = "reduceMotion",
      .save = "reduceMotion",
      .category = STUDIO_SETTINGS_APPEARANCE,
      .label = "Reduce Motion",
      .description = "Turns off transitions and animations.",
      .keywords = "animation transition motion accessibility",
      .type = STUDIO_SETTING_BOOL,
      FIELD(reduced_motion) },
    { .key = "wires",
      .save = "wires",
      .category = STUDIO_SETTINGS_GRAPH,
      .label = "Wire Style",
      .description = "How wires route between nodes.",
      .keywords = "edges links connections routing curved orthogonal straight "
                  "subway",
      .type = STUDIO_SETTING_CHOICE,
      FIELD(wires),
      CHOICES(WIRES) },
    { .key = "nodes.shortBindings",
      .save = "shortBindings",
      .category = STUDIO_SETTINGS_GRAPH,
      .label = "Short Bindings",
      .description = "Binding chips name the component and field, as "
                     "\xe2\x80\x9c"
                     "Aim \xe2\x80\xba Manual\xe2\x80\x9d, not the full path.",
      .keywords = "chips component field path names",
      .type = STUDIO_SETTING_BOOL,
      FIELD(short_bindings),
      .fallback = 1 },
    { .key = "nodes.titleFlow",
      .save = "titleFlow",
      .category = STUDIO_SETTINGS_GRAPH,
      .label = "Flow in Titles",
      .description = "Unlabeled execution pins sit in node titles.",
      .keywords = "exec execution pins header title",
      .type = STUDIO_SETTING_BOOL,
      FIELD(title_flow),
      .fallback = 1 },
    { .key = "nodes.compact",
      .save = "compactNodes",
      .category = STUDIO_SETTINGS_GRAPH,
      .label = "Compact Nodes",
      .description = "Operators show as symbols, and reads with one output as "
                     "pills.",
      .keywords = "operators symbols pills small dense",
      .type = STUDIO_SETTING_BOOL,
      FIELD(compact_nodes),
      .fallback = 1 },
    { .key = "code.vim",
      .save = "codeVim",
      .category = STUDIO_SETTINGS_TEXT,
      .label = "Vim Keys",
      .description = "Edit with Vim's modes and keys.",
      .keywords = "vi modal keybindings normal insert",
      .type = STUDIO_SETTING_BOOL,
      FIELD(code_vim) },
    { .key = "code.fontSize",
      .save = "codeFontSize",
      .category = STUDIO_SETTINGS_TEXT,
      .label = "Font Size",
      .description = "Text size in the Text Editor. UI Scale enlarges it too.",
      .keywords = "text code size zoom points",
      .type = STUDIO_SETTING_NUMBER,
      FIELD(code_font_size),
      .min = 8,
      .max = 32,
      .step = 1,
      .fallback = 13 },
    { .key = "code.applyLive",
      .save = "codeApplyLive",
      .category = STUDIO_SETTINGS_TEXT,
      .label = "Apply Live",
      .description = "UI edits apply as you type. Off, they apply when you "
                     "save.",
      .keywords = "live preview hot reload save apply mode",
      .type = STUDIO_SETTING_BOOL,
      FIELD(code_apply_live),
      .fallback = 1 },
    /* Workstream I */
    { .key = "code.wrap",
      .save = "codeWrap",
      .category = STUDIO_SETTINGS_TEXT,
      .label = "Word Wrap",
      .description = "Long lines wrap at the Text Editor's edge. Off, they "
                     "run on and scroll sideways.",
      .keywords = "wrap lines horizontal scroll long",
      .type = STUDIO_SETTING_BOOL,
      FIELD(code_wrap),
      .fallback = 1 },
};
const int studio_setting_count = sizeof studio_settings / sizeof *studio_settings;

static const void *field_at(const studio_prefs *prefs, const studio_setting *s)
{
    return (const char *)prefs + s->offset;
}

float studio_setting_get(const studio_prefs *prefs, const studio_setting *s)
{
    const void *field = field_at(prefs, s);
    switch (s->type) {
        case STUDIO_SETTING_BOOL:
            assert(s->size == sizeof(bool));
            return *(const bool *)field;
        case STUDIO_SETTING_CHOICE:
            if (s->size == sizeof(bool)) {
                return *(const bool *)field;
            }
            assert(s->size == sizeof(int));
            return (float)*(const int *)field;
        case STUDIO_SETTING_NUMBER:
            assert(s->size == sizeof(float));
            return *(const float *)field;
    }
    assert(!"a setting type");
    return 0;
}

void studio_setting_set(studio_prefs *prefs, const studio_setting *s, float value)
{
    void *field = (char *)prefs + s->offset;
    if (s->type == STUDIO_SETTING_NUMBER) {
        assert(s->size == sizeof(float) && value >= s->min && value <= s->max);
        *(float *)field = value;
        return;
    }
    assert(
        s->type == STUDIO_SETTING_BOOL ? value == 0 || value == 1
                                       : value >= 0 && value < s->choice_count
    );
    if (s->size == sizeof(bool)) {
        *(bool *)field = value != 0;
        return;
    }
    assert(s->type == STUDIO_SETTING_CHOICE && s->size == sizeof(int));
    *(int *)field = (int)value;
}

void studio_setting_text(const studio_prefs *prefs, const studio_setting *s, char *out, size_t size)
{
    float value = studio_setting_get(prefs, s);
    switch (s->type) {
        case STUDIO_SETTING_BOOL:
            snprintf(out, size, "%s", value ? "true" : "false");
            return;
        case STUDIO_SETTING_CHOICE:
            snprintf(out, size, "%s", s->choices[(int)value].value);
            return;
        case STUDIO_SETTING_NUMBER: snprintf(out, size, "%g", value); return;
    }
}

/* A number with presets takes one of them, so a select of them always shows
 * its value, as UI Scale's steps do. */
static bool preset(const studio_setting *s, float value)
{
    for (int i = 0; i < s->choice_count; i++) {
        if (strtof(s->choices[i].value, NULL) == value) {
            return true;
        }
    }
    return s->choice_count == 0;
}

bool studio_setting_parse(studio_prefs *prefs, const studio_setting *s, const char *text)
{
    if (s->type == STUDIO_SETTING_BOOL) {
        bool on = !strcmp(text, "true");
        if (!on && strcmp(text, "false") != 0) {
            return false;
        }
        studio_setting_set(prefs, s, on);
        return true;
    }
    if (s->type == STUDIO_SETTING_CHOICE) {
        for (int i = 0; i < s->choice_count; i++) {
            if (!strcmp(s->choices[i].value, text)) {
                studio_setting_set(prefs, s, (float)i);
                return true;
            }
        }
        return false;
    }
    char *end;
    float value = strtof(text, &end);
    if (end == text || *end || !isfinite(value) || value < s->min ||
        value > s->max || !preset(s, value)) {
        return false;
    }
    studio_setting_set(prefs, s, value);
    return true;
}

/* Whether `word`, `length` bytes, is in `text`, ignoring case. */
static bool contains_word(const char *text, const char *word, size_t length)
{
    for (const char *at = text; *at; at++) {
        size_t i = 0;
        while (i < length && at[i] &&
               tolower((unsigned char)at[i]) == tolower((unsigned char)word[i])) {
            i++;
        }
        if (i == length) {
            return true;
        }
    }
    return false;
}

bool studio_settings_match(const char *query, const char *text)
{
    for (const char *at = query; *at;) {
        at += strspn(at, " ");
        size_t length = strcspn(at, " ");
        if (length && !contains_word(text, at, length)) {
            return false;
        }
        at += length;
    }
    return true;
}

bool studio_setting_matches(const studio_setting *s, const char *query)
{
    char text[1024];
    snprintf(
        text,
        sizeof text,
        "%s %s %s %s",
        s->label,
        s->description,
        s->keywords,
        studio_settings_categories[s->category].label
    );
    return studio_settings_match(query, text);
}
