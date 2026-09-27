/* Details (workstream H): the selected element's text, attributes, data
 * bindings and inline style, then the rules of the document's RCSS that
 * match it, as UI Builder's Inspector and UMG's Details panel show a
 * widget's. A value edits in the control its property's type calls for: a
 * length scrubs with its unit, a color picks, a keyword chooses, and
 * anything else is text. Each commit is a text edit of the document, and a
 * scrub or a picker session is one undo step. With nothing selected,
 * Details lists every rule of the document's RCSS. */
#include "editor/studio/studio_designer_internal.h"
#include "render/ui_check.h"
#include "support/array.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Suggestions an Add popover lists at once. */
#define SUGGESTIONS 12

/* ---------- Property types ---------- */

typedef enum
{
    VALUE_TEXT,
    VALUE_LENGTH,
    VALUE_NUMBER,
    VALUE_COLOR,
    VALUE_KEYWORD,
} value_kind;

typedef struct
{
    const char *name;
    value_kind kind;
    const char *const *keywords;
    int count;
} property_type;

#define KEYWORDS(list) list, (int)(sizeof list / sizeof *list)

static const char *const DISPLAY[] = { "none",         "block", "inline",
                                       "inline-block", "flex",  "inline-flex",
                                       "table" };
static const char *const POSITION[] = { "static", "relative", "absolute", "fixed" };
static const char *const FLEX_DIRECTION[] = { "row", "row-reverse", "column", "column-reverse" };
static const char *const FLEX_WRAP[] = { "nowrap", "wrap", "wrap-reverse" };
static const char *const JUSTIFY[] = { "flex-start",   "flex-end",
                                       "center",       "space-between",
                                       "space-around", "space-evenly" };
static const char *const ALIGN_ITEMS[] = { "flex-start", "flex-end", "center", "baseline", "stretch" };
static const char *const ALIGN_SELF[] = { "auto",   "flex-start", "flex-end",
                                          "center", "baseline",   "stretch" };
static const char *const TEXT_ALIGN[] = { "left", "center", "right", "justify" };
static const char *const OVERFLOW_MODES[] = { "visible", "hidden", "auto", "scroll" };
static const char *const VISIBILITY[] = { "visible", "hidden" };
static const char *const WHITE_SPACE[] = { "normal", "pre", "nowrap", "pre-wrap", "pre-line" };
static const char *const FONT_STYLE[] = { "normal", "italic" };
static const char *const FONT_WEIGHT[] = { "normal", "bold", "100", "200",
                                           "300",    "400",  "500", "600",
                                           "700",    "800",  "900" };
static const char *const TEXT_TRANSFORM[] = { "none", "capitalize", "uppercase", "lowercase" };
static const char *const TEXT_DECORATION[] = { "none", "underline", "overline", "line-through" };
static const char *const TEXT_OVERFLOW[] = { "clip", "ellipsis" };
static const char *const POINTER_EVENTS[] = { "auto", "none" };
static const char *const BOX_SIZING[] = { "content-box", "border-box" };

/* RCSS properties by the control their values take (RmlUi's property
 * reference); others edit as text. */
static const property_type TYPES[] = {
    { "width", VALUE_LENGTH },
    { "height", VALUE_LENGTH },
    { "min-width", VALUE_LENGTH },
    { "max-width", VALUE_LENGTH },
    { "min-height", VALUE_LENGTH },
    { "max-height", VALUE_LENGTH },
    { "left", VALUE_LENGTH },
    { "top", VALUE_LENGTH },
    { "right", VALUE_LENGTH },
    { "bottom", VALUE_LENGTH },
    { "margin", VALUE_LENGTH },
    { "margin-top", VALUE_LENGTH },
    { "margin-right", VALUE_LENGTH },
    { "margin-bottom", VALUE_LENGTH },
    { "margin-left", VALUE_LENGTH },
    { "padding", VALUE_LENGTH },
    { "padding-top", VALUE_LENGTH },
    { "padding-right", VALUE_LENGTH },
    { "padding-bottom", VALUE_LENGTH },
    { "padding-left", VALUE_LENGTH },
    { "border-width", VALUE_LENGTH },
    { "border-top-width", VALUE_LENGTH },
    { "border-right-width", VALUE_LENGTH },
    { "border-bottom-width", VALUE_LENGTH },
    { "border-left-width", VALUE_LENGTH },
    { "border-radius", VALUE_LENGTH },
    { "font-size", VALUE_LENGTH },
    { "line-height", VALUE_LENGTH },
    { "gap", VALUE_LENGTH },
    { "row-gap", VALUE_LENGTH },
    { "column-gap", VALUE_LENGTH },
    { "letter-spacing", VALUE_LENGTH },
    { "flex-basis", VALUE_LENGTH },
    { "opacity", VALUE_NUMBER },
    { "flex-grow", VALUE_NUMBER },
    { "flex-shrink", VALUE_NUMBER },
    { "z-index", VALUE_NUMBER },
    { "color", VALUE_COLOR },
    { "background-color", VALUE_COLOR },
    { "border-color", VALUE_COLOR },
    { "border-top-color", VALUE_COLOR },
    { "border-right-color", VALUE_COLOR },
    { "border-bottom-color", VALUE_COLOR },
    { "border-left-color", VALUE_COLOR },
    { "image-color", VALUE_COLOR },
    { "display", VALUE_KEYWORD, KEYWORDS(DISPLAY) },
    { "position", VALUE_KEYWORD, KEYWORDS(POSITION) },
    { "flex-direction", VALUE_KEYWORD, KEYWORDS(FLEX_DIRECTION) },
    { "flex-wrap", VALUE_KEYWORD, KEYWORDS(FLEX_WRAP) },
    { "justify-content", VALUE_KEYWORD, KEYWORDS(JUSTIFY) },
    { "align-content", VALUE_KEYWORD, KEYWORDS(JUSTIFY) },
    { "align-items", VALUE_KEYWORD, KEYWORDS(ALIGN_ITEMS) },
    { "align-self", VALUE_KEYWORD, KEYWORDS(ALIGN_SELF) },
    { "text-align", VALUE_KEYWORD, KEYWORDS(TEXT_ALIGN) },
    { "overflow", VALUE_KEYWORD, KEYWORDS(OVERFLOW_MODES) },
    { "overflow-x", VALUE_KEYWORD, KEYWORDS(OVERFLOW_MODES) },
    { "overflow-y", VALUE_KEYWORD, KEYWORDS(OVERFLOW_MODES) },
    { "visibility", VALUE_KEYWORD, KEYWORDS(VISIBILITY) },
    { "white-space", VALUE_KEYWORD, KEYWORDS(WHITE_SPACE) },
    { "font-style", VALUE_KEYWORD, KEYWORDS(FONT_STYLE) },
    { "font-weight", VALUE_KEYWORD, KEYWORDS(FONT_WEIGHT) },
    { "text-transform", VALUE_KEYWORD, KEYWORDS(TEXT_TRANSFORM) },
    { "text-decoration", VALUE_KEYWORD, KEYWORDS(TEXT_DECORATION) },
    { "text-overflow", VALUE_KEYWORD, KEYWORDS(TEXT_OVERFLOW) },
    { "pointer-events", VALUE_KEYWORD, KEYWORDS(POINTER_EVENTS) },
    { "box-sizing", VALUE_KEYWORD, KEYWORDS(BOX_SIZING) },
};

static const property_type *type_of(const char *property)
{
    for (size_t i = 0; i < sizeof TYPES / sizeof *TYPES; i++) {
        if (!strcmp(TYPES[i].name, property)) {
            return &TYPES[i];
        }
    }
    return NULL;
}

/* ---------- Values ---------- */

/* A number and its unit alone, as "12dp", "50%" or "0.5". */
static bool parse_number(const char *value, double *number, char *unit, size_t size)
{
    char *end;
    *number = strtod(value, &end);
    const char *start = end;
    while (isalpha((unsigned char)*end) || *end == '%') {
        end++;
    }
    size_t length = (size_t)(end - start);
    if (start == value || *end || length >= size || !isfinite(*number)) {
        return false;
    }
    snprintf(unit, size, "%.*s", (int)length, start);
    return true;
}

static void format_value(char *out, size_t size, double value, int precision, const char *unit)
{
    char number[48];
    snprintf(number, sizeof number, "%.*f", precision, value);
    size_t length = strlen(number);
    while (strchr(number, '.') &&
           (number[length - 1] == '0' || number[length - 1] == '.')) {
        number[--length] = '\0';
    }
    snprintf(out, size, "%s%s", strcmp(number, "-0") ? number : "0", unit);
}

static int hex_digit(char c)
{
    return isdigit((unsigned char)c)  ? c - '0'
         : isxdigit((unsigned char)c) ? tolower((unsigned char)c) - 'a' + 10
                                      : -1;
}

/* #rgb, #rgba, #rrggbb or #rrggbbaa as 0xRRGGBBAA. */
static bool parse_color(const char *value, uint32_t *rgba)
{
    size_t digits = value[0] == '#' ? strlen(value + 1) : 0;
    if (digits != 3 && digits != 4 && digits != 6 && digits != 8) {
        return false;
    }
    bool shorthand = digits <= 4;
    uint32_t color = 0;
    for (size_t i = 1; i <= digits; i++) {
        int v = hex_digit(value[i]);
        if (v < 0) {
            return false;
        }
        color = shorthand ? color << 8 | (uint32_t)(v * 17)
                          : color << 4 | (uint32_t)v;
    }
    *rgba = digits == 3 || digits == 6 ? color << 8 | 0xffu : color;
    return true;
}

static void format_color(char *out, size_t size, uint32_t rgba)
{
    if ((rgba & 0xffu) == 0xffu) {
        snprintf(out, size, "#%06x", (unsigned)(rgba >> 8));
    } else {
        snprintf(out, size, "#%08x", (unsigned)rgba);
    }
}

/* A property's control: its type's, or for a custom property its value's. */
static value_kind kind_of(const char *property, const char *value, const property_type **type)
{
    *type = type_of(property);
    uint32_t color;
    double number;
    char unit[8];
    bool custom = !strncmp(property, "--", 2);
    return *type                                ? (*type)->kind
         : custom && parse_color(value, &color) ? VALUE_COLOR
         : custom && parse_number(value, &number, unit, sizeof unit) ? VALUE_LENGTH
                                                                     : VALUE_TEXT;
}

static const char *default_value(const char *property)
{
    const property_type *type = type_of(property);
    switch (type ? type->kind : VALUE_TEXT) {
        case VALUE_LENGTH: return "0dp";
        case VALUE_NUMBER: return strcmp(property, "opacity") ? "0" : "1";
        case VALUE_COLOR: return "#ffffff";
        case VALUE_KEYWORD: return type->keywords[0];
        case VALUE_TEXT: break;
    }
    return "none";
}

/* A value's change this frame, and whether the gesture making it ended. */
typedef struct
{
    bool changed, committed;
    char text[256];
} value_edit;

/* Scrubs with its unit: dp and percentages by ones, ems and seconds by
 * tenths, opacity as a slider. An unwritten unit reads as dp. */
static bool number_control(kit *k, const char *property, value_kind kind, const char *value, value_edit *e)
{
    double number;
    char unit[8];
    if (!parse_number(value, &number, unit, sizeof unit)) {
        return false;
    }
    const char *shown = unit[0] || kind != VALUE_LENGTH ? unit : "dp";
    bool fine = !strcmp(shown, "em") || !strcmp(shown, "rem") || !strcmp(shown, "s");
    kit_number_desc desc = { .step = fine ? 0.1 : 1, .precision = 2, .unit = shown };
    if (!strcmp(property, "opacity")) {
        desc = (kit_number_desc){ .min = 0,
                                  .max = 1,
                                  .step = 0.01,
                                  .precision = 2,
                                  .slider = true };
    }
    desc.integer = !strcmp(property, "z-index");
    if (kit_number(k, "number", &number, &desc)) {
        e->changed = true;
        format_value(e->text, sizeof e->text, number, desc.precision, shown);
    }
    e->committed = kit_item_committed(k);
    return true;
}

static bool color_control(kit *k, const char *value, value_edit *e)
{
    uint32_t rgba;
    if (!parse_color(value, &rgba)) {
        return false;
    }
    if (kit_color(k, "color", &rgba, 0)) {
        e->changed = true;
        format_color(e->text, sizeof e->text, rgba);
    }
    e->committed = kit_item_committed(k);
    return true;
}

static bool keyword_control(kit *k, const property_type *type, const char *value, value_edit *e)
{
    int index = -1;
    for (int i = 0; i < type->count; i++) {
        index = !strcmp(type->keywords[i], value) ? i : index;
    }
    if (index < 0) {
        return false;
    }
    if (kit_choice(k, "keyword", &index, type->keywords, type->count, 0)) {
        e->changed = e->committed = true;
        snprintf(e->text, sizeof e->text, "%s", type->keywords[index]);
    }
    return true;
}

/* The control for a value its property's type calls for, or text when the
 * type is unknown or the value is written otherwise, as var(--x) is. */
static value_edit value_control(kit *k, const char *property, const char *value)
{
    value_edit e = { 0 };
    snprintf(e.text, sizeof e.text, "%s", value);
    const property_type *type;
    value_kind kind = kind_of(property, value, &type);
    bool typed = kind == VALUE_LENGTH || kind == VALUE_NUMBER
                   ? number_control(k, property, kind, value, &e)
               : kind == VALUE_COLOR
                   ? color_control(k, value, &e)
                   : kind == VALUE_KEYWORD && keyword_control(k, type, value, &e);
    if (!typed && kit_text_field(k, "text", e.text, sizeof e.text, NULL, 0)) {
        e.changed = e.committed = true;
    }
    return e;
}

/* ---------- Changes ---------- */

typedef struct
{
    studio *s;
    studio_designer *d;
    ui_buffer *b;
    bool edited; /* the text changed this frame: later rows show the text before */
} details;

static void edited(details *x, bool ok, const char *error, const char *failed)
{
    x->edited = true;
    if (!ok) {
        designer_warn(x->s, failed, error);
    }
}

/* A value of the element's inline style, or of a rule's declaration. */
typedef struct
{
    int element; /* -1 for a rule */
    int rule, declaration;
    const char *property;
} value_target;

/* A scrub's or a picker's changes to one value join one undo step while
 * nothing else changed the text between them. */
static void apply_value(details *x, const value_target *t, const value_edit *e)
{
    studio_designer *d = x->d;
    char key[sizeof d->gesture.key];
    snprintf(key, sizeof key, "%d:%d:%d:%s", t->element, t->rule, t->declaration, t->property);
    bool mine = !strcmp(d->gesture.key, key);
    if (!e->changed || x->edited) {
        d->gesture.active = d->gesture.active && !(mine && e->committed);
        return;
    }
    bool join = d->gesture.active && mine && d->gesture.revision == x->b->revision;
    unsigned before = x->b->revision;
    char error[192] = "";
    bool ok = t->element >= 0
                ? ui_design_set_style(x->b, t->element, t->property, e->text, join, error, sizeof error)
                : ui_design_set_declaration(x->b, t->rule, t->declaration, e->text, join, error, sizeof error);
    d->gesture.active = ok && !e->committed && (join || x->b->revision != before);
    snprintf(d->gesture.key, sizeof d->gesture.key, "%s", key);
    d->gesture.revision = x->b->revision;
    edited(x, ok, error, "Could not change the value");
}

/* ---------- Attributes ---------- */

static bool shown(const details *x, const char *name)
{
    return studio_matches(x->s->property_search, name);
}

static const ui_source_attribute *attribute_of(const ui_source *rml, int element, const char *name)
{
    const ui_source_element *el = &rml->elements[element];
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        if ((size_t)(a->name_end - a->name_start) == strlen(name) &&
            !strncmp(rml->text + a->name_start, name, strlen(name))) {
            return a;
        }
    }
    return NULL;
}

/* An attribute's value as written, which writes back unchanged. */
static void written_value(const ui_source *rml, const ui_source_attribute *a, char *out, size_t size)
{
    int length = a && a->value_start >= 0 ? a->value_end - a->value_start : 0;
    snprintf(out, size, "%.*s", length, length ? rml->text + a->value_start : "");
}

static bool remove_button(kit *k)
{
    kit_button_desc remove = { .icon = KIT_ICON_CLOSE,
                               .size = KIT_SM,
                               .cls = "remove",
                               .tip = "Remove" };
    return kit_button(k, "remove", &remove);
}

static void attribute_row(details *x, int element, int index, const char *name, const char *value)
{
    if (!shown(x, name)) {
        return;
    }
    kit *k = x->s->kit;
    char key[16], text[512];
    snprintf(key, sizeof key, "a%d", index);
    snprintf(text, sizeof text, "%s", value);
    kit_prop_begin(k, key, name);
    bool committed = kit_text_field(k, "value", text, sizeof text, NULL, 0);
    bool removed = remove_button(k);
    kit_prop_end(k);
    if ((committed || removed) && !x->edited) {
        char error[192] = "";
        bool ok = ui_design_set_attribute(x->b, element, name, removed ? NULL : text, error, sizeof error);
        edited(x, ok, error, "Could not change the attribute");
    }
}

static void id_row(details *x, int element)
{
    if (!shown(x, "id")) {
        return;
    }
    kit *k = x->s->kit;
    const ui_source_attribute *a = attribute_of(x->d->rml, element, "id");
    char value[128];
    written_value(x->d->rml, a, value, sizeof value);
    kit_prop_begin(k, "id", "id");
    bool committed = kit_text_field(k, "value", value, sizeof value, "None", 0);
    kit_prop_end(k);
    if (committed && !x->edited && (value[0] || a)) {
        char error[192] = "";
        bool ok = ui_design_set_attribute(x->b, element, "id", value[0] ? value : NULL, error, sizeof error);
        edited(x, ok, error, "Could not change the id");
    }
}

/* The text of an element holding text alone, without the space around it. */
static void text_row(details *x, int element)
{
    if (!shown(x, "text")) {
        return;
    }
    kit *k = x->s->kit;
    const ui_source_element *el = &x->d->rml->elements[element];
    const char *t = x->d->rml->text;
    int start = el->open_end, end = el->close_start;
    while (start < end && isspace((unsigned char)t[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)t[end - 1])) {
        end--;
    }
    char text[512];
    snprintf(text, sizeof text, "%.*s", end - start, t + start);
    kit_prop_begin(k, "text", "text");
    bool committed = kit_text_field(k, "value", text, sizeof text, "None", 0);
    kit_prop_end(k);
    if (committed && !x->edited) {
        char error[192] = "";
        edited(x, ui_design_set_text(x->b, element, text, error, sizeof error), error, "Could not change the text");
    }
}

/* A <use>'s widget, chosen among the project's ui assets. */
static void widget_row(details *x, int element)
{
    if (!shown(x, "widget")) {
        return;
    }
    kit *k = x->s->kit;
    char value[128], key[48];
    written_value(x->d->rml, attribute_of(x->d->rml, element, "ui"), value, sizeof value);
    snprintf(key, sizeof key, "designer-widget:%d", element);
    kit_prop_begin(k, "widget", "widget");
    const char *chosen = studio_asset_field(
        x->s,
        key,
        value,
        &(studio_pick_desc){ .kind = REFERENCE_ASSET_UI },
        0
    );
    kit_prop_end(k);
    if (chosen && chosen[0] && strcmp(chosen, value) && !x->edited) {
        char error[192] = "";
        bool ok = ui_design_set_attribute(x->b, element, "ui", chosen, error, sizeof error);
        edited(x, ok, error, "Could not change the widget");
    }
}

/* ---------- Add popovers ---------- */

typedef enum
{
    ADD_CLASS,
    ADD_ATTRIBUTE,
    ADD_STYLE,
} add_kind;

static const char *const ADD_TITLES[] = { "Add Class", "Add Attribute", "Add Property" };
static const char *const ADD_HINTS[] = { "Class name", "name=value", "property: value" };

/* Attributes RmlUi documents take besides id, class and style. */
static const char *const ATTRIBUTES[] = {
    "data-if",   "data-for",  "data-event-click", "data-value", "data-checked",
    "data-keys", "autofocus", "placeholder",      "type",       "value",
    "src",       "disabled",
};

static bool enter_pressed(const studio *s)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if (s->keys[i].key == SDLK_RETURN || s->keys[i].key == SDLK_KP_ENTER) {
            return true;
        }
    }
    return false;
}

static bool has_class(const char *classes, const char *name, size_t length)
{
    for (const char *c = classes ? classes : ""; *c;) {
        size_t n = strcspn(c, " \t\n");
        if (n == length && !strncmp(c, name, n)) {
            return true;
        }
        c += n + (c[n] != '\0');
    }
    return false;
}

static bool name_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* The classes the document's selectors name that the element lacks. */
static void class_choices(const details *x, int element, char ***out)
{
    const ui_source_element *el = &x->d->rml->elements[element];
    for (int r = 0; r < acount(x->d->rcss->rules); r++) {
        for (const char *c = strchr(x->d->rcss->rules[r].selector, '.'); c;
             c = strchr(c + 1, '.')) {
            size_t n = 0;
            while (name_char(c[1 + n])) {
                n++;
            }
            char name[128];
            snprintf(name, sizeof name, "%.*s", (int)n, c + 1);
            if (n && !has_class(el->classes, name, n)) {
                ui_source_list_add(out, name);
            }
        }
    }
}

/* What an Add popover suggests for what is typed so far. */
static char **suggestions(details *x, int element, add_kind kind)
{
    char **all = NULL, **matches = NULL;
    if (kind == ADD_CLASS) {
        class_choices(x, element, &all);
    } else if (kind == ADD_ATTRIBUTE) {
        for (size_t i = 0; i < sizeof ATTRIBUTES / sizeof *ATTRIBUTES; i++) {
            if (!attribute_of(x->d->rml, element, ATTRIBUTES[i])) {
                ui_source_list_add(&all, ATTRIBUTES[i]);
            }
        }
    } else {
        ui_check_property_names(&all);
    }
    for (int i = 0; i < acount(all); i++) {
        if (studio_matches(x->d->add.text, all[i]) && acount(matches) < SUGGESTIONS) {
            apush(matches, all[i]);
        } else {
            free(all[i]);
        }
    }
    afree(all);
    return matches;
}

/* "name=value", "property: value" or a name alone, split at `separator`. */
static void split(const char *text, char separator, char *name, size_t name_size, char *value, size_t value_size)
{
    const char *at = strchr(text, separator);
    int length = at ? (int)(at - text) : (int)strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        length--;
    }
    snprintf(name, name_size, "%.*s", length, text);
    const char *rest = at ? at + 1 : "";
    while (isspace((unsigned char)*rest) || *rest == '"') {
        rest++;
    }
    snprintf(value, value_size, "%s", rest);
    for (size_t n = strlen(value); n && (isspace((unsigned char)value[n - 1]) ||
                                         value[n - 1] == '"' || value[n - 1] == ';');) {
        value[--n] = '\0';
    }
}

static bool add_value(details *x, int element, add_kind kind, const char *text)
{
    char name[128], value[256], error[192] = "";
    bool ok = false;
    switch (kind) {
        case ADD_CLASS:
            ok = ui_design_class(x->b, element, text, true, error, sizeof error);
            break;
        case ADD_ATTRIBUTE:
            split(text, '=', name, sizeof name, value, sizeof value);
            ok = ui_design_set_attribute(x->b, element, name, value, error, sizeof error);
            break;
        case ADD_STYLE:
            split(text, ':', name, sizeof name, value, sizeof value);
            ok = ui_design_set_style(
                x->b,
                element,
                name,
                value[0] ? value : default_value(name),
                false,
                error,
                sizeof error
            );
            break;
    }
    edited(x, ok, error, ADD_TITLES[kind]);
    return ok;
}

/* A field to type the new name, or name and value, over what the document
 * suggests; Enter adds what is typed, a suggestion adds itself. */
static void add_popover(details *x, int element, add_kind kind, const char *key)
{
    kit *k = x->s->kit;
    studio_designer *d = x->d;
    if (!kit_popup_begin(k, key, "menu designer-add-menu")) {
        return;
    }
    kit_menu_group(k, "title", ADD_TITLES[kind]);
    kit_push(k, "field", NULL, "designer-add-field");
    kit_search(k, "name", d->add.text, sizeof d->add.text, ADD_HINTS[kind]);
    if (d->add.focus) {
        kit_focus(k);
        d->add.focus = false;
    }
    kit_pop(k);
    char **choices = suggestions(x, element, kind);
    if (acount(choices)) {
        kit_menu_separator(k, "sep");
    }
    int chosen = -1;
    for (int i = 0; i < acount(choices); i++) {
        char item[8];
        snprintf(item, sizeof item, "s%d", i);
        kit_menu_item_desc desc = { .label = choices[i], .checked = -1 };
        chosen = kit_menu_item(k, item, &desc) ? i : chosen;
    }
    bool entered = enter_pressed(x->s) && d->add.text[0];
    kit_popup_end(k);
    const char *text = chosen >= 0 ? choices[chosen]
                     : entered     ? d->add.text
                                   : NULL;
    if (text && !x->edited && add_value(x, element, kind, text)) {
        kit_popup_close(k, key);
    }
    for (int i = 0; i < acount(choices); i++) {
        free(choices[i]);
    }
    afree(choices);
}

static void add_button(details *x, int element, add_kind kind, const char *label)
{
    kit *k = x->s->kit;
    char key[32];
    snprintf(key, sizeof key, "designer-add-%d", (int)kind);
    kit_button_desc add = { .icon = KIT_ICON_ADD,
                            .label = label,
                            .size = KIT_SM,
                            .cls = "designer-add" };
    if (kit_button(k, "add", &add)) {
        kit_popup_open(k, key);
        x->d->add.focus = true;
        x->d->add.text[0] = '\0';
    }
    add_popover(x, element, kind, key);
}

/* ---------- Panels ---------- */

static void class_chip(details *x, int element, const char *name, int length, int index)
{
    kit *k = x->s->kit;
    char key[16], text[128];
    snprintf(key, sizeof key, "c%d", index);
    snprintf(text, sizeof text, "%.*s", length, name);
    kit_push(k, key, NULL, "designer-chip");
    kit_label(k, "name", "name", text);
    kit_button_desc remove = { .icon = KIT_ICON_CLOSE, .size = KIT_SM, .tip = "Remove class" };
    bool removed = kit_button(k, "remove", &remove);
    kit_pop(k);
    if (removed && !x->edited) {
        char error[192] = "";
        edited(x, ui_design_class(x->b, element, text, false, error, sizeof error), error, "Could not remove the class");
    }
}

/* Classes as chips, each removable, and a button adding one. */
static void class_row(details *x, int element)
{
    if (!shown(x, "class")) {
        return;
    }
    kit *k = x->s->kit;
    const char *classes = x->d->rml->elements[element].classes;
    kit_prop_begin(k, "class", "class");
    kit_push(k, "chips", NULL, "designer-chips");
    int index = 0;
    for (const char *c = classes ? classes : ""; *c;) {
        size_t n = strcspn(c, " \t\n");
        if (n) {
            class_chip(x, element, c, (int)n, index++);
        }
        c += n + (c[n] != '\0');
    }
    add_button(x, element, ADD_CLASS, NULL);
    kit_pop(k);
    kit_prop_end(k);
}

static void attribute_name(const ui_source *rml, const ui_source_attribute *a, char *out, size_t size)
{
    snprintf(out, size, "%.*s", a->name_end - a->name_start, rml->text + a->name_start);
}

/* Rows of their own show these. */
static bool own_row(const char *tag, const char *name)
{
    return !strcmp(name, "id") || !strcmp(name, "class") ||
           !strcmp(name, "style") || (!strcmp(tag, "use") && !strcmp(name, "ui"));
}

static void attributes_panel(details *x, int element)
{
    kit *k = x->s->kit;
    const ui_source *rml = x->d->rml;
    const ui_source_element *el = &rml->elements[element];
    if (kit_panel_begin(k, "attributes", "Attributes", NULL, true)) {
        if (!strcmp(el->tag, "use")) {
            widget_row(x, element);
        }
        if (ui_design_text_only(rml, element)) {
            text_row(x, element);
        }
        id_row(x, element);
        class_row(x, element);
        for (int i = 0; i < acount(el->attributes); i++) {
            const ui_source_attribute *a = &el->attributes[i];
            char name[128], value[512];
            attribute_name(rml, a, name, sizeof name);
            written_value(rml, a, value, sizeof value);
            if (!own_row(el->tag, name) &&
                !ui_source_is_binding(name, (int)strlen(name))) {
                attribute_row(x, element, i, name, value);
            }
        }
        kit_push(k, "more", NULL, "designer-more");
        add_button(x, element, ADD_ATTRIBUTE, "Add Attribute");
        kit_pop(k);
    }
    kit_panel_end(k);
}

/* Data bindings, as RmlUi's data-* attributes write them, in code. */
static void bindings_panel(details *x, int element)
{
    kit *k = x->s->kit;
    const ui_source *rml = x->d->rml;
    const ui_source_element *el = &rml->elements[element];
    int count = 0;
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        count += ui_source_is_binding(rml->text + a->name_start, a->name_end - a->name_start);
    }
    if (!count) {
        return;
    }
    if (kit_panel_begin(k, "bindings", "Bindings", NULL, true)) {
        kit_push(k, "rows", NULL, "designer-bindings");
        for (int i = 0; i < acount(el->attributes); i++) {
            const ui_source_attribute *a = &el->attributes[i];
            char name[128], value[512];
            attribute_name(rml, a, name, sizeof name);
            written_value(rml, a, value, sizeof value);
            if (ui_source_is_binding(name, (int)strlen(name))) {
                attribute_row(x, element, i, name, value);
            }
        }
        kit_pop(k);
    }
    kit_panel_end(k);
}

static void style_row(details *x, int element, int index, const char *property, const char *value)
{
    kit *k = x->s->kit;
    char key[16];
    snprintf(key, sizeof key, "s%d", index);
    kit_prop_begin(k, key, property);
    value_edit e = value_control(k, property, value);
    value_target t = { .element = element, .rule = -1, .declaration = -1, .property = property };
    apply_value(x, &t, &e);
    bool removed = remove_button(k);
    kit_prop_end(k);
    if (removed && !x->edited) {
        char error[192] = "";
        bool ok = ui_design_set_style(x->b, element, property, NULL, false, error, sizeof error);
        edited(x, ok, error, "Could not remove the property");
    }
}

/* The style attribute's declarations, each in its type's control. */
static void style_panel(details *x, int element)
{
    kit *k = x->s->kit;
    char style[1024];
    written_value(x->d->rml, attribute_of(x->d->rml, element, "style"), style, sizeof style);
    if (kit_panel_begin(k, "style", "Inline Style", NULL, true)) {
        int count = 0;
        for (const char *at = style; *at;) {
            size_t length = strcspn(at, ";");
            char declaration[512], property[128], value[256];
            snprintf(declaration, sizeof declaration, "%.*s", (int)length, at);
            at += length + (at[length] == ';');
            split(declaration, ':', property, sizeof property, value, sizeof value);
            const char *name = property;
            while (isspace((unsigned char)*name)) {
                name++;
            }
            if (name[0] && shown(x, name)) {
                style_row(x, element, count, name, value);
            }
            count += name[0] != '\0';
        }
        if (!count) {
            kit_label(k, "none", "designer-none", "No inline style");
        }
        kit_push(k, "more", NULL, "designer-more");
        add_button(x, element, ADD_STYLE, "Add Property");
        kit_pop(k);
    }
    kit_panel_end(k);
}

/* ---------- Rules ---------- */

/* Past a pseudo class, as :hover or :not(...), or an attribute test. */
static int skip_test(const char *text, int at, int end, char kind)
{
    if (kind == '[') {
        while (at < end && text[at] != ']') {
            at++;
        }
        return at + (at < end);
    }
    at += at < end && text[at] == ':';
    while (at < end && name_char(text[at])) {
        at++;
    }
    for (int depth = 0; at < end && (depth || text[at] == '(');) {
        depth += (text[at] == '(') - (text[at] == ')');
        at++;
    }
    return at;
}

/* Whether a compound selector's tag, id and classes all hold for the
 * element. Pseudo classes and attribute tests are left out, so a rule for
 * a state, as :hover, shows too. */
static bool compound_matches(const char *text, int start, int end, const ui_source_element *el)
{
    int at = start;
    while (at < end && name_char(text[at])) {
        at++;
    }
    bool named = at > start;
    if (named && ((size_t)(at - start) != strlen(el->tag) ||
                  strncasecmp(text + start, el->tag, (size_t)(at - start)))) {
        return false;
    }
    if (!named && at < end && text[at] == '*') {
        named = true;
        at++;
    }
    while (at < end) {
        char kind = text[at++];
        if (kind == ':' || kind == '[') {
            at = skip_test(text, at, end, kind);
            continue;
        }
        int name = at;
        while (at < end && name_char(text[at])) {
            at++;
        }
        size_t length = (size_t)(at - name);
        bool holds = kind == '.' ? has_class(el->classes, text + name, length)
                   : kind == '#' ? el->id && strlen(el->id) == length &&
                                       !strncmp(el->id, text + name, length)
                                 : false;
        if (!holds || !length) {
            return false;
        }
        named = true;
    }
    return named;
}

/* One compound of a selector and the combinator before it: ' ', '>', '+',
 * '~', or 0 for the first. */
typedef struct
{
    int start, end;
    char combinator;
} compound;

enum
{
    COMPOUNDS = 16,
};

static int split_compounds(const char *text, int start, int end, compound *out)
{
    int count = 0;
    char combinator = 0;
    for (int at = start; at < end && count < COMPOUNDS;) {
        if (text[at] == ' ' || strchr(">+~", text[at])) {
            combinator = text[at] == ' ' && combinator ? combinator : text[at];
            at++;
            continue;
        }
        int from = at;
        for (int depth = 0; at < end && (depth || !strchr(" >+~", text[at])); at++) {
            depth += (text[at] == '(' || text[at] == '[') -
                     (text[at] == ')' || text[at] == ']');
        }
        out[count] = (compound){ from, at, count ? combinator : 0 };
        count++;
        combinator = 0;
    }
    return count;
}

/* `body` in a document's RCSS names its host, above all its elements. */
static bool host(const char *text, const compound *c)
{
    return c->end - c->start >= 4 && !strncmp(text + c->start, "body", 4) &&
           !name_char(text[c->start + 4]);
}

/* Whether compounds [0, last] match `element` and its ancestors, right to
 * left. Sibling combinators are not checked, and a widget's own elements
 * between a <use> and its slot's content are not in the markup. */
static bool matches_at(const ui_source *rml, const char *text, const compound *c, int last, int element)
{
    if (host(text, &c[last])) {
        return true;
    }
    if (element < 0 ||
        !compound_matches(text, c[last].start, c[last].end, &rml->elements[element])) {
        return false;
    }
    if (last == 0 || c[last].combinator == '+' || c[last].combinator == '~') {
        return true;
    }
    int parent = rml->elements[element].parent;
    if (c[last].combinator == '>') {
        return matches_at(rml, text, c, last - 1, parent);
    }
    for (int a = parent; a >= 0; a = rml->elements[a].parent) {
        if (matches_at(rml, text, c, last - 1, a)) {
            return true;
        }
    }
    return host(text, &c[last - 1]);
}

/* Whether one of the selector's comma-separated selectors matches the
 * element in the markup. */
static bool rule_matches(const ui_source *rml, const char *selector, int element)
{
    int length = (int)strlen(selector);
    for (int start = 0; start < length;) {
        int end = start;
        for (int depth = 0; end < length && (selector[end] != ',' || depth); end++) {
            depth += (selector[end] == '(') - (selector[end] == ')');
        }
        compound c[COMPOUNDS];
        int count = split_compounds(selector, start, end, c);
        if (count && !host(selector, &c[count - 1]) &&
            matches_at(rml, selector, c, count - 1, element)) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

static void declaration_row(details *x, int rule, int index)
{
    kit *k = x->s->kit;
    const ui_source *rcss = x->d->rcss;
    const ui_source_declaration *d = &rcss->rules[rule].declarations[index];
    char property[128], value[256], key[16];
    snprintf(property, sizeof property, "%.*s", d->property_end - d->property_start, rcss->text + d->property_start);
    snprintf(value, sizeof value, "%.*s", d->value_end - d->value_start, rcss->text + d->value_start);
    if (!shown(x, property)) {
        return;
    }
    snprintf(key, sizeof key, "d%d", index);
    kit_prop_begin(k, key, property);
    value_edit e = value_control(k, property, value);
    value_target t = { .element = -1, .rule = rule, .declaration = index, .property = property };
    apply_value(x, &t, &e);
    kit_prop_end(k);
}

/* A rule by its selector, its line opening it in the Text Editor. */
static void rule_panel(details *x, int rule)
{
    kit *k = x->s->kit;
    const ui_source_rule *r = &x->d->rcss->rules[rule];
    char key[16], origin[32];
    snprintf(key, sizeof key, "rule%d", rule);
    snprintf(origin, sizeof origin, "rcss:%d", r->line);
    if (kit_panel_begin(k, key, r->selector, NULL, true)) {
        kit_push(k, "origin", NULL, "designer-origin");
        if (r->parent >= 0) {
            kit_label(k, "within", "within", x->d->rcss->rules[r->parent].selector);
        }
        kit_leaf(k, "spacer", NULL, "spacer");
        kit_button_desc open = { .label = origin,
                                 .icon = KIT_ICON_EXTERNAL,
                                 .size = KIT_SM,
                                 .tip = "Edit Source" };
        if (kit_button(k, "open", &open)) {
            studio_open_ui_part(x->s, x->d->path, UI_PART_RCSS, r->line);
        }
        kit_pop(k);
        for (int i = 0; i < acount(r->declarations); i++) {
            declaration_row(x, rule, i);
        }
    }
    kit_panel_end(k);
}

/* The document's rules that match the element, or all of them for -1. */
static int rules(details *x, int element)
{
    const ui_source *rcss = x->d->rcss;
    int count = 0;
    for (int r = 0; r < acount(rcss->rules); r++) {
        const char *selector = rcss->rules[r].selector;
        if (selector[0] != '@' &&
            (element < 0 || rule_matches(x->d->rml, selector, element))) {
            rule_panel(x, r);
            count++;
        }
    }
    return count;
}

/* The element's glyph, name and kind, with its source line as a link to
 * the Text Editor when it has one: true when that was clicked. */
static bool identity(kit *k, uint32_t icon, const char *title, const char *kind, const char *source)
{
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, icon);
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    kit_label(k, "name", "title", title);
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "kind", NULL, kind);
    bool opened = false;
    if (source) {
        kit_button_desc link = { .label = source, .icon = KIT_ICON_EDIT, .tip = "Edit Source" };
        opened = kit_button(k, "source", &link);
    }
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
    return opened;
}

static void element_details(details *x, int element)
{
    kit *k = x->s->kit;
    const ui_source *rml = x->d->rml;
    const ui_source_element *el = &rml->elements[element];
    char name[96], title[256], kind[80], source[160];
    designer_element_name(rml, element, name, sizeof name);
    snprintf(title, sizeof title, "%s%s%s", name, el->id ? "#" : "", el->id ? el->id : "");
    if (strcmp(el->tag, "use")) {
        snprintf(kind, sizeof kind, "<%s>", el->tag);
    } else {
        snprintf(kind, sizeof kind, "Widget");
    }
    snprintf(source, sizeof source, "%s:%d", x->d->asset, el->line);
    if (identity(k, designer_element_icon(rml, element), title, kind, source)) {
        studio_open_ui_source(x->s, x->d->path, el->line);
    }
    attributes_panel(x, element);
    bindings_panel(x, element);
    style_panel(x, element);
    if (!rules(x, element) && !x->s->property_search[0]) {
        kit_label(k, "unstyled", "designer-none", "No rule of the document's RCSS matches it");
    }
}

static void document_details(details *x)
{
    kit *k = x->s->kit;
    char kind[64];
    snprintf(
        kind,
        sizeof kind,
        "UI document \xc2\xb7 %d elements",
        acount(x->d->rml->elements)
    );
    identity(k, KIT_ICON_LAYOUT, x->d->asset, kind, NULL);
    kit_label(k, "hint", "designer-hint", "Select an element to edit it. The document's rules:");
    if (!rules(x, -1)) {
        kit_label(k, "none", "designer-none", "The document has no RCSS rules");
    }
}

void studio_designer_details_header(studio *s, int)
{
    kit *k = s->kit;
    kit_label(k, "mode", "designer-mode", "Details");
    kit_search(k, "search", s->property_search, sizeof s->property_search, "Search properties");
}

void studio_designer_details(studio *s, int)
{
    kit *k = s->kit;
    details x = { .s = s };
    x.d = designer_state(s, &x.b);
    assert(x.d);
    kit_push(k, "designer", NULL, "properties designer-details");
    kit_push(k, "scroll", NULL, "prop-scroll");
    if (x.d->selected >= 0) {
        element_details(&x, x.d->selected);
    } else {
        document_details(&x);
    }
    kit_pop(k);
    kit_pop(k);
}
