/* Details (workstream H, authored in D1): the selected element's text,
 * attributes, data bindings and inline style, then the rules of the
 * document's RCSS that match it, as UI Builder's Inspector and UMG's
 * Details panel show a widget's. A value edits in the control its
 * property's type calls for: a length scrubs with its unit, a color picks,
 * a keyword chooses, and anything else is text. Each commit is a text edit
 * of the document, and a scrub or a picker session is one undo step. A
 * rule's declarations are added and removed in place, and Extract to Class
 * moves the inline style into a new rule (workstream Q). With nothing
 * selected, Details lists every rule of the document's RCSS.
 *
 * Details.rml and DetailsHeader.rml bind two sources. `details` holds the
 * panels, each a head and rows (the shape the panel builds one control from):
 *
 *   details: { query, hint, note,
 *     identity: { icon, title, kind, source },
 *     sections: [{ key, title, kind, open, origin, within, none, rule,
 *                  addLabel, addKind, addTarget, extract, rows: [ROW] }] }
 *   ROW: { id, label, control, text, placeholder, remove,
 *          number, unit, step, min, max, precision, integer, slider,
 *          color, value, options: [{ label, pressed }],
 *          table: TABLE, chips: [{ name }] }
 *
 * `control` is text, number, color, choice (an EnumField over `table`),
 * toggle (a segmented control over `options`), asset (the widget a <use>
 * shows) or chips (a class list). A row's id names what it edits: "a<i>"
 * an attribute, "id", "text", "widget", "s:<property>" an inline style
 * property, "d<rule>.<declaration>" a declaration. `details_add` holds the
 * Add popover shared by every Add button: its title, hint, the text typed
 * and the suggestions.
 *
 * Intents: details.input and .change take a row's id and value (and
 * whether Escape canceled); .text a text field's commit (read from the
 * field); .remove a row; .removeClass a chip; .add opens the popover for a
 * kind and a target; .addPick and .addEnter add what it names; .fold a
 * panel; .source and .openRule open the Text Editor; .choices a keyword
 * list; .cancel puts a text field back. */
#include "editor/studio/studio_designer_internal.h"
#include "render/ui_check.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "ui/popover.h"

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

/* ---------- The document's side ---------- */

/* What the Add popover's suggestions and a change work on. */
typedef struct
{
    studio *s;
    studio_designer *d;
    ui_buffer *b;
} details;

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

/* ---------- Add popovers ---------- */

/* What an Add popover adds to: the element, or for a declaration its rule
 * (workstream Q); Extract to Class names the class the inline style moves
 * to. */
typedef enum
{
    ADD_CLASS,
    ADD_ATTRIBUTE,
    ADD_STYLE,
    ADD_DECLARATION,
    ADD_EXTRACT,
} add_kind;

static const char *const ADD_TITLES[] = {
    "Add Class",    "Add Attribute",    "Add Property",
    "Add Property", "Extract to Class",
};
static const char *const ADD_HINTS[] = {
    "Class name",      "name=value",     "property: value",
    "property: value", "New class name",
};

/* Attributes RmlUi documents take besides id, class and style. */
static const char *const ATTRIBUTES[] = {
    "data-if",      "data-when", "data-for",  "data-event-click", "data-value",
    "data-checked", "data-keys", "autofocus", "placeholder",      "type",
    "value",        "src",       "disabled",
};

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
    } else if (kind != ADD_EXTRACT) {
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

/* ---------- A value's control ---------- */

/* What a property's value shows as: the control its type calls for, and
 * what that control reads. */
typedef struct
{
    const char *control;       /* text, number, color, choice or toggle */
    const property_type *type; /* a keyword's */
    int keyword;               /* and which of them the value is */
    char number[48], unit[8], color[16];
    double step, min, max;
    int precision;
    bool integer, slider;
} value_shape;

static bool fine_unit(const char *unit)
{
    return !strcmp(unit, "em") || !strcmp(unit, "rem") || !strcmp(unit, "s");
}

/* A length scrubs with its unit, dp and percentages by ones, ems and
 * seconds by tenths, opacity as a slider. An unwritten unit reads as dp. A
 * value its type does not call a control for, as var(--x), is text. */
static value_shape shape_of(const char *property, const char *value)
{
    value_shape v = { .control = "text", .min = -INFINITY, .max = INFINITY };
    const property_type *type;
    value_kind kind = kind_of(property, value, &type);
    double number;
    char unit[8];
    uint32_t rgba;
    if ((kind == VALUE_LENGTH || kind == VALUE_NUMBER) &&
        parse_number(value, &number, unit, sizeof unit)) {
        const char *shown_unit = unit[0] || kind != VALUE_LENGTH ? unit : "dp";
        v.control = "number";
        snprintf(v.number, sizeof v.number, "%.17g", number);
        snprintf(v.unit, sizeof v.unit, "%s", shown_unit);
        v.step = fine_unit(shown_unit) ? 0.1 : 1;
        v.precision = 2;
        v.integer = !strcmp(property, "z-index");
        if (!strcmp(property, "opacity")) {
            v.min = 0;
            v.max = 1;
            v.step = 0.01;
            v.slider = true;
        }
    } else if (kind == VALUE_COLOR && parse_color(value, &rgba)) {
        v.control = "color";
        snprintf(v.color, sizeof v.color, "#%08x", (unsigned)rgba);
    } else if (kind == VALUE_KEYWORD) {
        for (int i = 0; i < type->count; i++) {
            if (!strcmp(type->keywords[i], value)) {
                v.control = type->count <= 3 ? "toggle" : "choice";
                v.type = type;
                v.keyword = i;
            }
        }
    }
    return v;
}

/* What a control sends back, as the text the document gets, for the value
 * `current`; false when it is not a value that control takes. */
static bool value_text(const char *property, const char *current, const char *argument, char *out, size_t size)
{
    value_shape v = shape_of(property, current);
    if (!strcmp(v.control, "number")) {
        char *end;
        double number = strtod(argument, &end);
        if (end == argument || !isfinite(number)) {
            return false;
        }
        format_value(out, size, number, v.precision, v.unit);
        return true;
    }
    uint32_t rgba;
    if (!strcmp(v.control, "color")) {
        if (!parse_color(argument, &rgba)) {
            return false;
        }
        format_color(out, size, rgba);
        return true;
    }
    for (int i = 0; v.type && i < v.type->count; i++) {
        if (!strcmp(v.type->keywords[i], argument)) {
            snprintf(out, size, "%s", argument);
            return true;
        }
    }
    return false;
}

/* ---------- Data ---------- */

static yyjson_mut_val *member(yyjson_mut_val *object, const char *name)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    assert(value);
    return value;
}

static void add_members(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *const *texts,
    const char *const *flags,
    const char *const *ints
)
{
    for (int i = 0; texts && texts[i]; i++) {
        yyjson_mut_obj_add_strcpy(doc, object, texts[i], "");
    }
    for (int i = 0; flags && flags[i]; i++) {
        yyjson_mut_obj_add_bool(doc, object, flags[i], false);
    }
    for (int i = 0; ints && ints[i]; i++) {
        yyjson_mut_obj_add_int(doc, object, ints[i], 0);
    }
}

typedef enum
{
    ITEM_SECTION,
    ITEM_ROW,
    ITEM_CHIP,
    ITEM_OPTION,
    ITEM_SUGGESTION,
} item_kind;

static yyjson_mut_val *table_new(yyjson_mut_doc *doc, yyjson_mut_val *object)
{
    static const char *const TEXTS[] = { "label", "icon", "none", "key", NULL };
    yyjson_mut_val *table = yyjson_mut_obj_add_obj(doc, object, "table");
    add_members(doc, table, TEXTS, NULL, NULL);
    yyjson_mut_obj_add_arr(doc, table, "groups");
    return table;
}

static yyjson_mut_val *item_new(yyjson_mut_doc *doc, item_kind kind)
{
    static const char *const SECTION_TEXTS[] = { "key",      "title",   "kind",
                                                 "origin",   "within",  "none",
                                                 "addLabel", "addKind", NULL };
    static const char *const SECTION_FLAGS[] = { "open", "extract", NULL };
    static const char *const SECTION_INTS[] = { "rule", "addTarget", NULL };
    static const char *const ROW_TEXTS[] = { "id",          "label",   "tip",
                                             "about",       "control", "text",
                                             "placeholder", "number",  "unit",
                                             "step",        "min",     "max",
                                             "color",       "value",   NULL };
    static const char *const ROW_FLAGS[] = { "remove", "overridden", "integer", "slider", NULL };
    static const char *const ROW_INTS[] = { "precision", NULL };
    static const char *const CHIP_TEXTS[] = { "name", "label", NULL };
    static const char *const OPTION_FLAGS[] = { "pressed", "last", NULL };
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    switch (kind) {
        case ITEM_SECTION:
            add_members(doc, item, SECTION_TEXTS, SECTION_FLAGS, SECTION_INTS);
            yyjson_mut_obj_add_arr(doc, item, "rows");
            break;
        case ITEM_ROW:
            add_members(doc, item, ROW_TEXTS, ROW_FLAGS, ROW_INTS);
            yyjson_mut_obj_add_arr(doc, item, "options");
            yyjson_mut_obj_add_arr(doc, item, "chips");
            table_new(doc, item);
            break;
        case ITEM_CHIP: add_members(doc, item, CHIP_TEXTS, NULL, NULL); break;
        case ITEM_OPTION:
            add_members(doc, item, CHIP_TEXTS, OPTION_FLAGS, NULL);
            break;
        case ITEM_SUGGESTION:
            add_members(doc, item, CHIP_TEXTS, NULL, NULL);
            break;
    }
    return item;
}

void studio_details_add(yyjson_mut_doc *doc)
{
    static const char *const TEXTS[] = { "query", "hint", "note", NULL };
    static const char *const IDENTITY_TEXTS[] = { "icon", "title", "kind", "source", NULL };
    yyjson_mut_val *details = yyjson_mut_obj_add_obj(doc, yyjson_mut_doc_get_root(doc), "details");
    add_members(doc, details, TEXTS, NULL, NULL);
    add_members(doc, yyjson_mut_obj_add_obj(doc, details, "identity"), IDENTITY_TEXTS, NULL, NULL);
    yyjson_mut_obj_add_arr(doc, details, "sections");
}

void studio_details_popover_add(yyjson_mut_doc *doc)
{
    static const char *const TEXTS[] = { "title", "hint", "text", NULL };
    yyjson_mut_val *add = yyjson_mut_obj_add_obj(doc, yyjson_mut_doc_get_root(doc), "details_add");
    add_members(doc, add, TEXTS, NULL, NULL);
    yyjson_mut_obj_add_arr(doc, add, "suggestions");
}

/* ---------- Data, written in place ---------- */

/* The panel being written: the lists keep their items, so data-for keeps
 * their elements, and every write is in place. */
typedef struct
{
    studio *s;
    studio_designer *d;
    ui_buffer *b;
    yyjson_mut_doc *doc;
    yyjson_mut_val *sections;
    size_t count; /* sections written */
    bool changed;
} view;

static void put_text(view *v, yyjson_mut_val *object, const char *key, const char *text)
{
    v->changed |= ui_data_put_text(v->doc, object, key, text);
}

static void put_flag(view *v, yyjson_mut_val *object, const char *key, bool on)
{
    v->changed |= ui_data_put_bool(object, key, on);
}

static void put_int(view *v, yyjson_mut_val *object, const char *key, int number)
{
    v->changed |= ui_data_put_int(object, key, number);
}

/* The item at `i`, added when the list is shorter. */
static yyjson_mut_val *item_at(view *v, yyjson_mut_val *list, size_t i, item_kind kind)
{
    if (i < yyjson_mut_arr_size(list)) {
        return yyjson_mut_arr_get(list, i);
    }
    yyjson_mut_val *item = item_new(v->doc, kind);
    yyjson_mut_arr_append(list, item);
    v->changed = true;
    return item;
}

static void trim(view *v, yyjson_mut_val *list, size_t count)
{
    while (yyjson_mut_arr_size(list) > count) {
        yyjson_mut_arr_remove_last(list);
        v->changed = true;
    }
}

static bool is_shut(const studio_designer *d, const char *key)
{
    for (int i = 0; i < acount(d->shut); i++) {
        if (!strcmp(d->shut[i], key)) {
            return true;
        }
    }
    return false;
}

static void fold_panel(studio_designer *d, const char *key)
{
    for (int i = 0; i < acount(d->shut); i++) {
        if (!strcmp(d->shut[i], key)) {
            free(d->shut[i]);
            d->shut[i] = d->shut[acount(d->shut) - 1];
            asetlen(d->shut, acount(d->shut) - 1);
            d->shuts++;
            return;
        }
    }
    apush(d->shut, xstrdup(key));
    d->shuts++;
}

void designer_details_forget(studio_designer *d)
{
    for (int i = 0; i < acount(d->shut); i++) {
        free(d->shut[i]);
    }
    afree(d->shut);
}

/* A section being written: its rows go to `list`. */
typedef struct
{
    view *v;
    yyjson_mut_val *section, *list;
    size_t count;
} rows;

static rows begin_section(view *v, const char *key, const char *title, const char *kind)
{
    yyjson_mut_val *section = item_at(v, v->sections, v->count++, ITEM_SECTION);
    put_text(v, section, "key", key);
    put_text(v, section, "title", title);
    put_text(v, section, "kind", kind);
    put_flag(v, section, "open", !is_shut(v->d, key));
    static const char *const CLEARED[] = { "origin", "within", "none", "addLabel", "addKind" };
    for (size_t i = 0; i < sizeof CLEARED / sizeof *CLEARED; i++) {
        put_text(v, section, CLEARED[i], "");
    }
    put_flag(v, section, "extract", false);
    put_int(v, section, "rule", 0);
    put_int(v, section, "addTarget", 0);
    return (rows){ v, section, member(section, "rows"), 0 };
}

static void end_section(rows *r)
{
    trim(r->v, r->list, r->count);
}

static yyjson_mut_val *next_row(rows *r, const char *id, const char *label, const char *control)
{
    view *v = r->v;
    yyjson_mut_val *row = item_at(v, r->list, r->count++, ITEM_ROW);
    put_text(v, row, "id", id);
    put_text(v, row, "label", label);
    put_text(v, row, "control", control);
    put_flag(v, row, "remove", false);
    return row;
}

static void limit_text(char out[24], double limit)
{
    if (isinf(limit)) {
        snprintf(out, 24, "%s", limit < 0 ? "-inf" : "inf");
        return;
    }
    snprintf(out, 24, "%.9g", limit);
}

/* A keyword's choices: segments for up to three, else a list that holds only
 * the current one until it is opened (details.choices), so closed lists
 * build no elements. */
static void keyword_row(rows *r, yyjson_mut_val *row, const char *id, const value_shape *shape)
{
    view *v = r->v;
    const property_type *type = shape->type;
    put_text(v, row, "value", type->keywords[shape->keyword]);
    if (type->count <= 3) {
        yyjson_mut_val *options = member(row, "options");
        for (int i = 0; i < type->count; i++) {
            yyjson_mut_val *option = item_at(v, options, (size_t)i, ITEM_OPTION);
            put_text(v, option, "label", type->keywords[i]);
            put_flag(v, option, "pressed", i == shape->keyword);
            put_flag(v, option, "last", i == type->count - 1);
        }
        trim(v, options, (size_t)type->count);
        return;
    }
    bool open = !strcmp(v->d->choosing, id);
    char key[160];
    snprintf(key, sizeof key, "%s:%s", id, open ? "*" : type->keywords[shape->keyword]);
    yyjson_mut_val *table = member(row, "table");
    if (!strcmp(yyjson_mut_get_str(member(table, "key")), key)) {
        return;
    }
    put_text(v, table, "key", key);
    yyjson_mut_val *groups = member(table, "groups");
    yyjson_mut_arr_clear(groups);
    yyjson_mut_val *group = yyjson_mut_arr_add_obj(v->doc, groups);
    yyjson_mut_obj_add_str(v->doc, group, "label", "");
    yyjson_mut_val *list = yyjson_mut_obj_add_arr(v->doc, group, "options");
    for (int i = 0; i < type->count; i++) {
        if (!open && i != shape->keyword) {
            continue;
        }
        yyjson_mut_val *option = yyjson_mut_arr_add_obj(v->doc, list);
        yyjson_mut_obj_add_strcpy(v->doc, option, "value", type->keywords[i]);
        yyjson_mut_obj_add_strcpy(v->doc, option, "label", type->keywords[i]);
        yyjson_mut_obj_add_str(v->doc, option, "icon", "");
    }
    v->changed = true;
}

/* A property's row: its value in the control its type calls for, and a
 * button removing it. */
static void value_row(rows *r, const char *id, const char *property, const char *value)
{
    view *v = r->v;
    value_shape shape = shape_of(property, value);
    yyjson_mut_val *row = next_row(r, id, property, shape.control);
    put_flag(v, row, "remove", true);
    put_text(v, row, "text", value);
    put_text(v, row, "placeholder", "");
    if (!strcmp(shape.control, "number")) {
        char step[24], min[24], max[24];
        limit_text(step, shape.step);
        limit_text(min, shape.min);
        limit_text(max, shape.max);
        put_text(v, row, "number", shape.number);
        put_text(v, row, "unit", shape.unit);
        put_text(v, row, "step", step);
        put_text(v, row, "min", min);
        put_text(v, row, "max", max);
        put_int(v, row, "precision", shape.precision);
        put_flag(v, row, "integer", shape.integer);
        put_flag(v, row, "slider", shape.slider);
    } else if (!strcmp(shape.control, "color")) {
        put_text(v, row, "color", shape.color);
    } else if (shape.type) {
        keyword_row(r, row, id, &shape);
    }
}

/* ---------- Attributes ---------- */

static void text_row(
    rows *r,
    const char *id,
    const char *label,
    const char *value,
    const char *placeholder,
    bool remove
)
{
    view *v = r->v;
    yyjson_mut_val *row = next_row(r, id, label, "text");
    put_flag(v, row, "remove", remove);
    put_text(v, row, "text", value);
    put_text(v, row, "placeholder", placeholder);
}

static bool listed(const view *v, const char *name)
{
    return studio_matches(v->s->property_search, name);
}

/* A <use>'s widget, chosen among the project's ui assets. */
static void widget_row(rows *r, int element)
{
    view *v = r->v;
    if (!listed(v, "widget")) {
        return;
    }
    char value[128];
    written_value(v->d->rml, attribute_of(v->d->rml, element, "ui"), value, sizeof value);
    yyjson_mut_val *row = next_row(r, "widget", "widget", "asset");
    put_text(v, row, "text", value);
}

/* The text of an element holding text alone, without the space around it. */
static void element_text(const ui_source *rml, int element, char *out, size_t size)
{
    const ui_source_element *el = &rml->elements[element];
    int start = el->open_end, end = el->close_start;
    while (start < end && isspace((unsigned char)rml->text[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)rml->text[end - 1])) {
        end--;
    }
    snprintf(out, size, "%.*s", end - start, rml->text + start);
}

static void element_text_row(rows *r, int element)
{
    view *v = r->v;
    if (!listed(v, "text")) {
        return;
    }
    char text[512];
    element_text(v->d->rml, element, text, sizeof text);
    text_row(r, "text", "text", text, "None", false);
}

static void id_row(rows *r, int element)
{
    view *v = r->v;
    if (!listed(v, "id")) {
        return;
    }
    char value[128];
    written_value(v->d->rml, attribute_of(v->d->rml, element, "id"), value, sizeof value);
    text_row(r, "id", "id", value, "None", false);
}

/* Classes as chips, each removable. */
static void class_row(rows *r, int element)
{
    view *v = r->v;
    if (!listed(v, "class")) {
        return;
    }
    yyjson_mut_val *row = next_row(r, "class", "class", "chips");
    yyjson_mut_val *chips = member(row, "chips");
    size_t count = 0;
    const char *classes = v->d->rml->elements[element].classes;
    for (const char *c = classes ? classes : ""; *c;) {
        size_t n = strcspn(c, " \t\n");
        if (n) {
            char name[128];
            snprintf(name, sizeof name, "%.*s", (int)n, c);
            put_text(v, item_at(v, chips, count++, ITEM_CHIP), "name", name);
        }
        c += n + (c[n] != '\0');
    }
    trim(v, chips, count);
}

static void attribute_row(rows *r, int index, const char *name, const char *value)
{
    if (!listed(r->v, name)) {
        return;
    }
    char id[16];
    snprintf(id, sizeof id, "a%d", index);
    text_row(r, id, name, value, "", true);
}

static void attributes_section(view *v, int element)
{
    const ui_source *rml = v->d->rml;
    const ui_source_element *el = &rml->elements[element];
    rows r = begin_section(v, "attributes", "Attributes", "attributes");
    if (!strcmp(el->tag, "use")) {
        widget_row(&r, element);
    }
    if (ui_design_text_only(rml, element)) {
        element_text_row(&r, element);
    }
    id_row(&r, element);
    class_row(&r, element);
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        char name[128], value[512];
        attribute_name(rml, a, name, sizeof name);
        written_value(rml, a, value, sizeof value);
        if (!own_row(el->tag, name) && !ui_source_is_binding(name, (int)strlen(name))) {
            attribute_row(&r, i, name, value);
        }
    }
    put_text(v, r.section, "addLabel", "Add Attribute");
    put_text(v, r.section, "addKind", "attribute");
    put_int(v, r.section, "addTarget", element);
    end_section(&r);
}

/* Data bindings, as RmlUi's data-* attributes write them, in code. */
static void bindings_section(view *v, int element)
{
    const ui_source *rml = v->d->rml;
    const ui_source_element *el = &rml->elements[element];
    int count = 0;
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        count += ui_source_is_binding(rml->text + a->name_start, a->name_end - a->name_start);
    }
    if (!count) {
        return;
    }
    rows r = begin_section(v, "bindings", "Bindings", "bindings");
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        char name[128], value[512];
        attribute_name(rml, a, name, sizeof name);
        written_value(rml, a, value, sizeof value);
        if (ui_source_is_binding(name, (int)strlen(name))) {
            attribute_row(&r, i, name, value);
        }
    }
    end_section(&r);
}

/* ---------- Inline style ---------- */

typedef struct
{
    char property[128], value[256];
} declaration;

/* The inline style's declarations, in order, as `property: value` text. The
 * caller afrees the array. */
static declaration *style_declarations(const ui_source *rml, int element)
{
    char style[1024];
    written_value(rml, attribute_of(rml, element, "style"), style, sizeof style);
    declaration *found = NULL;
    for (const char *at = style; *at;) {
        size_t length = strcspn(at, ";");
        char text[512];
        snprintf(text, sizeof text, "%.*s", (int)length, at);
        at += length + (at[length] == ';');
        declaration d;
        split(text, ':', d.property, sizeof d.property, d.value, sizeof d.value);
        const char *name = d.property;
        while (isspace((unsigned char)*name)) {
            name++;
        }
        if (name[0]) {
            memmove(d.property, name, strlen(name) + 1);
            apush(found, d);
        }
    }
    return found;
}

static void style_section(view *v, int element)
{
    declaration *styles = style_declarations(v->d->rml, element);
    rows r = begin_section(v, "style", "Inline Style", "style");
    for (int i = 0; i < acount(styles); i++) {
        if (listed(v, styles[i].property)) {
            char id[sizeof styles[i].property + 4];
            snprintf(id, sizeof id, "s:%s", styles[i].property);
            value_row(&r, id, styles[i].property, styles[i].value);
        }
    }
    put_text(v, r.section, "none", acount(styles) ? "" : "No inline style");
    put_text(v, r.section, "addLabel", "Add Property");
    put_text(v, r.section, "addKind", "style");
    put_int(v, r.section, "addTarget", element);
    put_flag(v, r.section, "extract", acount(styles) > 0);
    end_section(&r);
    afree(styles);
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

static void declaration_row(rows *r, int rule, int index)
{
    const ui_source *rcss = r->v->d->rcss;
    const ui_source_declaration *d = &rcss->rules[rule].declarations[index];
    char property[128], value[256], id[24];
    snprintf(property, sizeof property, "%.*s", d->property_end - d->property_start, rcss->text + d->property_start);
    snprintf(value, sizeof value, "%.*s", d->value_end - d->value_start, rcss->text + d->value_start);
    if (!listed(r->v, property)) {
        return;
    }
    snprintf(id, sizeof id, "d%d.%d", rule, index);
    value_row(r, id, property, value);
}

/* A rule by its selector, its file and line opening it in the Text Editor. */
static void rule_section(view *v, int rule)
{
    const ui_source_rule *rl = &v->d->rcss->rules[rule];
    char key[16], origin[160];
    snprintf(key, sizeof key, "rule%d", rule);
    snprintf(origin, sizeof origin, "%s:%d", content_basename(v->b->files[UI_PART_RCSS]), rl->line);
    rows r = begin_section(v, key, rl->selector, "rule");
    put_text(v, r.section, "origin", origin);
    put_text(
        v,
        r.section,
        "within",
        rl->parent >= 0 ? v->d->rcss->rules[rl->parent].selector : ""
    );
    put_int(v, r.section, "rule", rule);
    for (int i = 0; i < acount(rl->declarations); i++) {
        declaration_row(&r, rule, i);
    }
    put_text(v, r.section, "addLabel", "Add Property");
    put_text(v, r.section, "addKind", "declaration");
    put_int(v, r.section, "addTarget", rule);
    end_section(&r);
}

/* The document's rules that match the element, or all of them for -1. */
static int rules(view *v, int element)
{
    const ui_source *rcss = v->d->rcss;
    int count = 0;
    for (int r = 0; r < acount(rcss->rules); r++) {
        const char *selector = rcss->rules[r].selector;
        if (selector[0] != '@' &&
            (element < 0 || rule_matches(v->d->rml, selector, element))) {
            rule_section(v, r);
            count++;
        }
    }
    return count;
}

/* ---------- The panel ---------- */

/* The element's glyph, name and kind, with its source line as a link to
 * the Text Editor when it has one. */
static void identity(
    view *v,
    yyjson_mut_val *details,
    uint32_t icon,
    const char *title,
    const char *kind,
    const char *source
)
{
    char glyph[5];
    studio_glyph(glyph, icon);
    yyjson_mut_val *identity = member(details, "identity");
    put_text(v, identity, "icon", glyph);
    put_text(v, identity, "title", title);
    put_text(v, identity, "kind", kind);
    put_text(v, identity, "source", source);
}

static void element_view(view *v, yyjson_mut_val *details, int element)
{
    const ui_source *rml = v->d->rml;
    const ui_source_element *el = &rml->elements[element];
    char name[96], title[256], kind[80], source[160];
    designer_element_name(rml, element, name, sizeof name);
    snprintf(title, sizeof title, "%s%s%s", name, el->id ? "#" : "", el->id ? el->id : "");
    if (strcmp(el->tag, "use")) {
        snprintf(kind, sizeof kind, "<%s>", el->tag);
    } else {
        snprintf(kind, sizeof kind, "Widget");
    }
    snprintf(source, sizeof source, "%s:%d", v->d->asset, el->line);
    identity(v, details, designer_element_icon(rml, element), title, kind, source);
    put_text(v, details, "hint", "");
    attributes_section(v, element);
    bindings_section(v, element);
    style_section(v, element);
    bool none = !rules(v, element) && !v->s->property_search[0];
    put_text(v, details, "note", none ? "No rule of the document's RCSS matches it" : "");
}

static void document_view(view *v, yyjson_mut_val *details)
{
    char kind[64];
    snprintf(
        kind,
        sizeof kind,
        "UI document \xc2\xb7 %d elements",
        acount(v->d->rml->elements)
    );
    identity(v, details, KIT_ICON_LAYOUT, v->d->asset, kind, "");
    put_text(v, details, "hint", "Select an element to edit it. The document's rules:");
    put_text(v, details, "note", rules(v, -1) ? "" : "The document has no RCSS rules");
}

bool studio_details_sync(studio *s, const studio_panel_sync *at)
{
    ui_buffer *b;
    studio_designer *d = at->preview ? NULL : designer_state(s, &b);
    if (!d) {
        return false;
    }
    yyjson_mut_val *details = at->variable;
    yyjson_mut_val *query = member(details, "query");
    if (at->wrote && strcmp(yyjson_mut_get_str(query), s->property_search)) {
        snprintf(s->property_search, sizeof s->property_search, "%s", yyjson_mut_get_str(query));
    }
    struct designer_details_seen *seen = &d->details_seen;
    if (seen->built && seen->parsed == d->parsed && seen->selected == d->selected &&
        seen->shuts == d->shuts && !strcmp(seen->search, s->property_search) &&
        !strcmp(seen->choosing, d->choosing)) {
        return false;
    }
    *seen = (struct designer_details_seen){ .built = true,
                                            .parsed = d->parsed,
                                            .selected = d->selected,
                                            .shuts = d->shuts };
    snprintf(seen->search, sizeof seen->search, "%s", s->property_search);
    snprintf(seen->choosing, sizeof seen->choosing, "%s", d->choosing);
    view v = { .s = s, .d = d, .b = b, .doc = at->doc, .sections = member(details, "sections") };
    put_text(&v, details, "query", s->property_search);
    if (d->selected >= 0) {
        element_view(&v, details, d->selected);
    } else {
        document_view(&v, details);
    }
    trim(&v, v.sections, v.count);
    return v.changed;
}

/* ---------- The Add popover ---------- */

static const char *const ADD_NAMES[] = { "class", "attribute", "style", "declaration", "extract" };

static rml_element *by_id(studio *s, const char *id)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int i = 0; i < rml_context_document_count(context); i++) {
        rml_element *found = rml_element_by_id(rml_context_document(context, i), id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* Frames an Add popover's field keeps asking for the focus, since the
 * popover places itself after it opens. */
#define FOCUS_FRAMES 30

bool studio_details_popover_sync(studio *s, const studio_panel_sync *at)
{
    ui_buffer *b;
    studio_designer *d = at->preview ? NULL : designer_state(s, &b);
    if (!d) {
        return false;
    }
    yyjson_mut_val *add = at->variable;
    yyjson_mut_val *typed = member(add, "text");
    if (at->wrote && strcmp(yyjson_mut_get_str(typed), d->add.text)) {
        snprintf(d->add.text, sizeof d->add.text, "%s", yyjson_mut_get_str(typed));
    }
    if (d->add.focus > 0) {
        rml_element *field = by_id(s, "details-add-field");
        if (field && rml_element_is_visible(field, true) &&
            rml_element_focus(field, true)) {
            rml_element_select(field);
            d->add.focus = 0;
        } else {
            d->add.focus--;
            s->ui_schedule.wake = true;
        }
    }
    bool changed = ui_data_put_text(at->doc, add, "title", ADD_TITLES[d->add.kind]);
    changed |= ui_data_put_text(at->doc, add, "hint", ADD_HINTS[d->add.kind]);
    changed |= ui_data_put_text(at->doc, add, "text", d->add.text);
    struct designer_add_seen *seen = &d->add_seen;
    if (seen->built && seen->parsed == d->parsed && seen->kind == d->add.kind &&
        seen->target == d->add.target && !strcmp(seen->text, d->add.text)) {
        return changed;
    }
    *seen = (struct designer_add_seen){ .built = true,
                                        .parsed = d->parsed,
                                        .kind = d->add.kind,
                                        .target = d->add.target };
    snprintf(seen->text, sizeof seen->text, "%s", d->add.text);
    details x = { .s = s, .d = d, .b = b };
    bool element_target = d->add.kind != ADD_DECLARATION;
    int bound = element_target ? acount(d->rml->elements) : acount(d->rcss->rules);
    char **choices = d->add.target >= 0 && d->add.target < bound
                       ? suggestions(&x, d->add.target, (add_kind)d->add.kind)
                       : NULL;
    yyjson_mut_val *list = member(add, "suggestions");
    view v = { .s = s, .d = d, .b = b, .doc = at->doc };
    for (int i = 0; i < acount(choices); i++) {
        put_text(&v, item_at(&v, list, (size_t)i, ITEM_SUGGESTION), "label", choices[i]);
        free(choices[i]);
    }
    trim(&v, list, (size_t)acount(choices));
    afree(choices);
    return v.changed || changed;
}

/* ---------- Intents ---------- */

/* What a row's id names. */
typedef enum
{
    REF_NONE,
    REF_ATTRIBUTE,
    REF_ID,
    REF_TEXT,
    REF_WIDGET,
    REF_STYLE,
    REF_DECLARATION,
} ref_kind;

typedef struct
{
    ref_kind kind;
    int index, rule;
    char property[128];
} row_ref;

static bool parse_ref(const char *id, row_ref *r)
{
    *r = (row_ref){ 0 };
    if (!strcmp(id, "id")) {
        r->kind = REF_ID;
    } else if (!strcmp(id, "text")) {
        r->kind = REF_TEXT;
    } else if (!strcmp(id, "widget")) {
        r->kind = REF_WIDGET;
    } else if (id[0] == 'a' && isdigit((unsigned char)id[1])) {
        r->kind = REF_ATTRIBUTE;
        r->index = atoi(id + 1);
    } else if (!strncmp(id, "s:", 2)) {
        r->kind = REF_STYLE;
        snprintf(r->property, sizeof r->property, "%s", id + 2);
    } else if (id[0] == 'd' && isdigit((unsigned char)id[1])) {
        r->kind = REF_DECLARATION;
        r->kind = sscanf(id + 1, "%d.%d", &r->rule, &r->index) == 2 ? REF_DECLARATION
                                                                    : REF_NONE;
    }
    return r->kind != REF_NONE;
}

static bool init(studio *s, details *x)
{
    x->s = s;
    x->d = designer_state(s, &x->b);
    return x->d != NULL;
}

/* The declaration of a rule `r` names, or NULL. */
static const ui_source_declaration *declaration_of(const details *x, const row_ref *r)
{
    const ui_source_rule *rules_ = x->d->rcss->rules;
    if (r->rule < 0 || r->rule >= acount(x->d->rcss->rules) || r->index < 0 ||
        r->index >= acount(rules_[r->rule].declarations)) {
        return NULL;
    }
    return &rules_[r->rule].declarations[r->index];
}

/* The text a row's value is in the document now; false when it names
 * nothing there. */
static bool ref_text(const details *x, const row_ref *r, char *out, size_t size)
{
    const ui_source *rml = x->d->rml;
    int element = x->d->selected;
    out[0] = '\0';
    if (r->kind == REF_DECLARATION) {
        const ui_source_declaration *d = declaration_of(x, r);
        snprintf(
            out,
            size,
            "%.*s",
            d ? d->value_end - d->value_start : 0,
            d ? x->d->rcss->text + d->value_start : ""
        );
        return d != NULL;
    }
    if (element < 0 || element >= acount(rml->elements)) {
        return false;
    }
    const ui_source_element *el = &rml->elements[element];
    switch (r->kind) {
        case REF_ATTRIBUTE:
            if (r->index >= acount(el->attributes)) {
                return false;
            }
            written_value(rml, &el->attributes[r->index], out, size);
            return true;
        case REF_ID:
            written_value(rml, attribute_of(rml, element, "id"), out, size);
            return true;
        case REF_WIDGET:
            written_value(rml, attribute_of(rml, element, "ui"), out, size);
            return true;
        case REF_TEXT: element_text(rml, element, out, size); return true;
        case REF_STYLE: {
            declaration *styles = style_declarations(rml, element);
            bool found = false;
            for (int i = 0; i < acount(styles); i++) {
                if (!strcmp(styles[i].property, r->property)) {
                    snprintf(out, size, "%s", styles[i].value);
                    found = true;
                }
            }
            afree(styles);
            return found;
        }
        default: return false;
    }
}

static void warn(details *x, bool ok, const char *error, const char *failed)
{
    if (!ok) {
        designer_warn(x->s, failed, error);
    }
}

/* A scrub's or a picker's changes to one value join one undo step while
 * nothing else changed the text between them. */
typedef struct
{
    int element; /* -1 for a rule */
    int rule, declaration;
    char property[128];
} value_target;

static void gesture_key(const value_target *t, char *key, size_t size)
{
    snprintf(key, size, "%d:%d:%d:%s", t->element, t->rule, t->declaration, t->property);
}

static void write_value(details *x, const value_target *t, const char *text, bool committed)
{
    studio_designer *d = x->d;
    char key[sizeof d->gesture.key];
    gesture_key(t, key, sizeof key);
    bool mine = !strcmp(d->gesture.key, key);
    bool join = d->gesture.active && mine && d->gesture.revision == x->b->revision;
    unsigned before = x->b->revision;
    char error[192] = "";
    bool ok = t->element >= 0
                ? ui_design_set_style(x->b, t->element, t->property, text, join, error, sizeof error)
                : ui_design_set_declaration(x->b, t->rule, t->declaration, text, join, error, sizeof error);
    d->gesture.active = ok && !committed && (join || x->b->revision != before);
    snprintf(d->gesture.key, sizeof d->gesture.key, "%s", key);
    d->gesture.revision = x->b->revision;
    warn(x, ok, error, "Could not change the value");
}

/* The target a style or declaration row names, with its property. */
static bool value_target_of(const details *x, const row_ref *r, value_target *t)
{
    if (r->kind == REF_STYLE && x->d->selected >= 0) {
        *t = (value_target){ x->d->selected, -1, -1, "" };
        snprintf(t->property, sizeof t->property, "%s", r->property);
        return true;
    }
    const ui_source_declaration *decl = r->kind == REF_DECLARATION
                                          ? declaration_of(x, r)
                                          : NULL;
    if (!decl) {
        return false;
    }
    *t = (value_target){ -1, r->rule, r->index, "" };
    snprintf(
        t->property,
        sizeof t->property,
        "%.*s",
        decl->property_end - decl->property_start,
        x->d->rcss->text + decl->property_start
    );
    return true;
}

/* A <use>'s widget, chosen among the project's ui assets; none is no
 * widget, which a <use> cannot do without. */
static void choose_widget(details *x, const char *name)
{
    char current[128], error[192] = "";
    int element = x->d->selected;
    if (element < 0 || !name[0]) {
        return;
    }
    written_value(x->d->rml, attribute_of(x->d->rml, element, "ui"), current, sizeof current);
    if (strcmp(name, current)) {
        bool ok = ui_design_set_attribute(x->b, element, "ui", name, error, sizeof error);
        warn(x, ok, error, "Could not change the widget");
    }
}

/* A control's input or change: emit('details.input', row, value) and
 * emit('details.change', row, value[, canceled]). Input writes as the
 * control moves; change ends the gesture, and Escape puts the text back by
 * undoing it. */
static void gesture(studio *s, const ui_activation *a, bool active)
{
    details x;
    row_ref ref;
    value_target t;
    char current[256], text[256];
    if (a->argc < 2 || !init(s, &x) || !parse_ref(a->args[0], &ref)) {
        return;
    }
    if (ref.kind == REF_WIDGET) {
        choose_widget(&x, a->args[1]);
        return;
    }
    if (!value_target_of(&x, &ref, &t) || !ref_text(&x, &ref, current, sizeof current)) {
        return;
    }
    studio_designer *d = x.d;
    char key[sizeof d->gesture.key];
    gesture_key(&t, key, sizeof key);
    bool mine = d->gesture.active && !strcmp(d->gesture.key, key);
    bool canceled = !active && a->argc > 2 && !strcmp(a->args[2], "1");
    if (canceled) {
        if (mine && d->gesture.revision == x.b->revision && ui_buffer_can_undo(x.b)) {
            ui_buffer_undo(x.b);
        }
        d->gesture.active = false;
        return;
    }
    if (!value_text(t.property, current, a->args[1], text, sizeof text)) {
        return;
    }
    if (!strcmp(text, current)) {
        d->gesture.active &= !(mine && !active);
        return;
    }
    write_value(&x, &t, text, !active);
}

/* A text field puts the document's text back and lets go. */
static void settle(const details *x, rml_element *field, const row_ref *ref)
{
    char text[512];
    if (field && ref_text(x, ref, text, sizeof text)) {
        rml_element_set_value(field, text);
    }
    if (field) {
        rml_element_blur(field);
    }
}

/* A text field's commit: emit('details.text', row); the field holds the
 * text, which can be longer than an argument. */
static void commit_text(studio *s, const ui_activation *a)
{
    details x;
    row_ref ref;
    rml_element *field = a->anchor ? rml_element_ref_get(a->anchor) : NULL;
    if (!field || !a->argc || !init(s, &x) || !parse_ref(a->args[0], &ref)) {
        return;
    }
    char text[4096], current[4096], error[192] = "";
    rml_element_value(field, text, sizeof text);
    if (!ref_text(&x, &ref, current, sizeof current) || !strcmp(text, current)) {
        return;
    }
    int element = x.d->selected;
    bool ok = false;
    switch (ref.kind) {
        case REF_ATTRIBUTE: {
            char name[128];
            attribute_name(x.d->rml, &x.d->rml->elements[element].attributes[ref.index], name, sizeof name);
            ok = ui_design_set_attribute(x.b, element, name, text, error, sizeof error);
            warn(&x, ok, error, "Could not change the attribute");
            break;
        }
        case REF_ID:
            ok = ui_design_set_attribute(x.b, element, "id", text[0] ? text : NULL, error, sizeof error);
            warn(&x, ok, error, "Could not change the id");
            break;
        case REF_TEXT:
            ok = ui_design_set_text(x.b, element, text, error, sizeof error);
            warn(&x, ok, error, "Could not change the text");
            break;
        case REF_STYLE:
        case REF_DECLARATION: {
            value_target t;
            if (value_target_of(&x, &ref, &t)) {
                write_value(&x, &t, text, true);
                ok = true;
            }
            break;
        }
        default: break;
    }
    if (!ok) {
        settle(&x, field, &ref);
    }
}

/* Removes a row: an attribute, an inline property or a declaration. */
static void remove_row(studio *s, const ui_activation *a)
{
    details x;
    row_ref ref;
    char error[192] = "", name[128];
    if (!a->argc || !init(s, &x) || !parse_ref(a->args[0], &ref)) {
        return;
    }
    studio_designer *d = x.d;
    int element = d->selected;
    switch (ref.kind) {
        case REF_ATTRIBUTE:
            if (element >= 0 &&
                ref.index < acount(d->rml->elements[element].attributes)) {
                attribute_name(d->rml, &d->rml->elements[element].attributes[ref.index], name, sizeof name);
                bool ok = ui_design_set_attribute(x.b, element, name, NULL, error, sizeof error);
                warn(&x, ok, error, "Could not change the attribute");
            }
            break;
        case REF_STYLE: {
            bool ok = ui_design_set_style(x.b, element, ref.property, NULL, false, error, sizeof error);
            warn(&x, ok, error, "Could not remove the property");
            break;
        }
        case REF_DECLARATION:
            if (declaration_of(&x, &ref)) {
                bool ok = ui_design_remove_declaration(x.b, ref.rule, ref.index, error, sizeof error);
                warn(&x, ok, error, "Could not remove the declaration");
            }
            break;
        default: break;
    }
}

/* The Add popover for `kind` and `target` (an element, or a rule for a
 * declaration), under the button that asked. */
static void open_add(studio *s, const ui_activation *a)
{
    details x;
    if (a->argc != 2 || !init(s, &x)) {
        return;
    }
    int kind = -1;
    for (size_t i = 0; i < sizeof ADD_NAMES / sizeof *ADD_NAMES; i++) {
        kind = !strcmp(ADD_NAMES[i], a->args[0]) ? (int)i : kind;
    }
    rml_element *button = a->anchor ? rml_element_ref_get(a->anchor) : NULL;
    rml_element *popover = button
                             ? rml_element_by_id(rml_element_owner_document(button), "details-add")
                             : NULL;
    if (kind < 0 || !popover) {
        return;
    }
    x.d->add.kind = kind;
    x.d->add.target = atoi(a->args[1]);
    x.d->add.text[0] = '\0';
    x.d->add.focus = FOCUS_FRAMES;
    studio_ui_resync(s, "details_add");
    popover_show(popover, &(popover_anchor){ .side = POPOVER_BELOW, .element = button, .trigger = button });
}

/* Adds what the popover's text names to `target`: the element, or a
 * declaration's rule. */
static bool add_value(details *x, int target, add_kind kind, const char *text)
{
    char name[128], value[256], error[192] = "";
    bool element_target = kind != ADD_DECLARATION;
    if (target < 0 || target >= (element_target ? acount(x->d->rml->elements)
                                                : acount(x->d->rcss->rules))) {
        return false;
    }
    bool ok = false;
    switch (kind) {
        case ADD_CLASS:
            ok = ui_design_class(x->b, target, text, true, error, sizeof error);
            break;
        case ADD_ATTRIBUTE:
            split(text, '=', name, sizeof name, value, sizeof value);
            ok = ui_design_set_attribute(x->b, target, name, value, error, sizeof error);
            break;
        case ADD_STYLE:
        case ADD_DECLARATION:
            split(text, ':', name, sizeof name, value, sizeof value);
            ok = kind == ADD_STYLE ? ui_design_set_style(
                                         x->b,
                                         target,
                                         name,
                                         value[0] ? value : default_value(name),
                                         false,
                                         error,
                                         sizeof error
                                     )
                                   : ui_design_add_declaration(
                                         x->b,
                                         target,
                                         name,
                                         value[0] ? value : default_value(name),
                                         error,
                                         sizeof error
                                     );
            break;
        case ADD_EXTRACT:
            ok = ui_design_extract_class(x->b, target, text, error, sizeof error);
            break;
    }
    warn(x, ok, error, ADD_TITLES[kind]);
    return ok;
}

void studio_details_action(studio *s, const char *action, const ui_activation *a)
{
    details x;
    if (!init(s, &x)) {
        return;
    }
    const char *arg = a->argc ? a->args[0] : "";
    if (!strcmp(action, "input") || !strcmp(action, "change")) {
        gesture(s, a, !strcmp(action, "input"));
    } else if (!strcmp(action, "text")) {
        commit_text(s, a);
    } else if (!strcmp(action, "remove")) {
        remove_row(s, a);
    } else if (!strcmp(action, "removeClass") && x.d->selected >= 0) {
        char error[192] = "";
        bool ok = ui_design_class(x.b, x.d->selected, arg, false, error, sizeof error);
        warn(&x, ok, error, "Could not remove the class");
    } else if (!strcmp(action, "add")) {
        open_add(s, a);
    } else if (!strcmp(action, "addPick") || !strcmp(action, "addEnter")) {
        rml_element *field = !strcmp(action, "addEnter") && a->anchor
                               ? rml_element_ref_get(a->anchor)
                               : NULL;
        char typed[192] = "";
        if (field) {
            rml_element_value(field, typed, sizeof typed);
        }
        const char *text = field ? typed : arg;
        if (text[0] &&
            add_value(&x, x.d->add.target, (add_kind)x.d->add.kind, text) && field) {
            rml_element *popover = rml_element_by_id(rml_element_owner_document(field), "details-add");
            if (popover) {
                popover_hide(popover);
            }
        }
    } else if (!strcmp(action, "fold")) {
        fold_panel(x.d, arg);
    } else if (!strcmp(action, "source") && x.d->selected >= 0) {
        studio_open_ui_source(s, x.d->path, x.d->rml->elements[x.d->selected].line);
    } else if (
        !strcmp(action, "openRule") && a->argc && atoi(arg) < acount(x.d->rcss->rules)
    ) {
        studio_open_ui_part(
            s,
            x.d->path,
            UI_PART_RCSS,
            x.d->rcss->rules[atoi(arg)].line
        );
    } else if (!strcmp(action, "choices")) {
        snprintf(x.d->choosing, sizeof x.d->choosing, "%s", arg);
    } else if (!strcmp(action, "cancel")) {
        row_ref ref;
        rml_element *field = a->anchor ? rml_element_ref_get(a->anchor) : NULL;
        if (parse_ref(a->id, &ref)) {
            settle(&x, field, &ref);
        }
    }
}

/* ---------- Panel ---------- */

void studio_designer_details_header(studio *s, int area)
{
    rml_element *host = studio_ui_area_controls(s, area, "details", "DetailsHeader");
    rml_element *field = host && s->find_request == STUDIO_EDITOR_PROPERTIES + 1
                           ? rml_element_query_selector(host, "#details-search")
                           : NULL;
    if (field && rml_element_focus(field, true)) {
        rml_element_select(field);
        s->find_request = 0;
    }
}

void studio_designer_details(studio *s, int area)
{
    studio_ui_area(s, area, "Details");
}
