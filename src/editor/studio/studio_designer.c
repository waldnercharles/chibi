/* The visual designer (workstream H). While a ui asset's UI document is
 * active, the Outliner is its Hierarchy, as Unity's UI Builder and Unreal's
 * UMG Designer list a widget's tree: rows select and fold, drag to reorder
 * and reparent, or move with the keyboard as dnd-kit's keyboard sensor
 * moves them (workstream Q), and a menu adds, duplicates, wraps and
 * deletes. The Canvas in the Scene area outlines the selection and the
 * element under the pointer, selects with a click and takes Library drops,
 * marking where between siblings they land. Selecting in the
 * Hierarchy, in the Canvas or with the Text Editor's cursor selects in all
 * of them. Every change is a text edit of the document
 * (editor/documents/ui_design.h): one undo step that keeps the author's
 * formatting. Details and the Library are studio_designer_details.c and
 * studio_designer_library.c. */
#include "editor/studio/studio_designer_internal.h"
#include "support/array.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The share of a Hierarchy row, at its top and at its bottom, where a drop
 * goes before or after the element rather than inside it, as dnd-kit's
 * sortable trees split a row. */
#define DROP_EDGE 0.25f
/* Characters of an element's text its row shows. */
#define EXCERPT 32

static const char *const PLACE_NAMES[] = { "inside", "before", "after" };

/* ---------- The document ---------- */

static void forget(studio_designer *d)
{
    ui_source_free(d->rml);
    ui_source_free(d->rcss);
    for (int i = 0; i < acount(d->folded); i++) {
        free(d->folded[i]);
    }
    afree(d->folded);
}

void studio_designer_free(studio *s)
{
    if (!s->designer) {
        return;
    }
    forget(s->designer);
    free(s->designer);
    s->designer = NULL;
}

bool studio_designer_active(studio *s)
{
    ui_buffer *b = studio_ui_buffer(s);
    return b && ui_buffer_text(b, UI_PART_RML);
}

/* Another document starts with nothing selected or folded. Its asset's
 * name, as data-src gives it, is its file's. */
static void open_document(studio_designer *d, const char *path)
{
    forget(d);
    *d = (studio_designer){ .selected = -1, .hovered = -1, .reveal_cursor = -1 };
    snprintf(d->path, sizeof d->path, "%s", path);
    const char *base = content_basename(path), *dot = strrchr(base, '.');
    snprintf(d->asset, sizeof d->asset, "%.*s", dot ? (int)(dot - base) : (int)strlen(base), base);
}

studio_designer *designer_state(studio *s, ui_buffer **out)
{
    ui_buffer *b = studio_ui_buffer(s);
    if (!b || !ui_buffer_text(b, UI_PART_RML)) {
        return NULL;
    }
    if (!s->designer) {
        s->designer = xcalloc(1, sizeof *s->designer);
    }
    studio_designer *d = s->designer;
    if (strcmp(d->path, b->path)) {
        open_document(d, b->path);
    }
    if (!d->rml || d->parsed != b->revision) {
        /* The selection follows its element through an undo or typing. */
        ui_source *before = d->rml;
        ui_source_free(d->rcss);
        d->rml = ui_design_parse(b, UI_PART_RML);
        d->rcss = ui_design_parse(b, UI_PART_RCSS);
        d->parsed = b->revision;
        d->selected = before ? ui_design_follow(before, d->rml, d->selected) : -1;
        ui_source_free(before);
    }
    *out = b;
    return d;
}

void designer_select(studio *s, int element)
{
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    assert(d && element < acount(d->rml->elements));
    d->selected = element;
    d->reveal_row = true;
    if (element >= 0) {
        d->reveal_cursor = d->rml->elements[element].start;
        b->part = UI_PART_RML;
    }
}

void designer_warn(studio *s, const char *title, const char *error)
{
    kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_WARNING, .title = title, .description = error });
    studio_record(s, KIT_TOAST_WARNING, error);
}

/* ---------- Elements ---------- */

void designer_element_name(const ui_source *rml, int element, char *out, size_t size)
{
    const ui_source_element *el = &rml->elements[element];
    char *widget = strcmp(el->tag, "use")
                     ? NULL
                     : ui_source_attribute_value(rml, element, "ui");
    snprintf(out, size, "%s", widget && widget[0] ? widget : el->tag);
    free(widget);
}

typedef struct
{
    const char *tag;
    uint32_t icon;
} tag_icon;

static const tag_icon TAG_ICONS[] = {
    { "use", KIT_ICON_WIDGET },
    { "slot", KIT_ICON_SLOT },
    { "button", KIT_ICON_BUTTON },
    { "select", KIT_ICON_INPUT },
    { "textarea", KIT_ICON_INPUT },
    { "img", KIT_ICON_IMAGE },
    { "progress", KIT_ICON_PROGRESS },
    { "span", KIT_ICON_TEXT },
    { "p", KIT_ICON_TEXT },
    { "label", KIT_ICON_TEXT },
    { "h1", KIT_ICON_TEXT },
    { "h2", KIT_ICON_TEXT },
    { "h3", KIT_ICON_TEXT },
    { "morph-text", KIT_ICON_TEXT },
    { "number-flow", KIT_ICON_NUMBER },
};

/* An input by its type: a checkbox or radio toggles, a range slides. */
static uint32_t input_icon(const ui_source *rml, int element)
{
    char *type = ui_source_attribute_value(rml, element, "type");
    uint32_t icon = !type ? KIT_ICON_INPUT
                  : !strcmp(type, "checkbox") || !strcmp(type, "radio") ? KIT_ICON_CHECKBOX
                  : !strcmp(type, "range") ? KIT_ICON_SLIDER
                                           : KIT_ICON_INPUT;
    free(type);
    return icon;
}

uint32_t designer_element_icon(const ui_source *rml, int element)
{
    const char *tag = rml->elements[element].tag;
    if (!strcmp(tag, "input")) {
        return input_icon(rml, element);
    }
    for (size_t i = 0; i < sizeof TAG_ICONS / sizeof *TAG_ICONS; i++) {
        if (!strcmp(TAG_ICONS[i].tag, tag)) {
            return TAG_ICONS[i].icon;
        }
    }
    return KIT_ICON_ELEMENT;
}

static bool has_children(const ui_source *rml, int element)
{
    return element + 1 < acount(rml->elements) &&
           rml->elements[element + 1].parent == element;
}

static int depth_of(const ui_source *rml, int element)
{
    int depth = 0;
    for (int i = rml->elements[element].parent; i >= 0; i = rml->elements[i].parent) {
        depth++;
    }
    return depth;
}

static bool has_bindings(const ui_source *rml, int element)
{
    const ui_source_element *el = &rml->elements[element];
    for (int i = 0; i < acount(el->attributes); i++) {
        const ui_source_attribute *a = &el->attributes[i];
        if (ui_source_is_binding(rml->text + a->name_start, a->name_end - a->name_start)) {
            return true;
        }
    }
    return false;
}

static void append(char *out, size_t size, const char *text, int length)
{
    size_t used = strlen(out);
    if (used + 1 < size) {
        snprintf(out + used, size - used, "%.*s", length, text);
    }
}

/* The start of an element's text, its spaces collapsed, in quotes. */
static void append_excerpt(char *out, size_t size, const ui_source *rml, int element)
{
    const ui_source_element *el = &rml->elements[element];
    char excerpt[EXCERPT + 8] = "";
    int length = 0;
    bool space = false;
    for (int i = el->open_end; i < el->close_start && length < EXCERPT; i++) {
        char c = rml->text[i];
        bool blank = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!blank && space && length) {
            excerpt[length++] = ' ';
        }
        space = blank;
        if (!blank) {
            excerpt[length++] = c;
        }
    }
    excerpt[length] = '\0';
    if (length) {
        append(out, size, " \"", 2);
        append(out, size, excerpt, length);
        append(out, size, length == EXCERPT ? "\xe2\x80\xa6\"" : "\"", length == EXCERPT ? 4 : 1);
    }
}

/* "#id.a.b", then the text of an element holding text alone. */
static void element_meta(const ui_source *rml, int element, char *out, size_t size)
{
    const ui_source_element *el = &rml->elements[element];
    out[0] = '\0';
    if (el->id) {
        append(out, size, "#", 1);
        append(out, size, el->id, (int)strlen(el->id));
    }
    for (const char *c = el->classes ? el->classes : ""; *c;) {
        size_t n = strcspn(c, " \t\n");
        if (n) {
            append(out, size, ".", 1);
            append(out, size, c, (int)n);
        }
        c += n + (c[n] != '\0');
    }
    if (ui_design_text_only(rml, element)) {
        append_excerpt(out, size, rml, element);
    }
}

/* ---------- Changes ---------- */

/* After a structural change: selects `selected` and reports it with
 * `verb`, or warns why the change failed. */
static void changed(studio *s, int selected, const char *error, const char *failed, const char *verb)
{
    if (error[0]) {
        designer_warn(s, failed, error);
        return;
    }
    designer_select(s, selected);
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    char name[96] = "", message[160];
    if (selected >= 0) {
        designer_element_name(d->rml, selected, name, sizeof name);
    }
    snprintf(message, sizeof message, "%s %s", verb, name);
    studio_report(s, message);
}

void designer_add(studio *s, int target, const char *markup, const char *label)
{
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    ui_source_place place = target < 0 || ui_design_container(d->rml, target)
                              ? UI_SOURCE_INSIDE
                              : UI_SOURCE_AFTER;
    char error[192] = "", failed[128];
    snprintf(failed, sizeof failed, "Could not add %s", label);
    changed(s, ui_design_insert(b, target, place, markup, error, sizeof error), error, failed, "Added");
}

static void duplicate(studio *s, studio_designer *d, ui_buffer *b)
{
    char error[192] = "";
    if (d->selected >= 0) {
        int copy = ui_design_duplicate(b, d->selected, error, sizeof error);
        changed(s, copy, error, "Could not duplicate", "Duplicated");
    }
}

static void wrap(studio *s, studio_designer *d, ui_buffer *b)
{
    char error[192] = "";
    if (d->selected >= 0) {
        int wrapper = ui_design_wrap(b, d->selected, "div", error, sizeof error);
        changed(s, wrapper, error, "Could not wrap", "Wrapped in");
    }
}

/* Deletes the selection, offering Undo while the delete is the latest edit. */
static void delete_selected(studio *s, studio_designer *d, ui_buffer *b)
{
    if (d->selected < 0) {
        return;
    }
    char name[96], error[192] = "", title[160];
    designer_element_name(d->rml, d->selected, name, sizeof name);
    int next = ui_design_delete(b, d->selected, error, sizeof error);
    if (error[0]) {
        designer_warn(s, "Could not delete", error);
        return;
    }
    snprintf(title, sizeof title, "Deleted %s", name);
    kit_toast_dismiss(s->kit, d->undo.toast);
    d->undo.toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .title = title, .action = "Undo", .duration = 6 }
    );
    d->undo.revision = b->revision;
    studio_record(s, KIT_TOAST_DEFAULT, title);
    designer_select(s, next);
}

static void undo_delete(studio *s, studio_designer *d, ui_buffer *b)
{
    if (!d->undo.toast || !kit_toast_action(s->kit, d->undo.toast)) {
        return;
    }
    d->undo.toast = 0;
    if (b->revision == d->undo.revision && ui_buffer_can_undo(b)) {
        ui_buffer_undo(b);
        studio_report(s, "Undo");
    }
}

/* A Hierarchy row's drop ("#<element>") moves the element, a Library item's
 * adds its markup. A row dropped on itself stays. */
static void drop(studio *s, studio_designer *d, ui_buffer *b, const char *payload, int target, ui_source_place place)
{
    char error[192] = "";
    if (payload[0] != '#') {
        int added = ui_design_insert(b, target, place, payload, error, sizeof error);
        changed(s, added, error, "Could not add", "Added");
        return;
    }
    int element = atoi(payload + 1);
    bool valid = element >= 0 && element < acount(d->rml->elements);
    if (!valid || (target >= element &&
                   target < element + ui_design_extent(d->rml, element))) {
        return;
    }
    int moved = ui_design_move(b, element, target, place, error, sizeof error);
    changed(s, moved, error, "Could not move", "Moved");
}

/* Opens the widget a <use> element shows, beside the document. */
static void open_widget(studio *s, studio_designer *d, int element)
{
    char *widget = ui_source_attribute_value(d->rml, element, "ui");
    if (!widget) {
        return;
    }
    char path[sizeof d->path + 160];
    const char *base = content_basename(d->path);
    snprintf(path, sizeof path, "%.*s%s" UI_MARKUP_EXTENSION, (int)(base - d->path), d->path, widget);
    free(widget);
    studio_open_ui_source(s, path, 1);
}

/* ---------- Hierarchy ---------- */

typedef struct
{
    int element; /* -1 for the document */
    int depth;
} tree_row;

/* What a Hierarchy row or its menu asked for, done after the rows are
 * emitted, since a change leaves their indexes behind. */
typedef enum
{
    DO_NOTHING,
    DO_DROP,
    DO_ADD,
    DO_DUPLICATE,
    DO_WRAP,
    DO_SOURCE,
    DO_WIDGET,
    DO_DELETE,
} tree_action;

typedef struct
{
    tree_action action;
    int element;
    ui_source_place place;
    char payload[512];
    char label[96];
} tree_request;

/* An element's place among its siblings down from the top, "0.2.1": folds
 * keep to places, so they outlast edits elsewhere. */
static void place_of(const ui_source *rml, int element, char *out, size_t size)
{
    if (element < 0) {
        out[0] = '\0';
        return;
    }
    int parent = rml->elements[element].parent, nth = 0;
    for (int i = parent + 1; i < element; i++) {
        nth += rml->elements[i].parent == parent;
    }
    char above[160];
    place_of(rml, parent, above, sizeof above);
    snprintf(out, size, "%s%s%d", above, above[0] ? "." : "", nth);
}

static int fold_index(const studio_designer *d, int element)
{
    char place[160];
    place_of(d->rml, element, place, sizeof place);
    for (int i = 0; i < acount(d->folded); i++) {
        if (!strcmp(d->folded[i], place)) {
            return i;
        }
    }
    return -1;
}

static void set_folded(studio_designer *d, int element, bool fold)
{
    int at = fold_index(d, element);
    if (fold && at < 0 && has_children(d->rml, element)) {
        char place[160];
        place_of(d->rml, element, place, sizeof place);
        apush(d->folded, xstrdup(place));
    } else if (!fold && at >= 0) {
        free(d->folded[at]);
        d->folded[at] = d->folded[acount(d->folded) - 1];
        asetlen(d->folded, acount(d->folded) - 1);
    }
}

static bool row_matches(const studio_designer *d, int element)
{
    char name[96], meta[256];
    designer_element_name(d->rml, element, name, sizeof name);
    element_meta(d->rml, element, meta, sizeof meta);
    return studio_matches(d->filter, name) || studio_matches(d->filter, meta);
}

static bool subtree_matches(const studio_designer *d, int element)
{
    int end = element + ui_design_extent(d->rml, element);
    for (int i = element; i < end; i++) {
        if (row_matches(d, i)) {
            return true;
        }
    }
    return false;
}

/* The document, then the elements shown: unfolded, or while filtering
 * those that match or hold a match. */
static tree_row *tree_rows(const studio_designer *d)
{
    tree_row *rows = NULL;
    apush(rows, ((tree_row){ -1, 0 }));
    bool filtering = d->filter[0] != '\0';
    for (int i = 0; i < acount(d->rml->elements) && (filtering || !d->closed);) {
        bool shown = !filtering || subtree_matches(d, i);
        if (shown) {
            apush(rows, ((tree_row){ i, depth_of(d->rml, i) + 1 }));
        }
        bool skip = !shown || (!filtering && fold_index(d, i) >= 0);
        i += skip ? ui_design_extent(d->rml, i) : 1;
    }
    return rows;
}

/* The pointer's place on a row: its edges go before or after, its middle
 * inside a container. */
static ui_source_place drop_place(const studio *s, kit_rect row, bool container)
{
    float t = (s->in.mouse_y - row.y) / fmaxf(row.h, 1);
    if (t < DROP_EDGE || (!container && t < 0.5f)) {
        return UI_SOURCE_BEFORE;
    }
    return t > 1 - DROP_EDGE || !container ? UI_SOURCE_AFTER : UI_SOURCE_INSIDE;
}

/* The row emitted last takes drops, marked with where they would go. */
static void row_drop(studio *s, const studio_designer *d, int element, tree_request *request)
{
    kit *k = s->kit;
    ui_source_place place = element < 0
                              ? UI_SOURCE_INSIDE
                              : drop_place(s, kit_item_rect(k), ui_design_container(d->rml, element));
    bool over = kit_dragging(k, DESIGNER_DRAG) && kit_item_hovered(k);
    kit_attr(k, "data-drop", over ? PLACE_NAMES[place] : "none");
    char payload[sizeof request->payload];
    if (kit_drop_target(k, DESIGNER_DRAG, payload, sizeof payload)) {
        *request = (tree_request){ .action = DO_DROP, .element = element, .place = place };
        snprintf(request->payload, sizeof request->payload, "%s", payload);
    }
}

/* The slots a carried row steps through, as the Hierarchy shows its rows:
   what folded rows hold is left out. */
static ui_design_slot *carry_slots(const studio_designer *d, int *home)
{
    int count = acount(d->rml->elements);
    bool *folded = xcalloc((size_t)count + 1, sizeof *folded);
    for (int i = 0; i < count; i++) {
        folded[i] = fold_index(d, i) >= 0;
    }
    ui_design_slot *slots = NULL;
    *home = ui_design_slots(d->rml, d->carry.element, folded, &slots);
    free(folded);
    return slots;
}

/* While a row is carried, the row its slot names shows where the drop
   goes, as a pointer drag marks it, and the carried row is dimmed. */
static void carry_marks(kit *k, const studio_designer *d, int element)
{
    if (!d->carry.active) {
        return;
    }
    const ui_design_slot *slot = &d->carry.at;
    kit_attr(k, "data-drop", slot->target == element ? PLACE_NAMES[slot->place] : "none");
    kit_flag(k, "data-dragging", element >= 0 && element == d->carry.element);
}

static void open_menu(studio *s, studio_designer *d, int element, const char *menu)
{
    d->menu = element;
    kit_popup_open_at(s->kit, menu, s->in.mouse_x, s->in.mouse_y);
}

static void document_row(studio *s, studio_designer *d, const char *menu, tree_request *request)
{
    kit *k = s->kit;
    char count[16];
    snprintf(count, sizeof count, "%d", acount(d->rml->elements));
    kit_row_desc row = { .label = d->asset,
                         .icon = KIT_ICON_LAYOUT,
                         .twisty = !d->closed,
                         .meta = count,
                         .flags = d->selected < 0 ? KIT_ON | KIT_ACTIVE : 0 };
    unsigned events = kit_row_begin(k, "document", &row);
    kit_row_end(k);
    row_drop(s, d, -1, request);
    carry_marks(k, d, -1);
    if (events & KIT_ROW_TWISTY) {
        d->closed = !d->closed;
    } else if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
        designer_select(s, -1);
    }
    if (events & KIT_ROW_CONTEXT) {
        open_menu(s, d, -1, menu);
    }
}

static void element_row(studio *s, studio_designer *d, const tree_row *row, const char *menu, tree_request *request)
{
    kit *k = s->kit;
    int e = row->element;
    char key[16], name[96], meta[256], payload[16];
    snprintf(key, sizeof key, "e%d", e);
    designer_element_name(d->rml, e, name, sizeof name);
    element_meta(d->rml, e, meta, sizeof meta);
    kit_row_desc desc = { .label = name,
                          .icon = designer_element_icon(d->rml, e),
                          .depth = row->depth,
                          .twisty = has_children(d->rml, e) ? fold_index(d, e) < 0 : -1,
                          .meta = meta,
                          .flags = d->selected == e ? KIT_ON | KIT_ACTIVE : 0 };
    unsigned events = kit_row_begin(k, key, &desc);
    if (has_bindings(d->rml, e)) {
        kit_icon(k, "bound", "bound", KIT_ICON_BINDING);
        kit_tip(k, "Bound to data", NULL, NULL);
    }
    kit_row_end(k);
    snprintf(payload, sizeof payload, "#%d", e);
    kit_drag_source(
        k,
        &(
            kit_drag_desc
        ){ .type = DESIGNER_DRAG, .payload = payload, .label = name, .icon = desc.icon }
    );
    row_drop(s, d, e, request);
    carry_marks(k, d, e);
    d->hovered = kit_item_hovered(k) ? e : d->hovered;
    if (events & KIT_ROW_TWISTY) {
        set_folded(d, e, desc.twisty == 1);
        return;
    }
    if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
        designer_select(s, e);
    }
    if (events & KIT_ROW_DOUBLE_CLICK) {
        *request = (tree_request){ .action = strcmp(d->rml->elements[e].tag, "use")
                                               ? DO_SOURCE
                                               : DO_WIDGET,
                                   .element = e };
    }
    if (events & KIT_ROW_CONTEXT) {
        open_menu(s, d, e, menu);
    }
}

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1 };
    return kit_menu_item(k, key, &d);
}

static void ask(tree_request *request, tree_action action, int element)
{
    *request = (tree_request){ .action = action, .element = element };
}

static void element_menu(studio *s, const studio_designer *d, int e, tree_request *request)
{
    kit *k = s->kit;
    if (menu_item(k, "duplicate", "Duplicate", KIT_ICON_COPY, "Shift+D")) {
        ask(request, DO_DUPLICATE, e);
    }
    if (menu_item(k, "wrap", "Wrap in Div", KIT_ICON_WRAP, NULL)) {
        ask(request, DO_WRAP, e);
    }
    kit_menu_separator(k, "s1");
    if (menu_item(k, "source", "Edit Source", KIT_ICON_EDIT, NULL)) {
        ask(request, DO_SOURCE, e);
    }
    if (!strcmp(d->rml->elements[e].tag, "use") &&
        menu_item(k, "widget", "Open Widget", KIT_ICON_EXTERNAL, NULL)) {
        ask(request, DO_WIDGET, e);
    }
    kit_menu_separator(k, "s2");
    if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X")) {
        ask(request, DO_DELETE, e);
    }
}

static void tree_menu(studio *s, const studio_designer *d, const char *key, tree_request *request)
{
    kit *k = s->kit;
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    int e = d->menu < acount(d->rml->elements) ? d->menu : -1;
    char name[96];
    if (e >= 0) {
        designer_element_name(d->rml, e, name, sizeof name);
    } else {
        snprintf(name, sizeof name, "%s", d->asset);
    }
    kit_menu_group(k, "group", name);
    tree_request add = { .action = DO_ADD, .element = e };
    if (kit_submenu_begin(k, "add", "Add", KIT_ICON_ADD)) {
        if (designer_add_items(
                s,
                add.payload,
                sizeof add.payload,
                add.label,
                sizeof add.label
            )) {
            *request = add;
        }
        kit_submenu_end(k);
    }
    if (e >= 0) {
        element_menu(s, d, e, request);
    }
    kit_popup_end(k);
}

static void run(studio *s, const tree_request *request)
{
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    if (request->action != DO_NOTHING && request->action != DO_DROP &&
        request->action != DO_ADD) {
        d->selected = request->element;
    }
    switch (request->action) {
        case DO_NOTHING: break;
        case DO_DROP:
            drop(s, d, b, request->payload, request->element, request->place);
            break;
        case DO_ADD:
            designer_add(s, request->element, request->payload, request->label);
            break;
        case DO_DUPLICATE: duplicate(s, d, b); break;
        case DO_WRAP: wrap(s, d, b); break;
        case DO_SOURCE:
            studio_open_ui_source(s, d->path, d->rml->elements[request->element].line);
            break;
        case DO_WIDGET: open_widget(s, d, request->element); break;
        case DO_DELETE: delete_selected(s, d, b); break;
    }
}

void studio_designer_hierarchy_header(studio *s, int area)
{
    kit *k = s->kit;
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    assert(d);
    kit_label(k, "mode", "designer-mode", "Hierarchy");
    kit_search(k, "filter", d->filter, sizeof d->filter, "Filter");
    char key[32], markup[512], label[96];
    snprintf(key, sizeof key, "designer-add:%d", area);
    kit_button_desc add = { .icon = KIT_ICON_ADD };
    if (kit_button(k, "add", &add)) {
        kit_popup_open(k, key);
    }
    kit_tip(k, "Add", NULL, d->selected >= 0 ? "Into the selection, or after it" : "Into the document");
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    kit_menu_group(k, "group", "Add");
    bool chosen = designer_add_items(s, markup, sizeof markup, label, sizeof label);
    kit_popup_end(k);
    if (chosen) {
        designer_add(s, d->selected, markup, label);
    }
}

/* The row to scroll to: the selection, or while a row is carried the row
   its slot names. */
static int revealed_element(const studio_designer *d)
{
    return d->carry.active ? d->carry.at.target : d->selected;
}

void studio_designer_hierarchy(studio *s, int area)
{
    kit *k = s->kit;
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    assert(d);
    tree_row *rows = tree_rows(d);
    int reveal = -1, shown = revealed_element(d);
    for (int r = 1; r < acount(rows); r++) {
        reveal = rows[r].element == shown ? r : reveal;
    }
    kit_list_desc list = { .count = acount(rows),
                           .row_height = s->prefs.comfortable ? 26 : 22,
                           .reveal = d->reveal_row ? reveal : -1 };
    d->reveal_row = false;
    int first, last;
    kit_list_begin(k, "hierarchy", "outliner designer-tree", &list, &first, &last);
    tree_request request = { .action = DO_NOTHING };
    char payload[sizeof request.payload];
    if (kit_drop_target(k, DESIGNER_DRAG, payload, sizeof payload)) {
        request = (tree_request){ .action = DO_DROP, .element = -1, .place = UI_SOURCE_INSIDE };
        snprintf(request.payload, sizeof request.payload, "%s", payload);
    }
    char menu[32];
    snprintf(menu, sizeof menu, "designer:%d", area);
    for (int r = first; r <= last && r < acount(rows); r++) {
        if (rows[r].element < 0) {
            document_row(s, d, menu, &request);
        } else {
            element_row(s, d, &rows[r], menu, &request);
        }
    }
    kit_list_end(k);
    if (d->filter[0] && acount(rows) == 1) {
        kit_label(k, "empty", "outliner-empty", "No matches");
    }
    afree(rows);
    tree_menu(s, d, menu, &request);
    run(s, &request);
}

/* Up and Down walk the rows shown, Left and Right fold. */
static void step(studio *s, studio_designer *d, int direction)
{
    tree_row *rows = tree_rows(d);
    int at = 0;
    for (int r = 0; r < acount(rows); r++) {
        at = rows[r].element == d->selected ? r : at;
    }
    int next = at + direction;
    if (next >= 0 && next < acount(rows)) {
        designer_select(s, rows[next].element);
    }
    afree(rows);
}

/* ---------- Keyboard reordering (workstream Q) ---------- */

/* dnd-kit's keyboard sensor over the Hierarchy: Space or Enter picks the
   selected row up, Up and Down move it through the gaps between rows at
   each depth, Space, Enter or Tab drops it and Escape cancels. Each step is
   announced in the status bar, as dnd-kit announces pickup, over, drop and
   cancel. */

static void announce_slot(studio *s, const studio_designer *d, const ui_design_slot *slot, int index, int count)
{
    static const char *const VERBS[] = { "Into", "Before", "After" };
    char name[96], message[192];
    if (slot->target >= 0) {
        designer_element_name(d->rml, slot->target, name, sizeof name);
    } else {
        snprintf(name, sizeof name, "%s", d->asset);
    }
    snprintf(message, sizeof message, "%s %s \xc2\xb7 %d of %d", VERBS[slot->place], name, index + 1, count);
    studio_report(s, message);
}

static void pick_up(studio *s, studio_designer *d, const ui_buffer *b)
{
    if (d->selected < 0 || d->filter[0]) {
        studio_report(s, d->filter[0] ? "Clear the filter to move a row with the keyboard" : "Select a row to move");
        return;
    }
    d->closed = false;
    for (int a = d->rml->elements[d->selected].parent; a >= 0;
         a = d->rml->elements[a].parent) {
        set_folded(d, a, false);
    }
    d->carry.active = true;
    d->carry.element = d->selected;
    d->carry.revision = b->revision;
    ui_design_slot *slots = carry_slots(d, &d->carry.slot);
    d->carry.at = slots[d->carry.slot];
    afree(slots);
    char name[96], message[192];
    designer_element_name(d->rml, d->selected, name, sizeof name);
    snprintf(message, sizeof message, "Picked up %s \xc2\xb7 arrows move it, Space drops, Escape cancels", name);
    studio_report(s, message);
}

static void move_carried(studio *s, studio_designer *d, int direction)
{
    int home;
    ui_design_slot *slots = carry_slots(d, &home);
    int next = d->carry.slot + direction;
    d->carry.slot = next >= 0 && next < acount(slots) ? next : d->carry.slot;
    d->carry.at = slots[d->carry.slot];
    announce_slot(s, d, &d->carry.at, d->carry.slot, acount(slots));
    d->reveal_row = true;
    afree(slots);
}

static void drop_carried(studio *s, studio_designer *d, ui_buffer *b)
{
    int home;
    ui_design_slot *slots = carry_slots(d, &home);
    ui_design_slot slot = slots[d->carry.slot];
    bool stays = d->carry.slot == home;
    int element = d->carry.element;
    afree(slots);
    d->carry.active = false;
    if (stays) {
        studio_report(s, "Dropped where it was");
        return;
    }
    char error[192] = "";
    int moved = ui_design_move(b, element, slot.target, slot.place, error, sizeof error);
    changed(s, moved, error, "Could not move", "Moved");
}

static void cancel_carry(studio *s, studio_designer *d, const char *why)
{
    d->carry.active = false;
    d->reveal_row = true;
    studio_report(s, why);
}

/* The carried row's keys; any other is swallowed while it is carried. */
static void carry_key(studio *s, studio_designer *d, ui_buffer *b, SDL_Keycode key)
{
    switch (key) {
        case SDLK_UP:
        case SDLK_DOWN: move_carried(s, d, key == SDLK_DOWN ? 1 : -1); break;
        case SDLK_SPACE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_TAB: drop_carried(s, d, b); break;
        case SDLK_ESCAPE: cancel_carry(s, d, "Cancelled the move"); break;
        default: break;
    }
}

/* A change to the text, or a selection elsewhere, puts a carried row down
   where it was. */
static void keep_carry(studio *s, studio_designer *d, const ui_buffer *b)
{
    if (d->carry.active &&
        (b->revision != d->carry.revision || d->selected != d->carry.element)) {
        cancel_carry(s, d, "Cancelled the move");
    }
}

/* ---------- Text Editor ---------- */

/* The Text Editor's cursor selects the element it is in, and a selection
 * made elsewhere puts the cursor at its element, while the editor holds the
 * document's markup as it is. A change to the text moves the cursor without
 * selecting. */
static void follow_cursor(studio *s, studio_designer *d, ui_buffer *b)
{
    kit_code_editor *e = s->code.editor;
    if (!e || b->part != UI_PART_RML || s->code.editor_part != UI_PART_RML ||
        strcmp(s->code.editor_path, b->path) ||
        strcmp(kit_code_text(e, NULL), ui_buffer_text(b, UI_PART_RML))) {
        return;
    }
    if (d->reveal_cursor >= 0) {
        kit_code_set_cursor(e, d->reveal_cursor);
        d->reveal_cursor = -1;
    } else if (d->cursor_seen == b->revision && kit_code_cursor(e) != d->cursor) {
        int element = ui_design_element_at(d->rml, kit_code_cursor(e));
        if (element >= 0 && element != d->selected) {
            d->selected = element;
            d->reveal_row = true;
        }
    }
    d->cursor = kit_code_cursor(e);
    d->cursor_seen = b->revision;
}

/* ---------- Shown elements ---------- */

static int written_line(const rml_element *el, const char *asset)
{
    char src[192];
    return rml_element_attribute(el, "data-src", src, sizeof src)
             ? ui_design_source_line(src, asset)
             : 0;
}

typedef struct
{
    int line, count;
} line_count;

/* How many children of one parent on `line` came before, counting this one. */
static int count_on(line_count **counts, int line)
{
    for (int i = 0; i < acount(*counts); i++) {
        if ((*counts)[i].line == line) {
            return (*counts)[i].count++;
        }
    }
    apush(*counts, ((line_count){ line, 1 }));
    return 0;
}

typedef struct
{
    const ui_source *rml;
    const char *asset;
    const rml_element *element; /* whose source to find, or NULL */
    int source;                 /* whose element to find, or -1 */
    rml_element *found;
    int found_source;
} shown_walk;

/* Walks the shown elements below `el` whose nearest ancestor the asset
 * wrote is element `parent` (-1 for none): an element is the nth child of
 * its parent on its line, as the markup has them. Elements of other assets,
 * such as a widget's, pass through to the use's children in its slot. */
static bool walk(shown_walk *w, rml_element *el, int parent, line_count **counts)
{
    int count = rml_element_child_count(el);
    for (int i = 0; i < count; i++) {
        rml_element *child = rml_element_child(el, i);
        int line = written_line(child, w->asset);
        if (!line) {
            if (walk(w, child, parent, counts)) {
                return true;
            }
            continue;
        }
        int source = ui_design_child_on_line(w->rml, parent, line, count_on(counts, line));
        if (source >= 0 && (child == w->element || source == w->source)) {
            w->found = child;
            w->found_source = source;
            return true;
        }
        line_count *inner = NULL;
        bool found = source >= 0 && walk(w, child, source, &inner);
        afree(inner);
        if (found) {
            return true;
        }
    }
    return false;
}

static void walk_documents(rml_context *context, shown_walk *w)
{
    for (int i = 0; i < rml_context_document_count(context) && !w->found; i++) {
        line_count *counts = NULL;
        walk(w, rml_context_document(context, i), -1, &counts);
        afree(counts);
    }
}

int studio_designer_shown_source(
    rml_context *context,
    const ui_source *rml,
    const char *asset,
    rml_element *element
)
{
    while (element && !written_line(element, asset)) {
        element = rml_element_parent(element);
    }
    shown_walk w = { .rml = rml,
                     .asset = asset,
                     .element = element,
                     .source = -1,
                     .found_source = -1 };
    if (element) {
        walk_documents(context, &w);
    }
    return w.found_source;
}

rml_element *studio_designer_shown(rml_context *context, const ui_source *rml, const char *asset, int source)
{
    shown_walk w = { .rml = rml, .asset = asset, .source = source, .found_source = -1 };
    if (source >= 0) {
        walk_documents(context, &w);
    }
    return w.found;
}

/* ---------- Canvas ---------- */

/* How the Canvas's pixels map to the kit's: the view zoom crops the frame
 * shown in the Scene's surface, `clip`. */
typedef struct
{
    float x, y, sx, sy;
    kit_rect clip;
} canvas_map;

static bool scene_shown(const studio *s)
{
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (s->prefs.editors[i] == STUDIO_EDITOR_SCENE && s->areas[i].rect.w > 0) {
            return true;
        }
    }
    return false;
}

static bool shows_document(const studio *s, const studio_designer *d)
{
    const char *shown = studio_previewed_ui(s);
    return shown && !strcmp(shown, d->asset) && scene_shown(s);
}

/* The Canvas's documents while they show the document as it is. */
static rml_context *canvas(studio *s, studio_designer *d, const ui_buffer *b, canvas_map *m)
{
    engine_viewport *viewport = shows_document(s, d) ? s->canvas->viewport : NULL;
    render_ui *ui = viewport ? engine_viewport_ui(viewport) : NULL;
    kit_rect r = s->surface.rect;
    int w = 0, h = 0;
    if (ui) {
        engine_viewport_pixel_size(viewport, &w, &h);
    }
    if (!ui || r.w <= 0 || r.h <= 0 || w <= 0 || h <= 0 || !ui_buffer_shown(b)) {
        return NULL;
    }
    float zoom = s->view.zoom;
    *m = (canvas_map){ .x = r.x + (0.5f - s->view.x * zoom) * r.w,
                       .y = r.y + (0.5f - s->view.y * zoom) * r.h,
                       .sx = r.w * zoom / (float)w,
                       .sy = r.h * zoom / (float)h,
                       .clip = r };
    return render_ui_context(ui);
}

static bool contains(rml_element *el, rml_vec2 p)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    return p.x >= at.x && p.y >= at.y && p.x < at.x + size.x && p.y < at.y + size.y;
}

/* The last, innermost shown element at or below `el` holding `p`, text and
 * pointer-events: none included, as the Inspector picks. */
static rml_element *deepest(rml_element *el, rml_vec2 p)
{
    rml_element *found = NULL;
    int count = rml_element_child_count(el);
    for (int i = 0; i < count; i++) {
        rml_element *child = rml_element_child(el, i);
        if (strcmp(rml_element_tag_name(child), "#text") &&
            rml_element_is_visible(child, false)) {
            rml_element *inner = deepest(child, p);
            found = inner ? inner : found;
        }
    }
    return found ? found : contains(el, p) ? el : NULL;
}

/* The document's element under the pointer, from the topmost document. */
static int source_under(const studio *s, const studio_designer *d, rml_context *context, canvas_map m)
{
    rml_vec2 p = { (s->in.mouse_x - m.x) / m.sx, (s->in.mouse_y - m.y) / m.sy };
    for (int i = rml_context_document_count(context) - 1; i >= 0; i--) {
        rml_element *el = deepest(rml_context_document(context, i), p);
        while (el && !written_line(el, d->asset)) {
            el = rml_element_parent(el);
        }
        if (el) {
            return studio_designer_shown_source(context, d->rml, d->asset, el);
        }
    }
    return -1;
}

static bool pointer_in(const studio *s, kit_rect r)
{
    float x = s->in.mouse_x, y = s->in.mouse_y;
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void place(kit *k, kit_rect r)
{
    char value[32];
    snprintf(value, sizeof value, "%.2fdp", r.x);
    kit_style(k, "left", value);
    snprintf(value, sizeof value, "%.2fdp", r.y);
    kit_style(k, "top", value);
    snprintf(value, sizeof value, "%.2fdp", r.w);
    kit_style(k, "width", value);
    snprintf(value, sizeof value, "%.2fdp", r.h);
    kit_style(k, "height", value);
}

/* An element's border box outlined, labeled with its name and size in dp
 * for the selection, as UMG labels its selected widget. */
static void outline(
    studio *s,
    const studio_designer *d,
    rml_context *context,
    canvas_map m,
    int element,
    const char *cls
)
{
    rml_element *el = studio_designer_shown(context, d->rml, d->asset, element);
    if (!el || !rml_element_is_visible(el, true)) {
        return;
    }
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    kit_rect box = { m.x + at.x * m.sx, m.y + at.y * m.sy, size.x * m.sx, size.y * m.sy };
    char name[96], label[160] = "";
    if (!strcmp(cls, "selected")) {
        float ratio = rml_context_dp_ratio(context);
        designer_element_name(d->rml, element, name, sizeof name);
        snprintf(
            label,
            sizeof label,
            "%s  %g \xc3\x97 %g",
            name,
            roundf(size.x / ratio),
            roundf(size.y / ratio)
        );
    }
    kit_highlight_desc desc = { .margin = box,
                                .border = box,
                                .padding = box,
                                .content = box,
                                .clip = m.clip,
                                .label = label };
    kit_highlight(s->kit, cls, &desc, cls);
}

/* Where a drop on the Canvas lands (workstream Q), as a Hierarchy row
 * splits: along the flow of the element's parent, its edges go before and
 * after it and its middle inside a container; on nothing, into the
 * document. */
typedef struct
{
    int target;
    ui_source_place place;
    kit_rect box;
    bool across; /* its siblings run across, as in a flex row */
} canvas_target;

/* Whether siblings lay out across: a flex row's items, or inline boxes in
 * a block. */
static bool flows_across(rml_element *el)
{
    rml_element *parent = rml_element_parent(el);
    char display[32] = "", direction[32] = "";
    if (parent) {
        rml_element_property(parent, "display", display, sizeof display);
        rml_element_property(parent, "flex-direction", direction, sizeof direction);
    }
    if (!strcmp(display, "flex") || !strcmp(display, "inline-flex")) {
        return !strncmp(direction, "row", 3);
    }
    rml_element_property(el, "display", display, sizeof display);
    return !strncmp(display, "inline", 6);
}

static canvas_target canvas_target_at(
    const studio *s,
    const studio_designer *d,
    rml_context *context,
    canvas_map m
)
{
    canvas_target t = { .target = source_under(s, d, context, m),
                        .place = UI_SOURCE_INSIDE };
    rml_element *el = t.target >= 0
                        ? studio_designer_shown(context, d->rml, d->asset, t.target)
                        : NULL;
    if (!el) {
        return t;
    }
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    t.box = (kit_rect){ m.x + at.x * m.sx, m.y + at.y * m.sy, size.x * m.sx, size.y * m.sy };
    t.across = flows_across(el);
    float u = t.across ? (s->in.mouse_x - t.box.x) / fmaxf(t.box.w, 1)
                       : (s->in.mouse_y - t.box.y) / fmaxf(t.box.h, 1);
    bool container = ui_design_container(d->rml, t.target);
    t.place = u < DROP_EDGE || (!container && u < 0.5f) ? UI_SOURCE_BEFORE
            : u > 1 - DROP_EDGE || !container           ? UI_SOURCE_AFTER
                                                        : UI_SOURCE_INSIDE;
    return t;
}

/* The line between siblings where a drop before or after lands, across
 * the element's edge, kept within the Canvas. */
static void drop_line(kit *k, const canvas_target *t, kit_rect clip)
{
    enum
    {
        THICK = 2,
    };
    kit_rect r = t->box;
    bool after = t->place == UI_SOURCE_AFTER;
    if (t->across) {
        r = (kit_rect){ r.x + (after ? r.w : 0) - THICK / 2.0f, r.y, THICK, r.h };
    } else {
        r = (kit_rect){ r.x, r.y + (after ? r.h : 0) - THICK / 2.0f, r.w, THICK };
    }
    float x = fmaxf(r.x, clip.x), y = fmaxf(r.y, clip.y);
    float right = fminf(r.x + r.w, clip.x + clip.w),
          bottom = fminf(r.y + r.h, clip.y + clip.h);
    kit_leaf(k, "drop-line", NULL, "designer-drop-line");
    place(k, (kit_rect){ x, y, fmaxf(right - x, 0), fmaxf(bottom - y, 0) });
}

/* Outlines the selection and the element under the pointer or its row,
 * selects with a click, and takes drops through a zone over the frame that
 * only takes the pointer while a Hierarchy row or a Library item drags.
 * Over the Canvas a drag marks where it lands: the element it goes into,
 * or the line between siblings it goes between. */
static void canvas_frame(studio *s, studio_designer *d, ui_buffer *b)
{
    canvas_map m;
    rml_context *context = canvas(s, d, b, &m);
    if (!context) {
        return;
    }
    kit *k = s->kit;
    bool dragging = kit_dragging(k, DESIGNER_DRAG);
    bool over = dragging && pointer_in(s, m.clip) && d->hovered < 0;
    bool pointed = s->surface.hovered || over;
    canvas_target t = over
                        ? canvas_target_at(s, d, context, m)
                        : (canvas_target){
                              .target = pointed ? source_under(s, d, context, m) : -1
                          };
    if (s->surface.hovered && s->surface.pressed[0]) {
        designer_select(s, t.target);
    }
    kit_inspect_begin(k, "designer", false);
    kit_leaf(k, "drop", NULL, "designer-drop");
    place(k, m.clip);
    kit_flag(k, "data-active", dragging);
    char payload[512];
    bool dropped = kit_drop_target(k, DESIGNER_DRAG, payload, sizeof payload);
    int hover = d->hovered >= 0 ? d->hovered : t.target;
    bool between = over && t.target >= 0 && t.place != UI_SOURCE_INSIDE;
    if (hover >= 0 && hover != d->selected) {
        outline(s, d, context, m, hover, dragging && !between ? "designer-target" : "designer-hover");
    }
    if (between) {
        drop_line(k, &t, m.clip);
    }
    if (d->selected >= 0) {
        outline(s, d, context, m, d->selected, "selected");
    }
    kit_inspect_end(k);
    if (dropped) {
        t = canvas_target_at(s, d, context, m);
        drop(s, d, b, payload, t.target, t.place);
    }
}

/* ---------- Frame and keys ---------- */

void studio_designer_frame(studio *s)
{
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    if (!d) {
        return;
    }
    undo_delete(s, d, b);
    keep_carry(s, d, b);
    follow_cursor(s, d, b);
    canvas_frame(s, d, b);
    d->hovered = -1;
}

bool studio_designer_key(studio *s, studio_editor editor, SDL_Keycode key, SDL_Keymod mod)
{
    ui_buffer *b;
    studio_designer *d = designer_state(s, &b);
    bool hierarchy = editor == STUDIO_EDITOR_OUTLINER;
    if (!d ||
        (!hierarchy && !(editor == STUDIO_EDITOR_SCENE && shows_document(s, d))) ||
        studio_primary(s, mod) || (mod & SDL_KMOD_ALT)) {
        return false;
    }
    if (hierarchy && d->carry.active) {
        carry_key(s, d, b, key);
        return true;
    }
    if (hierarchy && !(mod & SDL_KMOD_SHIFT) &&
        (key == SDLK_SPACE || key == SDLK_RETURN)) {
        pick_up(s, d, b);
        return true;
    }
    if (mod & SDL_KMOD_SHIFT) {
        if (key == SDLK_D) {
            duplicate(s, d, b);
        }
        return key == SDLK_D;
    }
    switch (key) {
        case SDLK_X:
        case SDLK_DELETE: delete_selected(s, d, b); return true;
        case SDLK_UP:
        case SDLK_DOWN:
            if (hierarchy) {
                step(s, d, key == SDLK_DOWN ? 1 : -1);
            }
            return hierarchy;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            if (hierarchy && d->selected >= 0) {
                set_folded(d, d->selected, key == SDLK_LEFT);
            }
            return hierarchy;
        default: return false;
    }
}
