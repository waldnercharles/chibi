/* The retained menu bar: File, Edit and View triggers over popups of semantic
 * actions, driven by mouse and keyboard through the RmlUi binding. */
#include "editor/rmlui/menu.h"

#include "support/math.h"
#include "support/xalloc.h"
#include "ui/rml.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

#define MENU_COUNT 3
#define MAX_MENU_ITEMS 20

static const char *const menu_names[MENU_COUNT] = { "file", "edit", "view" };
static const char *const trigger_ids[MENU_COUNT] = { "file.menu", "edit.menu", "view.menu" };
static const char *const wrapper_ids[MENU_COUNT] = { "file-wrap", "edit-wrap", "view-wrap" };
static const char *const popup_ids[MENU_COUNT] = { "file.popup", "edit.popup", "view.popup" };
static const char *const pane_action_ids[RML_INPUT_MENU_PANE_COUNT] = {
    "view.inspector",        "view.node_canvas", "view.scene",
    "view.compiler_results", "view.output_log",  "view.performance",
};
static const rml_event_id events[] = {
    RML_EVENT_CLICK,
    RML_EVENT_MOUSEMOVE,
    RML_EVENT_KEYDOWN,
};

struct editor_rml_menu
{
    rml_context *context;
    rml_element *document;
    rml_element *bar;
    rml_element *wrappers[MENU_COUNT];
    rml_element *triggers[MENU_COUNT];
    rml_element *popups[MENU_COUNT];
    rml_listener *listener;
    editor_rml_menu_action_fn action;
    void *user;
    editor_rml_menu_state state;
    int active_menu;
};

/* The menu an element's attribute names, or -1. */
static int menu_index(const rml_element *element, const char *attribute)
{
    char name[16];
    rml_element_attribute(element, attribute, name, sizeof name);
    for (int index = 0; index < MENU_COUNT; index++) {
        if (strcmp(name, menu_names[index]) == 0) {
            return index;
        }
    }
    return -1;
}

static rml_element *required(rml_element *document, const char *id)
{
    rml_element *element = rml_element_by_id(document, id);
    assert(element);
    return element;
}

static void set_check_state(rml_element *element, bool checked)
{
    rml_element_set_attribute(element, "aria-checked", checked ? "true" : "false");
    rml_element_set_class(element, "checked", checked);
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

static rml_element *action_item(rml_element *element)
{
    for (; element; element = rml_element_parent(element)) {
        if (rml_element_has_attribute(element, "data-action")) {
            return element;
        }
        if (rml_element_has_attribute(element, "data-menu-popup")) {
            break;
        }
    }
    return NULL;
}

static rml_element *trigger_item(rml_element *element)
{
    for (; element; element = rml_element_parent(element)) {
        if (rml_element_has_attribute(element, "data-menu-trigger")) {
            return element;
        }
        if (strcmp(rml_element_id(element), "menu-bar") == 0) {
            break;
        }
    }
    return NULL;
}

static rml_element *popup_element(rml_element *element)
{
    for (; element; element = rml_element_parent(element)) {
        if (rml_element_has_attribute(element, "data-menu-popup")) {
            return element;
        }
    }
    return NULL;
}

static bool bar_has_focus(const editor_rml_menu *menu)
{
    return rml_element_contains(menu->bar, rml_context_focus_element(menu->context));
}

static float dp_scale(const editor_rml_menu *menu)
{
    return math_max(rml_context_dp_ratio(menu->context), 0.001f);
}

static void set_popup_open(editor_rml_menu *menu, int index, bool open)
{
    rml_element_set_class(menu->wrappers[index], "open", open);
    rml_element_set_class(menu->triggers[index], "open", open);
    rml_element_set_attribute(menu->triggers[index], "aria-expanded", open ? "true" : "false");
    rml_element_set_attribute(menu->popups[index], "aria-hidden", open ? "false" : "true");
}

static void set_keyboard_mode(editor_rml_menu *menu, bool keyboard)
{
    rml_element_set_class(menu->bar, "keyboard-open", keyboard);
}

static void close_menu(editor_rml_menu *menu, bool return_to_trigger, bool blur_focus)
{
    int old_menu = menu->active_menu;
    if (old_menu >= 0) {
        set_popup_open(menu, old_menu, false);
    }
    menu->active_menu = -1;
    set_keyboard_mode(menu, false);

    if (return_to_trigger && old_menu >= 0) {
        rml_element_focus(menu->triggers[old_menu], true);
        set_keyboard_mode(menu, true);
        return;
    }
    if (blur_focus && bar_has_focus(menu)) {
        rml_element_focus(menu->document, false);
    }
}

/* The popup's action items, at most MAX_MENU_ITEMS. */
static int menu_items(const editor_rml_menu *menu, int index, rml_element *items[MAX_MENU_ITEMS])
{
    rml_element *popup = menu->popups[index];
    int count = 0;
    int children = rml_element_child_count(popup);
    for (int child = 0; child < children && count < MAX_MENU_ITEMS; child++) {
        rml_element *item = rml_element_child(popup, child);
        if (rml_element_has_attribute(item, "data-action")) {
            items[count++] = item;
        }
    }
    return count;
}

static int current_item(rml_element *const items[], int count, rml_element *focus)
{
    for (int index = 0; index < count; index++) {
        if (rml_element_contains(items[index], focus)) {
            return index;
        }
    }
    return -1;
}

/* Focuses the first enabled item from an edge, or after the focused item, in
 * the direction of travel (1 or -1). */
static void focus_menu_item(editor_rml_menu *menu, int index, int direction, bool edge)
{
    rml_element *items[MAX_MENU_ITEMS];
    int count = menu_items(menu, index, items);
    if (count == 0) {
        return;
    }
    int current = current_item(items, count, rml_context_focus_element(menu->context));
    int start = edge || current < 0 ? (direction > 0 ? 0 : count - 1)
                                    : current + direction;
    for (int step = 0; step < count; step++) {
        rml_element *item = items[(start + direction * step + count) % count];
        if (action_enabled(item)) {
            rml_element_focus(item, true);
            return;
        }
    }
}

/* The focused trigger's menu, else the open menu. */
static int focused_trigger(const editor_rml_menu *menu)
{
    rml_element *trigger = trigger_item(rml_context_focus_element(menu->context));
    if (!trigger) {
        return menu->active_menu;
    }
    return menu_index(trigger, "data-menu-trigger");
}

static void open_menu(editor_rml_menu *menu, int index, bool keyboard, int focus_direction)
{
    if (menu->active_menu >= 0 && menu->active_menu != index) {
        set_popup_open(menu, menu->active_menu, false);
    }
    menu->active_menu = index;
    set_popup_open(menu, index, true);
    set_keyboard_mode(menu, keyboard);
    if (keyboard) {
        focus_menu_item(menu, index, focus_direction, true);
    }
}

/* Closing from the keyboard returns focus to the trigger; a click leaves the
 * bar. */
static void toggle_menu(editor_rml_menu *menu, int index, bool keyboard)
{
    if (index == menu->active_menu) {
        close_menu(menu, keyboard, !keyboard);
        return;
    }
    open_menu(menu, index, keyboard, 1);
}

static void activate_action(editor_rml_menu *menu, rml_element *item)
{
    if (!action_enabled(item)) {
        return;
    }
    if (menu->action) {
        menu->action(menu->user, rml_element_id(item));
    }
    close_menu(menu, false, true);
}

static void handle_click(editor_rml_menu *menu, const rml_event *event)
{
    rml_element *trigger = trigger_item(event->target);
    if (trigger) {
        int index = menu_index(trigger, "data-menu-trigger");
        if (index >= 0) {
            toggle_menu(menu, index, false);
        }
        rml_event_stop_propagation(event);
        return;
    }

    rml_element *item = action_item(event->target);
    if (!item) {
        return;
    }
    rml_element *popup = popup_element(item);
    int index = popup ? menu_index(popup, "data-menu-popup") : -1;
    if (index == menu->active_menu) {
        activate_action(menu, item);
    }
    rml_event_stop_propagation(event);
}

static void handle_mousemove(editor_rml_menu *menu, rml_element *target)
{
    if (menu->active_menu < 0) {
        return;
    }
    set_keyboard_mode(menu, false);
    rml_element *trigger = trigger_item(target);
    if (!trigger) {
        return;
    }
    int index = menu_index(trigger, "data-menu-trigger");
    if (index >= 0 && index != menu->active_menu) {
        open_menu(menu, index, false, 1);
    }
}

/* Left or Right: the neighbouring menu, opened when one is open. */
static void step_menu(editor_rml_menu *menu, int direction)
{
    if (menu->active_menu >= 0) {
        int index = (menu->active_menu + direction + MENU_COUNT) % MENU_COUNT;
        open_menu(menu, index, true, 1);
        return;
    }
    int index = math_max(focused_trigger(menu), 0) + direction;
    rml_element_focus(menu->triggers[(index + MENU_COUNT) % MENU_COUNT], true);
}

/* Down or Up: opens the focused trigger's menu, or moves through the open
 * one. */
static void step_item(editor_rml_menu *menu, int direction)
{
    if (menu->active_menu >= 0) {
        focus_menu_item(menu, menu->active_menu, direction, false);
        return;
    }
    int trigger = focused_trigger(menu);
    if (trigger >= 0) {
        open_menu(menu, trigger, true, direction);
    }
}

/* Enter or Space: toggles a trigger's menu or activates an open item. False
 * when the key does neither. */
static bool press(editor_rml_menu *menu, rml_element *target)
{
    int trigger = focused_trigger(menu);
    if (trigger_item(target) && trigger >= 0) {
        toggle_menu(menu, trigger, true);
        return true;
    }
    rml_element *item = action_item(target);
    if (!item || menu->active_menu < 0) {
        return false;
    }
    activate_action(menu, item);
    return true;
}

static void handle_keydown(editor_rml_menu *menu, const rml_event *event)
{
    if (!bar_has_focus(menu)) {
        return;
    }
    set_keyboard_mode(menu, true);
    bool open = menu->active_menu >= 0;
    switch (event->key) {
        case RML_KEY_ESCAPE: close_menu(menu, open, !open); break;
        case RML_KEY_TAB: close_menu(menu, false, true); break;
        case RML_KEY_LEFT:
        case RML_KEY_RIGHT:
            step_menu(menu, event->key == RML_KEY_RIGHT ? 1 : -1);
            break;
        case RML_KEY_UP:
        case RML_KEY_DOWN:
            step_item(menu, event->key == RML_KEY_DOWN ? 1 : -1);
            break;
        case RML_KEY_HOME:
        case RML_KEY_END:
            if (!open) {
                return;
            }
            focus_menu_item(menu, menu->active_menu, event->key == RML_KEY_HOME ? 1 : -1, true);
            break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
        case RML_KEY_SPACE:
            if (!press(menu, event->target)) {
                return;
            }
            break;
        default: return;
    }
    rml_event_stop_propagation(event);
}

static void process_event(void *user, const rml_event *event)
{
    editor_rml_menu *menu = user;
    switch (event->id) {
        case RML_EVENT_CLICK: handle_click(menu, event); break;
        case RML_EVENT_MOUSEMOVE: handle_mousemove(menu, event->target); break;
        case RML_EVENT_KEYDOWN: handle_keydown(menu, event); break;
        default: break;
    }
}

static void apply_state(editor_rml_menu *menu)
{
    static const char *const commands[] = {
        "workspace.quick_open", "file.save",   "file.save_as",
        "file.save_all",        "file.reload",
    };
    static const struct
    {
        const char *id;
        const char *mac;
        const char *other;
    } shortcuts[] = {
        { "quick-open-shortcut", "⌘P", "Ctrl+P" },
        { "save-shortcut", "⌘S", "Ctrl+S" },
        { "undo-shortcut", "⌘Z", "Ctrl+Z" },
        { "redo-shortcut", "⌘⇧Z", "Ctrl+Shift+Z" },
    };
    const editor_rml_menu_state *state = &menu->state;
    rml_element *document = menu->document;
    rml_element_set_class(menu->bar, "design-preview", state->design_preview);
    rml_element *view = rml_element_by_id(document, "view.popup");
    for (int index = 0; index < rml_element_child_count(view); index++) {
        rml_element *item = rml_element_child(view, index);
        if (rml_element_is_class_set(item, "workspace-only") &&
            rml_element_has_attribute(item, "data-action")) {
            set_enabled(item, !state->design_preview);
        }
    }
    for (size_t index = 0; index < sizeof commands / sizeof *commands; index++) {
        set_enabled(rml_element_by_id(document, commands[index]), state->commands_available);
    }
    set_enabled(
        rml_element_by_id(document, "edit.undo"),
        state->commands_available && state->can_undo
    );
    set_enabled(
        rml_element_by_id(document, "edit.redo"),
        state->commands_available && state->can_redo
    );

    set_check_state(rml_element_by_id(document, "view.navigator"), state->navigator_visible);
    for (int index = 0; index < RML_INPUT_MENU_PANE_COUNT; index++) {
        set_check_state(
            rml_element_by_id(document, pane_action_ids[index]),
            state->panes[index]
        );
    }
    set_check_state(rml_element_by_id(document, "view.reduced_motion"), state->reduced_motion);

    for (size_t index = 0; index < sizeof shortcuts / sizeof *shortcuts; index++) {
        rml_element_set_inner_rml(
            rml_element_by_id(document, shortcuts[index].id),
            state->mac_shortcuts ? shortcuts[index].mac : shortcuts[index].other
        );
    }
    rml_element_set_class(menu->bar, "reduced-motion", state->reduced_motion);
}

static bool element_rect(const editor_rml_menu *menu, rml_element *element, editor_rml_menu_rect *out)
{
    if (!rml_element_is_visible(element, false)) {
        return false;
    }
    float scale = dp_scale(menu);
    rml_vec2 offset = rml_element_absolute_offset(element, RML_BOX_BORDER);
    out->x = offset.x / scale;
    out->y = offset.y / scale;
    out->width = rml_element_offset_width(element) / scale;
    out->height = rml_element_offset_height(element) / scale;
    return out->width > 0.0f && out->height > 0.0f;
}

editor_rml_menu *editor_rml_menu_create(void *context, editor_rml_menu_action_fn action, void *user)
{
    assert(context);
    rml_element *document = rml_context_load_document(context, CHIBI_RMLUI_RESOURCE_DIR "/menu.rml");
    if (!document) {
        return NULL;
    }
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    editor_rml_menu *menu = xcalloc(1, sizeof *menu);
    menu->context = context;
    menu->document = document;
    menu->action = action;
    menu->user = user;
    menu->active_menu = -1;
    menu->bar = required(document, "menu-bar");
    for (int index = 0; index < MENU_COUNT; index++) {
        menu->wrappers[index] = required(document, wrapper_ids[index]);
        menu->triggers[index] = required(document, trigger_ids[index]);
        menu->popups[index] = required(document, popup_ids[index]);
    }
    menu->listener = rml_listener_create(process_event, menu);
    for (size_t index = 0; index < sizeof events / sizeof *events; index++) {
        rml_element_add_event_listener(menu->bar, events[index], menu->listener, false);
    }
    apply_state(menu);
    return menu;
}

void editor_rml_menu_destroy(editor_rml_menu *menu)
{
    if (!menu) {
        return;
    }
    for (size_t index = 0; index < sizeof events / sizeof *events; index++) {
        rml_element_remove_event_listener(menu->bar, events[index], menu->listener, false);
    }
    if (bar_has_focus(menu)) {
        rml_element_blur(rml_context_focus_element(menu->context));
    }
    rml_document_close(menu->document);
    rml_context_update(menu->context);
    rml_listener_destroy(menu->listener);
    free(menu);
}

void editor_rml_menu_set_state(editor_rml_menu *menu, const editor_rml_menu_state *state)
{
    if (memcmp(&menu->state, state, sizeof *state) == 0) {
        return;
    }
    menu->state = *state;
    apply_state(menu);
}

void editor_rml_menu_close(editor_rml_menu *menu)
{
    close_menu(menu, false, true);
}

void editor_rml_menu_focus_bar(editor_rml_menu *menu)
{
    if (!bar_has_focus(menu)) {
        rml_element_focus(menu->triggers[0], true);
    }
    set_keyboard_mode(menu, true);
}

bool editor_rml_menu_is_open(const editor_rml_menu *menu)
{
    return menu->active_menu >= 0;
}

bool editor_rml_menu_captures_keyboard(const editor_rml_menu *menu)
{
    return menu->active_menu >= 0 || bar_has_focus(menu);
}

float editor_rml_menu_bar_height(const editor_rml_menu *menu)
{
    return rml_element_offset_height(menu->bar) / dp_scale(menu);
}

bool editor_rml_menu_item_rect(const editor_rml_menu *menu, const char *semantic_id, editor_rml_menu_rect *out)
{
    rml_element *element = rml_element_by_id(menu->document, semantic_id);
    if (!element) {
        return false;
    }
    rml_element *popup = popup_element(element);
    if (popup && menu_index(popup, "data-menu-popup") != menu->active_menu) {
        return false;
    }
    return element_rect(menu, element, out);
}

bool editor_rml_menu_popup_rect(const editor_rml_menu *menu, editor_rml_menu_rect *out)
{
    if (menu->active_menu < 0) {
        return false;
    }
    return element_rect(menu, menu->popups[menu->active_menu], out);
}
