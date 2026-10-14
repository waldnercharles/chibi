#include "ui/controls/code_control.h"

#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/input_keys.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Textareas already implement the platform input/IME contract. This element
 * adds only a view protocol and token paint; it never opens or saves a file. */
typedef struct code_control
{
    rml_element *root, *input, *highlight;
    rml_listener *capture, *events;
    char *value;
    int start, end, caret, completion_selected;
    bool dirty, selection_dirty, highlighting, command_dirty, requested;
} code_control;

static char *attribute(rml_element *el, const char *name)
{
    size_t size = rml_element_attribute(el, name, NULL, 0);
    char *text = xmalloc(size + 1);
    rml_element_attribute(el, name, text, size + 1);
    return text;
}

static char *input_value(code_control *c)
{
    size_t size = rml_element_value(c->input, NULL, 0);
    char *text = xmalloc(size + 1);
    rml_element_value(c->input, text, size + 1);
    return text;
}

static void selection(code_control *c, int *start, int *end)
{
    rml_element_selection_range(c->input, start, end);
    *start = code_text_byte_offset(c->value, *start);
    *end = code_text_byte_offset(c->value, *end);
}

static int caret(code_control *c)
{
    int offset = rml_element_textarea_caret(c->input);
    size_t length = strlen(c->value);
    return offset < 0 ? 0 : (size_t)offset > length ? (int)length : offset;
}

static void dispatch(code_control *c, const char *type, const char *command)
{
    char start[24], end[24], line[24], column[24], cursor[24];
    int a, b, l, col;
    selection(c, &a, &b);
    snprintf(start, sizeof start, "%d", a);
    snprintf(end, sizeof end, "%d", b);
    int position = caret(c);
    code_text_position(c->value, position, &l, &col);
    snprintf(cursor, sizeof cursor, "%d", position);
    snprintf(line, sizeof line, "%d", l);
    snprintf(column, sizeof column, "%d", col);
    const char *names[] = { "value", "start",  "end",  "command",
                            "line",  "column", "caret" };
    const char *values[] = { c->value, start,  end,   command ? command : "",
                             line,     column, cursor };
    rml_element_dispatch_texts(c->root, type, 7, names, values);
}

static bool locked(code_control *c)
{
    return control_bool(c->root, "readonly") || control_bool(c->root, "disabled");
}

static void no_highlight(code_control *c)
{
    c->highlighting = false;
    rml_element_remove_property(c->input, "color");
    rml_element_set_property(c->highlight, "display", "none");
}

static void edited(code_control *c)
{
    char *value = input_value(c);
    if (!strcmp(value, c->value)) {
        free(value);
        return;
    }
    if (locked(c)) {
        rml_element_set_value(c->input, c->value);
        free(value);
        return;
    }
    free(c->value);
    c->value = value;
    /* Old semantic spans must not paint new text. The app publishes a new
     * highlights attribute after its document transaction accepts the edit. */
    no_highlight(c);
    dispatch(c, "code-input", NULL);
}

static void insert_tab(code_control *c)
{
    int start, end;
    selection(c, &start, &end);
    char *value = code_text_replace(c->value, start, end, "\t");
    rml_element_set_value(c->input, value);
    int caret = code_text_character_offset(value, start + 1);
    rml_element_set_selection_range(c->input, caret, caret);
    free(value);
    edited(c);
}

static bool platform_key(code_control *c, const rml_event *e, bool mac)
{
    if (!mac) {
        return false;
    }
    rml_event forwarded = *e;
    bool meta = (e->modifiers & RML_KM_META) != 0;
    bool alt = (e->modifiers & RML_KM_ALT) != 0;
    if (meta && !alt &&
        (e->key == RML_KEY_A || e->key == RML_KEY_C || e->key == RML_KEY_X ||
         e->key == RML_KEY_V)) {
        forwarded.modifiers = (e->modifiers & ~RML_KM_META) | RML_KM_CTRL;
    } else if (alt && !meta && (e->key == RML_KEY_LEFT || e->key == RML_KEY_RIGHT || e->key == RML_KEY_BACK || e->key == RML_KEY_DELETE)) {
        forwarded.modifiers = (e->modifiers & ~RML_KM_ALT) | RML_KM_CTRL;
    } else if (meta && !alt && (e->key == RML_KEY_LEFT || e->key == RML_KEY_RIGHT || e->key == RML_KEY_UP || e->key == RML_KEY_DOWN)) {
        forwarded.key = e->key == RML_KEY_LEFT || e->key == RML_KEY_UP
                          ? RML_KEY_HOME
                          : RML_KEY_END;
        forwarded.modifiers = e->modifiers & ~RML_KM_META;
        if (e->key == RML_KEY_UP || e->key == RML_KEY_DOWN) {
            forwarded.modifiers |= RML_KM_CTRL;
        }
    } else {
        return false;
    }
    rml_element_dispatch_event(c->input, "keydown", &forwarded);
    rml_event_stop_propagation(e);
    return true;
}

static void capture(void *user, const rml_event *e)
{
    code_control *c = user;
    if (e->target != c->input) {
        return;
    }
    if (e->id == RML_EVENT_BLUR) {
        selection(c, &c->start, &c->end);
        c->caret = caret(c);
        dispatch(c, "code-selection", NULL);
        return;
    }
    if (e->id == RML_EVENT_TEXTINPUT && locked(c)) {
        rml_event_stop_propagation(e);
        return;
    }
    char *mode = attribute(c->root, "keyboard-mode");
    bool delegated = !strcmp(mode, "delegated");
    free(mode);
    if (delegated && !c->requested && !locked(c) &&
        (e->id == RML_EVENT_KEYDOWN || e->id == RML_EVENT_TEXTINPUT)) {
        char scratch[4], position[24];
        snprintf(position, sizeof position, "%d", caret(c));
        char *buffer = input_value(c);
        if (e->id == RML_EVENT_KEYDOWN) {
            const char *key = ui_key_name(e->key, scratch);
            const char *names[] = { "key",  "control", "shift", "alt",
                                    "meta", "caret",   "buffer" };
            const char *values[] = { key ? key : "",
                                     e->modifiers & RML_KM_CTRL ? "1" : "0",
                                     e->modifiers & RML_KM_SHIFT ? "1" : "0",
                                     e->modifiers & RML_KM_ALT ? "1" : "0",
                                     e->modifiers & RML_KM_META ? "1" : "0",
                                     position,
                                     buffer };
            rml_element_dispatch_texts(c->root, "code-key", 7, names, values);
        } else {
            size_t size = rml_event_text(e, NULL, 0);
            char *glyph = xmalloc(size + 1);
            rml_event_text(e, glyph, size + 1);
            const char *names[] = { "glyph", "caret", "buffer" };
            const char *values[] = { glyph, position, buffer };
            rml_element_dispatch_texts(c->root, "code-text", 3, names, values);
            free(glyph);
        }
        free(buffer);
        rml_event_stop_propagation(e);
        return;
    }
    if (e->id != RML_EVENT_KEYDOWN) {
        return;
    }
#ifdef __APPLE__
    bool mac = true;
#else
    bool mac = false;
#endif
    if (control_bool(c->root, "completion-open")) {
        const char *completion = e->key == RML_KEY_UP   ? "completion-up"
                               : e->key == RML_KEY_DOWN ? "completion-down"
                               : e->key == RML_KEY_RETURN || e->key == RML_KEY_TAB
                                   ? "completion-accept"
                                   : NULL;
        if (completion) {
            dispatch(c, "code-command", completion);
            rml_event_stop_propagation(e);
            return;
        }
    }
    const char *command = code_text_command(e->key, e->modifiers, mac);
    if (command) {
        dispatch(c, "code-command", command);
        rml_event_stop_propagation(e);
        return;
    }
    if (e->key == RML_KEY_TAB &&
        !(e->modifiers & (RML_KM_CTRL | RML_KM_META | RML_KM_ALT))) {
        if (!locked(c)) {
            insert_tab(c);
        }
        rml_event_stop_propagation(e);
        return;
    }
    if (locked(c) && (e->key == RML_KEY_BACK || e->key == RML_KEY_DELETE ||
                      e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER ||
                      ((e->modifiers & (RML_KM_CTRL | RML_KM_META)) &&
                       (e->key == RML_KEY_X || e->key == RML_KEY_V)))) {
        rml_event_stop_propagation(e);
        return;
    }
    platform_key(c, e, mac);
}

static void event(void *user, const rml_event *e)
{
    code_control *c = user;
    if (e->target != c->input) {
        return;
    }
    if (e->id == RML_EVENT_INPUT) {
        edited(c);
        return;
    }
    if (e->id == RML_EVENT_BLUR) {
        dispatch(c, "code-commit", NULL);
    }
}

static void text_part(rml_element *parent, const char *text, int start, int end, const char *classes)
{
    if (end <= start) {
        return;
    }
    rml_element *part = parent;
    if (classes) {
        part = control_part(parent, "span", classes);
    }
    char *slice = xmalloc((size_t)(end - start) + 1);
    memcpy(slice, text + start, (size_t)(end - start));
    slice[end - start] = 0;
    rml_element_set_text(rml_element_create_child(part, "#text", NULL), slice);
    free(slice);
}

static bool boundary(const char *text, int offset, int length)
{
    return offset >= 0 && offset <= length &&
           (offset == length || ((unsigned char)text[offset] & 0xc0) != 0x80);
}

static void highlights(code_control *c)
{
    no_highlight(c);
    while (rml_element_child_count(c->highlight)) {
        rml_element_remove_child(c->highlight, rml_element_first_child(c->highlight));
    }
    char *json = attribute(c->root, "highlights");
    bool requested = json[0] != 0;
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    free(json);
    yyjson_val *spans = doc ? yyjson_doc_get_root(doc) : NULL;
    bool valid = yyjson_is_arr(spans);
    int previous = 0, length = (int)strlen(c->value);
    size_t i, count;
    yyjson_val *span;
    yyjson_arr_foreach(spans, i, count, span)
    {
        yyjson_val *a = yyjson_obj_get(span, "start"),
                   *b = yyjson_obj_get(span, "end");
        const char *classes = yyjson_get_str(yyjson_obj_get(span, "classes"));
        if (!classes) {
            classes = yyjson_get_str(yyjson_obj_get(span, "kind"));
        }
        int64_t start = yyjson_get_sint(a), end = yyjson_get_sint(b);
        if (!yyjson_is_int(a) || !yyjson_is_int(b) || !classes || start < previous ||
            start > end || end > length || !boundary(c->value, (int)start, length) ||
            !boundary(c->value, (int)end, length)) {
            valid = false;
            break;
        }
        text_part(c->highlight, c->value, previous, (int)start, NULL);
        text_part(c->highlight, c->value, (int)start, (int)end, classes);
        previous = (int)end;
    }
    if (valid) {
        text_part(c->highlight, c->value, previous, length, NULL);
        rml_element_set_property(c->highlight, "display", "block");
        rml_element_set_property(c->input, "color", "transparent");
        c->highlighting = true;
    }
    control_flag(c->root, "data-highlight-invalid", requested && !valid);
    yyjson_doc_free(doc);
}

static void sync(code_control *c)
{
    c->dirty = false;
    char *value = attribute(c->root, "value");
    if (strcmp(value, c->value)) {
        free(c->value);
        c->value = value;
        rml_element_set_value(c->input, value);
    } else {
        free(value);
    }
    control_flag(c->input, "disabled", control_bool(c->root, "disabled"));
    const char *space = control_bool(c->root, "wrap") ? "pre-wrap" : "pre";
    rml_element_set_property(c->input, "white-space", space);
    rml_element_set_property(c->highlight, "white-space", space);
    highlights(c);
}

static void requested_command(code_control *c)
{
    char *command = attribute(c->root, "request-command");
    rml_key key = !strcmp(command, "copy")      ? RML_KEY_C
                : !strcmp(command, "cut")       ? RML_KEY_X
                : !strcmp(command, "paste")     ? RML_KEY_V
                : !strcmp(command, "selectAll") ? RML_KEY_A
                                                : RML_KEY_UNKNOWN;
    free(command);
    if (key == RML_KEY_UNKNOWN) {
        return;
    }
    int start = c->start, end = c->end;
    if (c->caret == start && start != end) {
        int swap = start;
        start = end;
        end = swap;
    }
    rml_element_focus(c->input, true);
    if (start >= 0 && end >= 0) {
        rml_element_set_selection_range(
            c->input,
            code_text_character_offset(c->value, start),
            code_text_character_offset(c->value, end)
        );
    }
    rml_event event = { .key = key, .modifiers = RML_KM_CTRL };
    c->requested = true;
    rml_element_dispatch_event(c->input, "keydown", &event);
    c->requested = false;
}

static void completion_view(code_control *c)
{
    if (!control_bool(c->root, "completion-open")) {
        c->completion_selected = -1;
        return;
    }
    char selector[256];
    control_string(c->root, "completion-list", selector, sizeof selector);
    rml_element *document = rml_element_owner_document(c->root);
    rml_element *list = selector[0] && document
                          ? rml_element_query_selector(document, selector)
                          : NULL;
    rml_element *selected = list ? rml_element_query_selector(list, "[aria-selected=true]")
                                 : NULL;
    if (!selected) {
        return;
    }
    double position = control_number(selected, "aria-posinset", -1);
    if (!isfinite(position) || position < 0 || position > INT_MAX) {
        return;
    }
    int index = (int)position;
    if (index != c->completion_selected) {
        c->completion_selected = index;
        rml_element_scroll_into_view(selected, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    }
}

static int offset_attribute(rml_element *el, const char *name, int fallback)
{
    double offset = control_number(el, name, fallback);
    return !isfinite(offset) || offset < 0 ? 0
         : offset > INT_MAX                ? INT_MAX
                                           : (int)offset;
}

static void update(rml_element *el, void *user)
{
    code_control *c = user;
    if (!c) {
        c = xcalloc(1, sizeof *c);
        c->root = el;
        c->value = xstrdup("");
        c->start = c->end = c->caret = c->completion_selected = -1;
        c->dirty = c->selection_dirty = true;
        rml_element_set_user(el, c);
        c->highlight = control_part(el, "div", "code-highlight");
        c->input = control_part(el, "textarea", "code-input");
        rml_element_set_attribute(c->input, "code", "");
        rml_element_set_property(el, "position", "relative");
        rml_element_set_property(el, "overflow", "hidden");
        rml_element_set_property(c->highlight, "position", "absolute");
        rml_element_set_property(c->highlight, "pointer-events", "none");
        rml_element_set_property(c->highlight, "white-space", "pre");
        rml_element_set_property(c->input, "width", "100%");
        rml_element_set_property(c->input, "height", "100%");
        rml_element_set_property(c->input, "white-space", "pre");
        rml_element_set_property(c->input, "padding", "0");
        rml_element_set_property(c->input, "border-width", "0");
        rml_element_set_property(c->input, "background-color", "transparent");
        c->capture = rml_listener_create(capture, c);
        c->events = rml_listener_create(event, c);
        rml_element_add_event_listener(el, RML_EVENT_KEYDOWN, c->capture, true);
        rml_element_add_event_listener(el, RML_EVENT_TEXTINPUT, c->capture, true);
        rml_element_add_event_listener(el, RML_EVENT_BLUR, c->capture, true);
        rml_element_add_event_listener(c->input, RML_EVENT_INPUT, c->events, false);
        rml_element_add_event_listener(c->input, RML_EVENT_BLUR, c->events, false);
    }
    if (c->dirty) {
        sync(c);
    }
    if (c->selection_dirty &&
        rml_context_focus_element(rml_element_context(el)) == c->input) {
        c->selection_dirty = false;
        int start = offset_attribute(el, "selection-start", 0);
        int end = offset_attribute(el, "selection-end", start);
        int cursor = offset_attribute(el, "selection-caret", end);
        if (cursor == start && start != end) {
            int swap = start;
            start = end;
            end = swap;
        }
        rml_element_set_selection_range(
            c->input,
            code_text_character_offset(c->value, start),
            code_text_character_offset(c->value, end)
        );
    }
    if (c->command_dirty) {
        c->command_dirty = false;
        requested_command(c);
    }
    int start, end;
    selection(c, &start, &end);
    int cursor = caret(c);
    if (rml_context_focus_element(rml_element_context(el)) == c->input &&
        (start != c->start || end != c->end || cursor != c->caret)) {
        c->start = start;
        c->end = end;
        c->caret = cursor;
        dispatch(c, "code-selection", NULL);
    }
    completion_view(c);
    if (c->highlighting) {
        char offset[48];
        snprintf(offset, sizeof offset, "%gpx", -rml_element_scroll_left(c->input));
        rml_element_set_property(c->highlight, "left", offset);
        snprintf(offset, sizeof offset, "%gpx", rml_element_client_width(c->input));
        rml_element_set_property(c->highlight, "width", offset);
        snprintf(offset, sizeof offset, "%gpx", -rml_element_scroll_top(c->input));
        rml_element_set_property(c->highlight, "top", offset);
    }
}

static void changed(rml_element *, void *user, const char *name)
{
    code_control *c = user;
    if (!c) {
        return;
    }
    if (!strcmp(name, "request-sequence")) {
        c->command_dirty = true;
        return;
    }
    if (!strcmp(name, "selection-start") || !strcmp(name, "selection-end") ||
        !strcmp(name, "selection-caret")) {
        c->selection_dirty = true;
        return;
    }
    if (!strcmp(name, "value") || !strcmp(name, "highlights") ||
        !strcmp(name, "readonly") || !strcmp(name, "disabled") ||
        !strcmp(name, "wrap")) {
        c->dirty = true;
    }
}

static void release(void *user)
{
    code_control *c = user;
    if (c) {
        rml_element_remove_event_listener(c->root, RML_EVENT_KEYDOWN, c->capture, true);
        rml_element_remove_event_listener(c->root, RML_EVENT_TEXTINPUT, c->capture, true);
        rml_element_remove_event_listener(c->root, RML_EVENT_BLUR, c->capture, true);
        rml_element_remove_event_listener(c->input, RML_EVENT_INPUT, c->events, false);
        rml_element_remove_event_listener(c->input, RML_EVENT_BLUR, c->events, false);
        rml_listener_destroy(c->capture);
        rml_listener_destroy(c->events);
        free(c->value);
        free(c);
    }
}

static const rml_element_hooks HOOKS = { .release = release,
                                         .update = update,
                                         .attribute = changed };

void code_control_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
    }
    rml_register_element_instancer("code-editor", instancer);
}
