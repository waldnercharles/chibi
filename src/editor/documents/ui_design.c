/* The visual designer's changes to a UI document as ui_source edits of its
 * parts, each one undo step. Structural changes also work out where the
 * element they moved or made lands, so the designer can keep it selected. */
#include "editor/documents/ui_design.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The tag a wrapper may take, with room for its "<" and "/>". */
#define TAG_MAX 64

ui_source *ui_design_parse(const ui_buffer *b, ui_part part)
{
    const char *text = ui_buffer_text(b, part);
    assert(text);
    return ui_source_parse_block(text, part == UI_PART_RML ? UI_SOURCE_RML : UI_SOURCE_RCSS);
}

static bool holds(const ui_source *source, int ancestor, int element)
{
    for (int i = element; i >= 0; i = source->elements[i].parent) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

int ui_design_extent(const ui_source *source, int element)
{
    int end = element + 1;
    while (end < acount(source->elements) && holds(source, element, end)) {
        end++;
    }
    return end - element;
}

static bool valid(const ui_source *source, int element, char *error, size_t size)
{
    if (element >= 0 && element < acount(source->elements)) {
        return true;
    }
    snprintf(error, size, "there is no element %d", element);
    return false;
}

/* ---------- Applying ---------- */

/* The part's text becomes `text` through one edit, the bytes between their
 * common start and end, as one undo step. Unchanged text makes no step
 * unless `always`, so a gesture's step stays the last. */
static void replace_part(ui_buffer *b, ui_part part, const char *text, bool always)
{
    const char *old = ui_buffer_text(b, part);
    int length = (int)strlen(old), target = (int)strlen(text), start = 0, end = 0;
    while (start < length && start < target && old[start] == text[start]) {
        start++;
    }
    while (end < length - start && end < target - start &&
           old[length - 1 - end] == text[target - 1 - end]) {
        end++;
    }
    if (start == length && start == target && !always) {
        return;
    }
    int inserted = target - start - end;
    char *middle = xmalloc((size_t)inserted + 1);
    memcpy(middle, text + start, (size_t)inserted);
    middle[inserted] = '\0';
    text_edit edit = { start, length - end, middle };
    ui_buffer_edit(b, part, &edit, 1);
    free(middle);
}

/* Parses the part, first undoing the step a joined change replaces. */
static ui_source *begin(ui_buffer *b, ui_part part, bool join)
{
    if (join) {
        assert(ui_buffer_can_undo(b));
        ui_buffer_undo(b);
    }
    return ui_design_parse(b, part);
}

/* Applies one ui_source call's edits as one step, or on failure puts back
 * the step a joined change undid. Frees the edits and the source. */
static bool finish(ui_buffer *b, ui_part part, ui_source *source, text_edit *edits, bool ok, bool join)
{
    if (ok) {
        char *text = text_edits_apply(source->text, edits, acount(edits));
        replace_part(b, part, text, join);
        free(text);
    } else if (join) {
        ui_buffer_redo(b);
    }
    ui_source_edits_free(edits);
    ui_source_free(source);
    return ok;
}

/* ---------- Structure ---------- */

/* The index an element `count` long has once it leaves `from` and lands
 * `place` `target`, as ui_source moves and inserts put it: inside after the
 * target's children, before or after it. An insertion leaves nothing. */
static int landing(const ui_source *source, int from, int count, int target, ui_source_place place)
{
    int at = target < 0 ? acount(source->elements)
           : place == UI_SOURCE_BEFORE ? target
                                       : target + ui_design_extent(source, target);
    return at > from ? at - count : at;
}

int ui_design_move(ui_buffer *b, int element, int target, ui_source_place place, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = ui_source_move_element(source, element, target, place, &edits, error, size);
    int landed = ok ? landing(source, element, ui_design_extent(source, element), target, place)
                    : -1;
    finish(b, UI_PART_RML, source, edits, ok, false);
    return landed;
}

int ui_design_insert(ui_buffer *b, int target, ui_source_place place, const char *markup, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = ui_source_insert_element(source, target, place, markup, &edits, error, size);
    int landed = ok ? landing(source, -1, 0, target, place) : -1;
    finish(b, UI_PART_RML, source, edits, ok, false);
    return landed;
}

/* The element's text with its line's indentation taken from the lines
 * after its first, as ui_source's insertions indent markup again. */
static char *element_markup(const ui_source *source, int element)
{
    const ui_source_element *el = &source->elements[element];
    const char *text = source->text;
    int line = el->start, indent = 0;
    while (line > 0 && text[line - 1] != '\n') {
        line--;
    }
    while (line + indent < el->start && text[line + indent] == ' ') {
        indent++;
    }
    indent = line + indent == el->start ? indent : 0;
    char *built = NULL;
    for (int i = el->start; i < el->end; i++) {
        apush(built, text[i]);
        for (int k = 0; text[i] == '\n' && k < indent && text[i + 1] == ' '; k++) {
            i++;
        }
    }
    apush(built, '\0');
    char *markup = xstrdup(built);
    afree(built);
    return markup;
}

int ui_design_duplicate(ui_buffer *b, int element, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = valid(source, element, error, size);
    int landed = -1;
    if (ok) {
        char *markup = element_markup(source, element);
        ok = ui_source_insert_element(source, element, UI_SOURCE_AFTER, markup, &edits, error, size);
        landed = element + ui_design_extent(source, element);
        free(markup);
    }
    finish(b, UI_PART_RML, source, edits, ok, false);
    return ok ? landed : -1;
}

static bool valid_tag(const char *tag, char *error, size_t size)
{
    bool ok = tag[0] && strlen(tag) < TAG_MAX;
    for (const char *c = tag; ok && *c; c++) {
        ok = isalnum((unsigned char)*c) || *c == '-';
    }
    if (!ok) {
        snprintf(error, size, "\"%s\" is not a tag", tag);
    }
    return ok;
}

/* A new empty `tag` goes before the element, which lands the wrapper where
 * the element was; the element then moves inside it. */
int ui_design_wrap(ui_buffer *b, int element, const char *tag, char *error, size_t size)
{
    if (!valid_tag(tag, error, size)) {
        return -1;
    }
    char wrapper[TAG_MAX + 4];
    snprintf(wrapper, sizeof wrapper, "<%s/>", tag);
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = ui_source_insert_element(source, element, UI_SOURCE_BEFORE, wrapper, &edits, error, size);
    char *text = ok ? text_edits_apply(source->text, edits, acount(edits)) : NULL;
    ui_source_edits_free(edits);
    ui_source_free(source);
    if (!ok) {
        return -1;
    }
    source = ui_source_parse_block(text, UI_SOURCE_RML);
    free(text);
    edits = NULL;
    ok = ui_source_move_element(source, element + 1, element, UI_SOURCE_INSIDE, &edits, error, size);
    finish(b, UI_PART_RML, source, edits, ok, false);
    return ok ? element : -1;
}

/* The element that takes a deleted one's place in the selection. */
static int after_delete(const ui_source *source, int element)
{
    int parent = source->elements[element].parent;
    int next = element + ui_design_extent(source, element);
    if (next < acount(source->elements) && source->elements[next].parent == parent) {
        return element;
    }
    for (int i = element - 1; i > parent; i--) {
        if (source->elements[i].parent == parent) {
            return i;
        }
    }
    return parent;
}

int ui_design_delete(ui_buffer *b, int element, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = ui_source_delete_element(source, element, &edits, error, size);
    int selected = ok ? after_delete(source, element) : -1;
    finish(b, UI_PART_RML, source, edits, ok, false);
    return selected;
}

/* ---------- Attributes, style and text ---------- */

bool ui_design_set_attribute(
    ui_buffer *b,
    int element,
    const char *name,
    const char *value,
    char *error,
    size_t size
)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = value
                ? ui_source_set_attribute(source, element, name, value, &edits, error, size)
                : ui_source_remove_attribute(source, element, name, &edits, error, size);
    return finish(b, UI_PART_RML, source, edits, ok, false);
}

bool ui_design_class(ui_buffer *b, int element, const char *name, bool on, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = on ? ui_source_add_class(source, element, name, &edits, error, size)
                 : ui_source_remove_class(source, element, name, &edits, error, size);
    return finish(b, UI_PART_RML, source, edits, ok, false);
}

bool ui_design_set_style(
    ui_buffer *b,
    int element,
    const char *property,
    const char *value,
    bool join,
    char *error,
    size_t size
)
{
    ui_source *source = begin(b, UI_PART_RML, join);
    text_edit *edits = NULL;
    bool ok = ui_source_set_style(source, element, property, value, &edits, error, size);
    return finish(b, UI_PART_RML, source, edits, ok, join);
}

bool ui_design_text_only(const ui_source *source, int element)
{
    if (element < 0 || element >= acount(source->elements)) {
        return false;
    }
    const ui_source_element *el = &source->elements[element];
    bool parent = element + 1 < acount(source->elements) &&
                  source->elements[element + 1].parent == element;
    return !el->self_closing && el->close_start < el->end && !parent;
}

bool ui_design_set_text(ui_buffer *b, int element, const char *text, char *error, size_t size)
{
    ui_source *source = ui_design_parse(b, UI_PART_RML);
    text_edit *edits = NULL;
    bool ok = ui_design_text_only(source, element) && !strchr(text, '<');
    if (!ok) {
        snprintf(error, size, "only an element holding text alone takes text without markup");
    } else {
        /* The space around the text keeps its lines. */
        const ui_source_element *el = &source->elements[element];
        int start = el->open_end, end = el->close_start;
        while (start < end && isspace((unsigned char)source->text[start])) {
            start++;
        }
        while (end > start && isspace((unsigned char)source->text[end - 1])) {
            end--;
        }
        apush(edits, ((text_edit){ start, end, xstrdup(text) }));
    }
    return finish(b, UI_PART_RML, source, edits, ok, false);
}

bool ui_design_set_declaration(
    ui_buffer *b,
    int rule,
    int declaration,
    const char *value,
    bool join,
    char *error,
    size_t size
)
{
    ui_source *source = begin(b, UI_PART_RCSS, join);
    text_edit *edits = NULL;
    const ui_source_rule *r = rule >= 0 && rule < acount(source->rules)
                                ? &source->rules[rule]
                                : NULL;
    bool ok = r && declaration >= 0 && declaration < acount(r->declarations) &&
              value[0] && !strpbrk(value, ";{}");
    if (!ok) {
        snprintf(error, size, "\"%s\" is not a declaration's value", value);
    } else {
        const ui_source_declaration *d = &r->declarations[declaration];
        apush(edits, ((text_edit){ d->value_start, d->value_end, xstrdup(value) }));
    }
    return finish(b, UI_PART_RCSS, source, edits, ok, join);
}

/* ---------- Selection ---------- */

int ui_design_source_line(const char *src, const char *asset)
{
    size_t length = strlen(asset);
    if (strncmp(src, asset, length) || src[length] != ':') {
        return 0;
    }
    return atoi(src + length + 1);
}

int ui_design_child_on_line(const ui_source *source, int parent, int line, int nth)
{
    for (int i = parent + 1; i < acount(source->elements); i++) {
        const ui_source_element *el = &source->elements[i];
        if (el->parent == parent && el->line == line && nth-- == 0) {
            return i;
        }
    }
    return -1;
}

int ui_design_element_at(const ui_source *source, int offset)
{
    const char *text = source->text;
    int line = offset;
    while (line > 0 && text[line - 1] != '\n') {
        line--;
    }
    bool leading = true;
    for (int i = line; i < offset; i++) {
        leading &= text[i] == ' ' || text[i] == '\t';
    }
    int at = offset;
    while (leading && (text[at] == ' ' || text[at] == '\t')) {
        at++;
    }
    return ui_source_element_at(source, at);
}

static int starting_at(const ui_source *source, int offset)
{
    for (int i = 0; offset >= 0 && i < acount(source->elements); i++) {
        if (source->elements[i].start == offset) {
            return i;
        }
    }
    return -1;
}

/* Where an element starting at `at` of `a` starts in `b`, the bytes between
 * their common start and end changed: before them it stays, after them it
 * moves with the change, within them it is lost (-1). The common start is
 * matched first, or with `suffix` the common end, since an insertion that
 * begins as the element does matches either way. */
static int moved(const char *a, const char *b, int at, bool suffix)
{
    int la = (int)strlen(a), lb = (int)strlen(b), start = 0, end = 0;
    int most = la < lb ? la : lb;
    while (suffix && end < most && a[la - 1 - end] == b[lb - 1 - end]) {
        end++;
    }
    while (start < most - end && a[start] == b[start]) {
        start++;
    }
    while (!suffix && end < most - start && a[la - 1 - end] == b[lb - 1 - end]) {
        end++;
    }
    return at < start ? at : at >= la - end ? at + lb - la : -1;
}

static bool same_open_tag(const ui_source *a, int i, const ui_source *b, int j)
{
    const ui_source_element *x = &a->elements[i], *y = &b->elements[j];
    int length = x->open_end - x->start;
    return length == y->open_end - y->start &&
           !strncmp(a->text + x->start, b->text + y->start, (size_t)length);
}

/* Both alignments of the change agree unless it inserted text beginning as
 * the element does; then the one finding the element's own start tag wins,
 * the common start's when both do. */
int ui_design_follow(const ui_source *before, const ui_source *after, int element)
{
    if (element < 0 || element >= acount(before->elements)) {
        return -1;
    }
    int at = before->elements[element].start;
    int first = starting_at(after, moved(before->text, after->text, at, false));
    int last = starting_at(after, moved(before->text, after->text, at, true));
    if (first == last || last < 0) {
        return first;
    }
    bool keeps = first >= 0 && same_open_tag(before, element, after, first);
    return first < 0 || (!keeps && same_open_tag(before, element, after, last))
             ? last
             : first;
}
