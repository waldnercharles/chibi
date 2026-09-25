/* The retained workspace toolbar: Back, Forward, Quick Open and the document
 * list, driven by mouse and keyboard through the RmlUi binding. */
#include "editor/rmlui/toolbar.h"

#include "editor/rmlui/rml.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

#define BUTTON_COUNT 4
#define PREFERRED_WIDTH 196.0f

static const char *const action_ids[BUTTON_COUNT] = {
    "workspace.back",
    "workspace.forward",
    "content.quick_open",
    "workspace.documents.list",
};
static const rml_event_id events[] = {
    RML_EVENT_CLICK,
    RML_EVENT_MOUSEMOVE,
    RML_EVENT_KEYDOWN,
};

struct editor_rml_toolbar
{
    rml_context *context;
    rml_element *document;
    rml_element *root;
    rml_element *buttons[BUTTON_COUNT];
    rml_listener *listener;
    editor_rml_menu_action_fn action;
    void *user;
    editor_rml_toolbar_state state;
    bool visible;
};

static rml_element *required(rml_element *document, const char *id)
{
    rml_element *element = rml_element_by_id(document, id);
    assert(element);
    return element;
}

static void set_enabled(rml_element *element, bool enabled)
{
    rml_element_set_attribute(element, "aria-disabled", enabled ? "false" : "true");
    rml_element_set_class(element, "disabled", !enabled);
    if (enabled) {
        rml_element_remove_attribute(element, "disabled");
        return;
    }
    rml_element_set_attribute(element, "disabled", "");
}

static bool action_enabled(const rml_element *element)
{
    return !rml_element_has_attribute(element, "disabled");
}

static void set_dp(rml_element *element, const char *property, float value)
{
    /* Nine significant digits round-trip a float through RmlUi's parser. */
    char text[32];
    snprintf(text, sizeof text, "%.9gdp", value);
    bool parsed = rml_element_set_property(element, property, text);
    assert(parsed);
}

static rml_element *action_item(const editor_rml_toolbar *toolbar, rml_element *element)
{
    for (; element; element = rml_element_parent(element)) {
        if (rml_element_has_attribute(element, "data-action")) {
            return element;
        }
        if (element == toolbar->root) {
            break;
        }
    }
    return NULL;
}

static bool root_has_focus(const editor_rml_toolbar *toolbar)
{
    return rml_element_contains(toolbar->root, rml_context_focus_element(toolbar->context));
}

static int focused_button(const editor_rml_toolbar *toolbar)
{
    rml_element *focus = rml_context_focus_element(toolbar->context);
    for (int index = 0; index < BUTTON_COUNT; index++) {
        if (rml_element_contains(toolbar->buttons[index], focus)) {
            return index;
        }
    }
    return -1;
}

static void set_keyboard_mode(editor_rml_toolbar *toolbar, bool enabled)
{
    rml_element_set_class(toolbar->root, "keyboard-mode", enabled);
}

/* Focuses the first enabled button from an edge, or after the focused button,
 * in the direction of travel (1 or -1); blurs when none is enabled. */
static void focus_button(editor_rml_toolbar *toolbar, int direction, bool edge)
{
    int current = focused_button(toolbar);
    int start = edge || current < 0 ? (direction > 0 ? 0 : BUTTON_COUNT - 1)
                                    : current + direction;
    for (int step = 0; step < BUTTON_COUNT; step++) {
        rml_element *button = toolbar->buttons[(start + direction * step + BUTTON_COUNT) % BUTTON_COUNT];
        if (action_enabled(button)) {
            rml_element_focus(button, true);
            return;
        }
    }
    editor_rml_toolbar_blur(toolbar);
}

static void activate_action(editor_rml_toolbar *toolbar, rml_element *button)
{
    if (!action_enabled(button)) {
        return;
    }
    if (toolbar->action) {
        toolbar->action(toolbar->user, rml_element_id(button));
    }
    editor_rml_toolbar_blur(toolbar);
}

static void handle_click(editor_rml_toolbar *toolbar, const rml_event *event)
{
    rml_element *button = action_item(toolbar, event->target);
    if (!button) {
        return;
    }
    rml_event_stop_propagation(event);
    activate_action(toolbar, button);
}

/* Enter or Space: activates the target's button, else the focused one. False
 * when there is neither. */
static bool press(editor_rml_toolbar *toolbar, rml_element *target)
{
    rml_element *button = action_item(toolbar, target);
    int index = focused_button(toolbar);
    if (!button && index < 0) {
        return false;
    }
    activate_action(toolbar, button ? button : toolbar->buttons[index]);
    return true;
}

static void handle_keydown(editor_rml_toolbar *toolbar, const rml_event *event)
{
    if (!editor_rml_toolbar_captures_keyboard(toolbar)) {
        return;
    }
    set_keyboard_mode(toolbar, true);
    switch (event->key) {
        case RML_KEY_ESCAPE:
        case RML_KEY_TAB: editor_rml_toolbar_blur(toolbar); break;
        case RML_KEY_HOME:
        case RML_KEY_END:
            focus_button(toolbar, event->key == RML_KEY_HOME ? 1 : -1, true);
            break;
        case RML_KEY_LEFT:
        case RML_KEY_UP: focus_button(toolbar, -1, false); break;
        case RML_KEY_RIGHT:
        case RML_KEY_DOWN: focus_button(toolbar, 1, false); break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
        case RML_KEY_SPACE:
            if (!press(toolbar, event->target)) {
                return;
            }
            break;
        default: return;
    }
    rml_event_stop_propagation(event);
}

static void process_event(void *user, const rml_event *event)
{
    editor_rml_toolbar *toolbar = user;
    switch (event->id) {
        case RML_EVENT_CLICK: handle_click(toolbar, event); break;
        case RML_EVENT_MOUSEMOVE: set_keyboard_mode(toolbar, false); break;
        case RML_EVENT_KEYDOWN: handle_keydown(toolbar, event); break;
        default: break;
    }
}

static void apply_state(editor_rml_toolbar *toolbar)
{
    const editor_rml_toolbar_state *state = &toolbar->state;
    set_enabled(toolbar->buttons[0], state->back);
    set_enabled(toolbar->buttons[1], state->forward);
    set_enabled(toolbar->buttons[2], state->commands_available);
    set_enabled(toolbar->buttons[3], state->commands_available);
    rml_element_set_class(toolbar->root, "reduced-motion", state->reduced_motion);

    int focused = focused_button(toolbar);
    if (focused >= 0 && !action_enabled(toolbar->buttons[focused])) {
        focus_button(toolbar, 1, false);
    }
}

editor_rml_toolbar *editor_rml_toolbar_create(void *context, editor_rml_menu_action_fn action, void *user)
{
    assert(context);
    rml_element *document = rml_context_load_document(context, CHIBI_RMLUI_RESOURCE_DIR "/toolbar.rml");
    if (!document) {
        return NULL;
    }
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    editor_rml_toolbar *toolbar = xcalloc(1, sizeof *toolbar);
    toolbar->context = context;
    toolbar->document = document;
    toolbar->action = action;
    toolbar->user = user;
    toolbar->root = required(document, "toolbar-root");
    for (int index = 0; index < BUTTON_COUNT; index++) {
        toolbar->buttons[index] = required(document, action_ids[index]);
    }
    toolbar->listener = rml_listener_create(process_event, toolbar);
    for (size_t index = 0; index < sizeof events / sizeof *events; index++) {
        rml_element_add_event_listener(toolbar->root, events[index], toolbar->listener, false);
    }
    rml_element_set_class(toolbar->root, "visible", false);
    apply_state(toolbar);
    return toolbar;
}

void editor_rml_toolbar_destroy(editor_rml_toolbar *toolbar)
{
    if (!toolbar) {
        return;
    }
    for (size_t index = 0; index < sizeof events / sizeof *events; index++) {
        rml_element_remove_event_listener(toolbar->root, events[index], toolbar->listener, false);
    }
    editor_rml_toolbar_blur(toolbar);
    rml_document_close(toolbar->document);
    rml_context_update(toolbar->context);
    rml_listener_destroy(toolbar->listener);
    free(toolbar);
}

void editor_rml_toolbar_set_state(editor_rml_toolbar *toolbar, const editor_rml_toolbar_state *state)
{
    if (memcmp(&toolbar->state, state, sizeof *state) == 0) {
        return;
    }
    toolbar->state = *state;
    apply_state(toolbar);
}

void editor_rml_toolbar_set_bounds(editor_rml_toolbar *toolbar, editor_rml_menu_rect bounds)
{
    set_dp(toolbar->root, "left", bounds.x);
    set_dp(toolbar->root, "top", bounds.y);
    set_dp(toolbar->root, "width", bounds.width);
    set_dp(toolbar->root, "height", bounds.height);
    toolbar->visible = bounds.width > 0.0f && bounds.height > 0.0f;
    rml_element_set_class(toolbar->root, "visible", toolbar->visible);
}

void editor_rml_toolbar_hide(editor_rml_toolbar *toolbar)
{
    editor_rml_toolbar_blur(toolbar);
    toolbar->visible = false;
    rml_element_set_class(toolbar->root, "visible", false);
}

void editor_rml_toolbar_focus(editor_rml_toolbar *toolbar)
{
    if (!toolbar->visible) {
        return;
    }
    if (focused_button(toolbar) < 0) {
        focus_button(toolbar, 1, true);
    }
    set_keyboard_mode(toolbar, true);
}

void editor_rml_toolbar_blur(editor_rml_toolbar *toolbar)
{
    if (root_has_focus(toolbar)) {
        rml_element_focus(toolbar->document, false);
    }
    set_keyboard_mode(toolbar, false);
}

bool editor_rml_toolbar_captures_keyboard(const editor_rml_toolbar *toolbar)
{
    return toolbar->visible && root_has_focus(toolbar);
}

bool editor_rml_toolbar_item_rect(
    const editor_rml_toolbar *toolbar,
    const char *semantic_id,
    editor_rml_menu_rect *out
)
{
    if (!toolbar->visible) {
        return false;
    }
    rml_element *element = rml_element_by_id(toolbar->document, semantic_id);
    if (!element) {
        return false;
    }
    float scale = math_max(rml_context_dp_ratio(toolbar->context), 0.001f);
    rml_vec2 offset = rml_element_absolute_offset(element, RML_BOX_BORDER);
    out->x = offset.x / scale;
    out->y = offset.y / scale;
    out->width = rml_element_offset_width(element) / scale;
    out->height = rml_element_offset_height(element) / scale;
    return out->width > 0.0f && out->height > 0.0f;
}

float editor_rml_toolbar_preferred_width(const editor_rml_toolbar *)
{
    return PREFERRED_WIDTH;
}
