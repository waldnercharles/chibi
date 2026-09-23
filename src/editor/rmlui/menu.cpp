#include "menu.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <new>

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

namespace {
constexpr int kMenuCount = 3;
constexpr int kMaxMenuItems = 20;
constexpr const char *kMenuNames[kMenuCount] = { "file", "edit", "view" };
constexpr const char *kTriggerIds[kMenuCount] = { "file.menu", "edit.menu", "view.menu" };
constexpr const char *kWrapperIds[kMenuCount] = { "file-wrap", "edit-wrap", "view-wrap" };
constexpr const char *kPopupIds[kMenuCount] = { "file.popup", "edit.popup", "view.popup" };
constexpr const char *kPaneActionIds[EDITOR_RML_MENU_PANE_COUNT] = {
    "view.inspector",        "view.node_canvas", "view.scene",
    "view.compiler_results", "view.output_log",  "view.performance",
};

class MenuListener final : public Rml::EventListener
{
public:
    explicit MenuListener(editor_rml_menu *menu)
      : menu_(menu)
    {
    }
    void ProcessEvent(Rml::Event &event) override;

private:
    editor_rml_menu *menu_;
};

int menu_index(const Rml::String &name)
{
    for (int index = 0; index < kMenuCount; ++index) {
        if (name == kMenuNames[index]) {
            return index;
        }
    }
    return -1;
}

Rml::Element *find_ancestor_with_attribute(Rml::Element *element, const char *attribute)
{
    for (; element; element = element->GetParentNode()) {
        if (element->HasAttribute(attribute)) {
            return element;
        }
    }
    return nullptr;
}

void set_string_attribute(Rml::Element *element, const char *name, const char *value)
{
    element->SetAttribute(name, Rml::String(value));
}

void set_check_state(Rml::Element *element, bool checked)
{
    set_string_attribute(element, "aria-checked", checked ? "true" : "false");
    element->SetClass("checked", checked);
}

void set_enabled(Rml::Element *element, bool enabled)
{
    set_string_attribute(element, "aria-disabled", enabled ? "false" : "true");
    element->SetClass("disabled", !enabled);
    if (enabled) {
        element->RemoveAttribute("disabled");
        return;
    }
    element->SetAttribute("disabled", true);
}

Rml::Element *action_item(Rml::Element *element)
{
    for (; element; element = element->GetParentNode()) {
        if (element->HasAttribute("data-action")) {
            return element;
        }
        if (element->HasAttribute("data-menu-popup")) {
            break;
        }
    }
    return nullptr;
}

Rml::Element *trigger_item(Rml::Element *element)
{
    for (; element; element = element->GetParentNode()) {
        if (element->HasAttribute("data-menu-trigger")) {
            return element;
        }
        if (element->GetId() == "menu-bar") {
            break;
        }
    }
    return nullptr;
}

Rml::Element *popup_element(Rml::Element *element)
{
    return find_ancestor_with_attribute(element, "data-menu-popup");
}

bool action_enabled(Rml::Element *element)
{
    return element && !element->HasAttribute("disabled");
}

} // namespace

struct editor_rml_menu
{
    Rml::Context *context;
    Rml::ElementDocument *document;
    Rml::Element *bar;
    std::array<Rml::Element *, kMenuCount> wrappers;
    std::array<Rml::Element *, kMenuCount> triggers;
    std::array<Rml::Element *, kMenuCount> popups;
    MenuListener *listener;
    editor_rml_menu_action_fn action;
    void *user;
    editor_rml_menu_state state;
    int active_menu;
    bool keyboard_mode;
};

namespace {
void set_popup_open(editor_rml_menu *menu, int index, bool open)
{
    menu->wrappers[index]->SetClass("open", open);
    menu->triggers[index]->SetClass("open", open);
    set_string_attribute(menu->triggers[index], "aria-expanded", open ? "true" : "false");
    set_string_attribute(menu->popups[index], "aria-hidden", open ? "false" : "true");
}

void set_keyboard_mode(editor_rml_menu *menu, bool keyboard)
{
    menu->keyboard_mode = keyboard;
    menu->bar->SetClass("keyboard-open", keyboard);
}

void close_menu(editor_rml_menu *menu, bool return_to_trigger, bool blur_focus)
{
    const int old_menu = menu->active_menu;
    if (old_menu >= 0) {
        set_popup_open(menu, old_menu, false);
    }
    menu->active_menu = -1;
    set_keyboard_mode(menu, false);

    Rml::Element *focus = menu->context->GetFocusElement();
    if (return_to_trigger && old_menu >= 0) {
        menu->triggers[old_menu]->Focus(true);
        set_keyboard_mode(menu, true);
        return;
    }
    if (blur_focus && focus && menu->bar->Contains(focus)) {
        menu->document->Focus();
    }
}

std::array<Rml::Element *, kMaxMenuItems> get_menu_items(editor_rml_menu *menu, int index, int &count)
{
    std::array<Rml::Element *, kMaxMenuItems> items = {};
    count = 0;
    Rml::Element *popup = menu->popups[index];
    for (int child_index = 0;
         child_index < popup->GetNumChildren() && count < kMaxMenuItems;
         ++child_index) {
        Rml::Element *child = popup->GetChild(child_index);
        if (child->HasAttribute("data-action")) {
            items[count++] = child;
        }
    }
    return items;
}

int current_item_index(
    const std::array<Rml::Element *, kMaxMenuItems> &items,
    int count,
    Rml::Element *focus
)
{
    for (int index = 0; index < count; ++index) {
        if (items[index] == focus || items[index]->Contains(focus)) {
            return index;
        }
    }
    return -1;
}

void focus_menu_item(editor_rml_menu *menu, int menu_index_value, int direction, bool edge)
{
    int count = 0;
    const auto items = get_menu_items(menu, menu_index_value, count);
    if (count == 0) {
        return;
    }

    const int current = current_item_index(items, count, menu->context->GetFocusElement());
    int index = current;
    if (edge) {
        index = direction > 0 ? 0 : count - 1;
    } else if (current < 0) {
        index = direction > 0 ? 0 : count - 1;
    } else {
        for (int step = 0; step < count; ++step) {
            index = (index + direction + count) % count;
            if (action_enabled(items[index])) {
                items[index]->Focus(true);
                return;
            }
        }
        return;
    }

    for (int step = 0; step < count; ++step) {
        const int candidate = (index + (direction > 0 ? step : -step) + count) % count;
        if (action_enabled(items[candidate])) {
            items[candidate]->Focus(true);
            return;
        }
    }
}

int focused_trigger_index(editor_rml_menu *menu, Rml::Element *focus)
{
    Rml::Element *trigger = trigger_item(focus);
    if (!trigger) {
        return menu->active_menu;
    }
    return menu_index(trigger->GetAttribute<Rml::String>("data-menu-trigger", ""));
}

void focus_trigger(editor_rml_menu *menu, int index)
{
    index = (index + kMenuCount) % kMenuCount;
    menu->triggers[index]->Focus(true);
}

void open_menu(editor_rml_menu *menu, int index, bool keyboard, int focus_direction)
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

void activate_action(editor_rml_menu *menu, Rml::Element *item)
{
    if (!action_enabled(item)) {
        return;
    }
    const Rml::String semantic_id = item->GetId();
    if (menu->action) {
        menu->action(menu->user, semantic_id.c_str());
    }
    close_menu(menu, false, true);
}

void handle_click(editor_rml_menu *menu, Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    Rml::Element *trigger = trigger_item(target);
    if (trigger) {
        const int index = menu_index(
            trigger->GetAttribute<Rml::String>("data-menu-trigger", "")
        );
        if (index >= 0 && index == menu->active_menu) {
            close_menu(menu, false, true);
        } else if (index >= 0) {
            open_menu(menu, index, false, 1);
        }
        event.StopPropagation();
        return;
    }

    Rml::Element *item = action_item(target);
    if (!item) {
        return;
    }
    Rml::Element *popup = popup_element(item);
    const int index = popup
                        ? menu_index(popup->GetAttribute<Rml::String>("data-menu-popup", ""))
                        : -1;
    if (index == menu->active_menu) {
        activate_action(menu, item);
    }
    event.StopPropagation();
}

void handle_mouseover(editor_rml_menu *menu, Rml::Event &event)
{
    if (menu->active_menu < 0) {
        return;
    }
    set_keyboard_mode(menu, false);
    Rml::Element *trigger = trigger_item(event.GetTargetElement());
    if (!trigger) {
        return;
    }
    const int index = menu_index(
        trigger->GetAttribute<Rml::String>("data-menu-trigger", "")
    );
    if (index >= 0 && index != menu->active_menu) {
        open_menu(menu, index, false, 1);
    }
}

void handle_keydown(editor_rml_menu *menu, Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    Rml::Element *focus = menu->context->GetFocusElement();
    const int key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool in_bar = focus && menu->bar->Contains(focus);
    if (!in_bar) {
        return;
    }
    set_keyboard_mode(menu, true);

    const int trigger_index = focused_trigger_index(menu, focus);
    const int current_menu = menu->active_menu;
    const bool on_trigger = trigger_item(target) != nullptr;
    Rml::Element *item = action_item(target);

    if (key == Rml::Input::KI_ESCAPE) {
        if (current_menu >= 0) {
            close_menu(menu, true, false);
        } else {
            close_menu(menu, false, true);
        }
        event.StopPropagation();
        return;
    }
    if (key == Rml::Input::KI_TAB) {
        close_menu(menu, false, true);
        event.StopPropagation();
        return;
    }

    if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) {
        const int direction = key == Rml::Input::KI_RIGHT ? 1 : -1;
        if (current_menu >= 0) {
            open_menu(menu, (current_menu + direction + kMenuCount) % kMenuCount, true, 1);
        } else {
            focus_trigger(menu, std::max(0, trigger_index) + direction);
            set_keyboard_mode(menu, true);
        }
        event.StopPropagation();
        return;
    }
    if (key == Rml::Input::KI_DOWN || key == Rml::Input::KI_UP) {
        const int direction = key == Rml::Input::KI_DOWN ? 1 : -1;
        if (current_menu < 0 && trigger_index >= 0) {
            open_menu(menu, trigger_index, true, direction);
        } else if (current_menu >= 0) {
            focus_menu_item(menu, current_menu, direction, false);
        }
        event.StopPropagation();
        return;
    }
    if (key == Rml::Input::KI_HOME || key == Rml::Input::KI_END) {
        if (current_menu >= 0) {
            focus_menu_item(menu, current_menu, key == Rml::Input::KI_HOME ? 1 : -1, true);
            event.StopPropagation();
        }
        return;
    }
    if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER ||
        key == Rml::Input::KI_SPACE) {
        if (on_trigger && trigger_index >= 0) {
            if (current_menu == trigger_index) {
                close_menu(menu, true, false);
            } else {
                open_menu(menu, trigger_index, true, 1);
            }
            event.StopPropagation();
            return;
        }
        if (item && current_menu >= 0) {
            activate_action(menu, item);
            event.StopPropagation();
        }
    }
}

void apply_state(editor_rml_menu *menu)
{
    set_enabled(
        menu->document->GetElementById("workspace.quick_open"),
        menu->state.commands_available
    );
    set_enabled(menu->document->GetElementById("file.save"), menu->state.commands_available);
    set_enabled(menu->document->GetElementById("file.save_as"), menu->state.commands_available);
    set_enabled(menu->document->GetElementById("file.save_all"), menu->state.commands_available);
    set_enabled(menu->document->GetElementById("file.reload"), menu->state.commands_available);
    set_enabled(
        menu->document->GetElementById("edit.undo"),
        menu->state.commands_available && menu->state.can_undo
    );
    set_enabled(
        menu->document->GetElementById("edit.redo"),
        menu->state.commands_available && menu->state.can_redo
    );

    set_check_state(
        menu->document->GetElementById("view.navigator"),
        menu->state.navigator_visible
    );
    for (int index = 0; index < EDITOR_RML_MENU_PANE_COUNT; ++index) {
        set_check_state(
            menu->document->GetElementById(kPaneActionIds[index]),
            menu->state.panes[index]
        );
    }
    set_check_state(
        menu->document->GetElementById("view.inline_edge_nodes"),
        menu->state.inline_edge_nodes
    );
    set_check_state(
        menu->document->GetElementById("view.reduced_motion"),
        menu->state.reduced_motion
    );

    const char *quick_open = menu->state.mac_shortcuts ? "⌘P" : "Ctrl+P";
    const char *save = menu->state.mac_shortcuts ? "⌘S" : "Ctrl+S";
    const char *undo = menu->state.mac_shortcuts ? "⌘Z" : "Ctrl+Z";
    const char *redo = menu->state.mac_shortcuts ? "⌘⇧Z" : "Ctrl+Shift+Z";
    menu->document->GetElementById("quick-open-shortcut")->SetInnerRML(quick_open);
    menu->document->GetElementById("save-shortcut")->SetInnerRML(save);
    menu->document->GetElementById("undo-shortcut")->SetInnerRML(undo);
    menu->document->GetElementById("redo-shortcut")->SetInnerRML(redo);
    menu->bar->SetClass("reduced-motion", menu->state.reduced_motion);
}

bool states_equal(const editor_rml_menu_state &left, const editor_rml_menu_state &right)
{
    if (left.commands_available != right.commands_available ||
        left.can_undo != right.can_undo || left.can_redo != right.can_redo ||
        left.navigator_visible != right.navigator_visible ||
        left.inline_edge_nodes != right.inline_edge_nodes ||
        left.reduced_motion != right.reduced_motion ||
        left.mac_shortcuts != right.mac_shortcuts) {
        return false;
    }
    for (int index = 0; index < EDITOR_RML_MENU_PANE_COUNT; ++index) {
        if (left.panes[index] != right.panes[index]) {
            return false;
        }
    }
    return true;
}

bool rect_for_element(const editor_rml_menu *menu, Rml::Element *element, editor_rml_menu_rect *out)
{
    if (!menu || !element || !out) {
        return false;
    }
    const float scale = std::max(menu->context->GetDensityIndependentPixelRatio(), 0.001f);
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    out->x = offset.x / scale;
    out->y = offset.y / scale;
    out->width = element->GetOffsetWidth() / scale;
    out->height = element->GetOffsetHeight() / scale;
    return out->width > 0.0f && out->height > 0.0f;
}

} // namespace

void MenuListener::ProcessEvent(Rml::Event &event)
{
    if (event == "click") {
        handle_click(menu_, event);
        return;
    }
    if (event == "mouseover") {
        handle_mouseover(menu_, event);
        return;
    }
    if (event == "keydown") {
        handle_keydown(menu_, event);
    }
}

extern "C" editor_rml_menu *editor_rml_menu_create(
    void *context,
    editor_rml_menu_action_fn action,
    void *user
)
{
    if (!context) {
        return nullptr;
    }
    auto *menu = new (std::nothrow) editor_rml_menu{};
    if (!menu) {
        return nullptr;
    }
    menu->context = static_cast<Rml::Context *>(context);
    menu->action = action;
    menu->user = user;
    menu->active_menu = -1;
    menu->document = menu->context->LoadDocument(CHIBI_RMLUI_RESOURCE_DIR "/menu.rml");
    if (!menu->document) {
        delete menu;
        return nullptr;
    }
    menu->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
    menu->bar = menu->document->GetElementById("menu-bar");
    bool complete = menu->bar != nullptr;
    for (int index = 0; index < kMenuCount; ++index) {
        menu->wrappers[index] = menu->document->GetElementById(kWrapperIds[index]);
        menu->triggers[index] = menu->document->GetElementById(kTriggerIds[index]);
        menu->popups[index] = menu->document->GetElementById(kPopupIds[index]);
        complete &= menu->wrappers[index] && menu->triggers[index] &&
                    menu->popups[index];
    }
    if (!complete) {
        menu->document->Close();
        menu->context->Update();
        delete menu;
        return nullptr;
    }

    menu->listener = new (std::nothrow) MenuListener(menu);
    if (!menu->listener) {
        menu->document->Close();
        menu->context->Update();
        delete menu;
        return nullptr;
    }
    menu->bar->AddEventListener("click", menu->listener);
    menu->bar->AddEventListener("mouseover", menu->listener);
    menu->bar->AddEventListener("keydown", menu->listener);
    apply_state(menu);
    return menu;
}

extern "C" void editor_rml_menu_destroy(editor_rml_menu *menu)
{
    if (!menu) {
        return;
    }
    menu->bar->RemoveEventListener("click", menu->listener);
    menu->bar->RemoveEventListener("mouseover", menu->listener);
    menu->bar->RemoveEventListener("keydown", menu->listener);
    if (Rml::Element *focus = menu->context->GetFocusElement();
        focus && menu->bar->Contains(focus)) {
        focus->Blur();
    }
    menu->document->Close();
    menu->context->Update();
    delete menu->listener;
    delete menu;
}

extern "C" void editor_rml_menu_set_state(editor_rml_menu *menu, const editor_rml_menu_state *state)
{
    if (!menu || !state || states_equal(menu->state, *state)) {
        return;
    }
    menu->state = *state;
    apply_state(menu);
}

extern "C" void editor_rml_menu_close(editor_rml_menu *menu)
{
    if (menu) {
        close_menu(menu, false, true);
    }
}

extern "C" void editor_rml_menu_focus_bar(editor_rml_menu *menu)
{
    if (!menu) {
        return;
    }
    Rml::Element *focus = menu->context->GetFocusElement();
    if (!focus || !menu->bar->Contains(focus)) {
        menu->triggers[0]->Focus(true);
    }
    set_keyboard_mode(menu, true);
}

extern "C" bool editor_rml_menu_is_open(const editor_rml_menu *menu)
{
    return menu && menu->active_menu >= 0;
}

extern "C" bool editor_rml_menu_captures_keyboard(const editor_rml_menu *menu)
{
    if (!menu) {
        return false;
    }
    Rml::Element *focus = menu->context->GetFocusElement();
    return menu->active_menu >= 0 || (focus && menu->bar->Contains(focus));
}

extern "C" float editor_rml_menu_bar_height(const editor_rml_menu *menu)
{
    if (!menu || !menu->bar) {
        return 0.0f;
    }
    const float scale = std::max(menu->context->GetDensityIndependentPixelRatio(), 0.001f);
    return menu->bar->GetOffsetHeight() / scale;
}

extern "C" bool editor_rml_menu_item_rect(
    const editor_rml_menu *menu,
    const char *semantic_id,
    editor_rml_menu_rect *out
)
{
    if (!menu || !semantic_id || !out) {
        return false;
    }
    Rml::Element *element = menu->document->GetElementById(semantic_id);
    if (!element) {
        return false;
    }
    Rml::Element *popup = popup_element(element);
    if (popup) {
        const int index = menu_index(
            popup->GetAttribute<Rml::String>("data-menu-popup", "")
        );
        if (index != menu->active_menu) {
            return false;
        }
    }
    return rect_for_element(menu, element, out);
}

extern "C" bool editor_rml_menu_popup_rect(const editor_rml_menu *menu, editor_rml_menu_rect *out)
{
    if (!menu || !out || menu->active_menu < 0) {
        return false;
    }
    return rect_for_element(menu, menu->popups[menu->active_menu], out);
}
