/* The Library (workstream H): while a UI document is active, the Assets
 * area lists what can go into it, as Unity UI Builder's Library and UMG's
 * Palette do: basic elements, then the project's ui assets as widgets
 * (<use ui>). Drag an item onto the Hierarchy or the Canvas, or click it to
 * add it to the selection. The Add menus list the same items. */
#include "editor/studio/studio_designer_internal.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *label;
    uint32_t icon;
    const char *markup;
} library_element;

static const library_element ELEMENTS[] = {
    { "Container", KIT_ICON_ELEMENT, "<div/>" },
    { "Text", KIT_ICON_TEXT, "<span>Text</span>" },
    { "Button", KIT_ICON_BUTTON, "<button>Button</button>" },
    { "Text Field", KIT_ICON_INPUT, "<input type=\"text\"/>" },
    { "Checkbox", KIT_ICON_CHECKBOX, "<input type=\"checkbox\"/>" },
    { "Slider", KIT_ICON_SLIDER, "<input type=\"range\"/>" },
    { "Image", KIT_ICON_IMAGE, "<img src=\"\"/>" },
    { "Progress", KIT_ICON_PROGRESS, "<progress value=\"0.5\"/>" },
    /* Native elements for authored UI (workstream N; ui/motion.h). */
    { "Number Flow", KIT_ICON_NUMBER, "<number-flow value=\"0\"/>" },
    { "Morph Text", KIT_ICON_TEXT, "<morph-text>Text</morph-text>" },
};

enum
{
    ELEMENT_COUNT = sizeof ELEMENTS / sizeof *ELEMENTS,
};

/* The project's ui assets but the document itself, which cannot use
 * itself. The caller afrees the array; the names belong to the index. */
static const char **widget_names(studio *s, const studio_designer *d)
{
    const char **names = studio_asset_names(app_scene_authoring_asset_index(s->scene), REFERENCE_ASSET_UI);
    int kept = 0;
    for (int i = 0; i < acount(names); i++) {
        if (strcmp(names[i], d->asset)) {
            names[kept++] = names[i];
        }
    }
    asetlen(names, kept);
    return names;
}

static void widget_markup(const char *name, char *markup, size_t size)
{
    snprintf(markup, size, "<use ui=\"%s\"/>", name);
}

bool designer_add_items(studio *s, char *markup, size_t size, char *label, size_t label_size)
{
    kit *k = s->kit;
    ui_buffer *b;
    const studio_designer *d = designer_state(s, &b);
    int chosen = -1;
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof key, "e%d", i);
        kit_menu_item_desc item = { .label = ELEMENTS[i].label,
                                    .icon = ELEMENTS[i].icon,
                                    .checked = -1 };
        chosen = kit_menu_item(k, key, &item) ? i : chosen;
    }
    if (chosen >= 0) {
        snprintf(markup, size, "%s", ELEMENTS[chosen].markup);
        snprintf(label, label_size, "%s", ELEMENTS[chosen].label);
    }
    const char **names = widget_names(s, d);
    if (acount(names)) {
        kit_menu_separator(k, "widgets-sep");
    }
    if (acount(names) && kit_submenu_begin(k, "widgets", "Widget", KIT_ICON_WIDGET)) {
        for (int i = 0; i < acount(names); i++) {
            char key[16];
            snprintf(key, sizeof key, "w%d", i);
            kit_menu_item_desc item = { .label = names[i],
                                        .icon = KIT_ICON_WIDGET,
                                        .checked = -1 };
            if (kit_menu_item(k, key, &item)) {
                widget_markup(names[i], markup, size);
                snprintf(label, label_size, "%s", names[i]);
                chosen = ELEMENT_COUNT + i;
            }
        }
        kit_submenu_end(k);
    }
    afree(names);
    return chosen >= 0;
}

void studio_designer_library_header(studio *s, int)
{
    kit *k = s->kit;
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    kit_label(k, "mode", "designer-mode", "Library");
    kit_push(k, "filter", NULL, "assets-filter");
    kit_search(k, "search", d->library_filter, sizeof d->library_filter, "Filter");
    kit_pop(k);
}

/* A tile drags its markup to the Hierarchy or the Canvas, and a click adds
 * it to the selection. */
static bool tile(kit *k, const char *key, const char *label, uint32_t icon, const char *markup)
{
    kit_push(k, key, NULL, "designer-tile");
    kit_icon(k, "icon", "glyph", icon);
    kit_label(k, "label", "label", label);
    kit_pop(k);
    kit_tip(k, label, NULL, markup);
    kit_drag_source(
        k,
        &(kit_drag_desc){ .type = DESIGNER_DRAG, .payload = markup, .label = label, .icon = icon }
    );
    return kit_item_clicked(k);
}

static void section(kit *k, const char *key, const char *title)
{
    kit_push(k, key, NULL, "designer-section");
    kit_label(k, "title", "title", title);
    kit_pop(k);
}

void studio_designer_library(studio *s, int)
{
    kit *k = s->kit;
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    int added = -1, target = d->selected;
    char markup[512];
    kit_push(k, "library", NULL, "designer-library");
    section(k, "elements-title", "Elements");
    kit_push(k, "elements", NULL, "designer-tiles");
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof key, "e%d", i);
        if (studio_matches(d->library_filter, ELEMENTS[i].label) &&
            tile(k, key, ELEMENTS[i].label, ELEMENTS[i].icon, ELEMENTS[i].markup)) {
            added = i;
        }
    }
    kit_pop(k);
    const char **names = widget_names(s, d);
    section(k, "widgets-title", "Widgets");
    kit_push(k, "widgets", NULL, "designer-tiles");
    for (int i = 0; i < acount(names); i++) {
        char key[16];
        snprintf(key, sizeof key, "w%d", i);
        widget_markup(names[i], markup, sizeof markup);
        if (studio_matches(d->library_filter, names[i]) &&
            tile(k, key, names[i], KIT_ICON_WIDGET, markup)) {
            added = ELEMENT_COUNT + i;
        }
    }
    if (!acount(names)) {
        kit_label(k, "none", "designer-none", "No other ui assets yet");
    }
    kit_pop(k);
    kit_pop(k);
    if (added >= ELEMENT_COUNT) {
        widget_markup(names[added - ELEMENT_COUNT], markup, sizeof markup);
        designer_add(s, target, markup, names[added - ELEMENT_COUNT]);
    } else if (added >= 0) {
        designer_add(s, target, ELEMENTS[added].markup, ELEMENTS[added].label);
    }
    afree(names);
}
