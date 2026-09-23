#include "toolbar.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <new>

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

namespace {
constexpr int kButtonCount = 4;
constexpr float kPreferredWidth = 196.0f;
constexpr const char *kActionIds[kButtonCount] = {
    "workspace.back",
    "workspace.forward",
    "content.quick_open",
    "workspace.documents.list",
};

class ToolbarListener final : public Rml::EventListener
{
public:
    explicit ToolbarListener(editor_rml_toolbar *toolbar)
      : toolbar_(toolbar)
    {
    }

    void ProcessEvent(Rml::Event &event) override;

private:
    editor_rml_toolbar *toolbar_;
};

void set_string_attribute(Rml::Element *element, const char *name, const char *value)
{
    element->SetAttribute(name, Rml::String(value));
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

bool action_enabled(const Rml::Element *element)
{
    return element && !element->HasAttribute("disabled");
}

Rml::Element *action_item(editor_rml_toolbar *toolbar, Rml::Element *element);

int focused_button_index(const editor_rml_toolbar *toolbar);
void set_keyboard_mode(editor_rml_toolbar *toolbar, bool enabled);
void focus_button(editor_rml_toolbar *toolbar, int direction, bool edge);
void activate_action(editor_rml_toolbar *toolbar, Rml::Element *button);

} // namespace

struct editor_rml_toolbar
{
    Rml::Context *context;
    Rml::ElementDocument *document;
    Rml::Element *root;
    std::array<Rml::Element *, kButtonCount> buttons;
    ToolbarListener *listener;
    editor_rml_menu_action_fn action;
    void *user;
    editor_rml_toolbar_state state;
    bool has_state;
    bool visible;
};

namespace {
Rml::Element *action_item(editor_rml_toolbar *toolbar, Rml::Element *element)
{
    for (; element; element = element->GetParentNode()) {
        if (element->HasAttribute("data-action")) {
            return element;
        }
        if (element == toolbar->root) {
            break;
        }
    }
    return nullptr;
}

int focused_button_index(const editor_rml_toolbar *toolbar)
{
    Rml::Element *focus = toolbar->context->GetFocusElement();
    for (int index = 0; index < kButtonCount; ++index) {
        if (toolbar->buttons[index] == focus ||
            (focus && toolbar->buttons[index]->Contains(focus))) {
            return index;
        }
    }
    return -1;
}

void set_keyboard_mode(editor_rml_toolbar *toolbar, bool enabled)
{
    toolbar->root->SetClass("keyboard-mode", enabled);
}

void focus_button(editor_rml_toolbar *toolbar, int direction, bool edge)
{
    const int current = focused_button_index(toolbar);
    for (int step = 0; step < kButtonCount; ++step) {
        const int candidate = edge
                                ? (direction > 0 ? step : kButtonCount - step - 1)
                                : (current < 0
                                       ? (direction > 0 ? step : kButtonCount - step - 1)
                                       : (current + direction * (step + 1) +
                                          kButtonCount * 2) %
                                             kButtonCount);
        if (action_enabled(toolbar->buttons[candidate])) {
            toolbar->buttons[candidate]->Focus(true);
            return;
        }
    }
    editor_rml_toolbar_blur(toolbar);
}

void activate_action(editor_rml_toolbar *toolbar, Rml::Element *button)
{
    if (!action_enabled(button)) {
        return;
    }
    const Rml::String semantic_id = button->GetId();
    if (toolbar->action) {
        toolbar->action(toolbar->user, semantic_id.c_str());
    }
    editor_rml_toolbar_blur(toolbar);
}

void handle_click(editor_rml_toolbar *toolbar, Rml::Event &event)
{
    Rml::Element *button = action_item(toolbar, event.GetTargetElement());
    if (!button) {
        return;
    }
    event.StopPropagation();
    activate_action(toolbar, button);
}

void handle_mousemove(editor_rml_toolbar *toolbar)
{
    set_keyboard_mode(toolbar, false);
}

void handle_keydown(editor_rml_toolbar *toolbar, Rml::Event &event)
{
    Rml::Element *focus = toolbar->context->GetFocusElement();
    if (!toolbar->visible || !focus || !toolbar->root->Contains(focus)) {
        return;
    }
    const int key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    set_keyboard_mode(toolbar, true);
    if (key == Rml::Input::KI_ESCAPE || key == Rml::Input::KI_TAB) {
        editor_rml_toolbar_blur(toolbar);
        event.StopPropagation();
        return;
    }
    if (key == Rml::Input::KI_HOME || key == Rml::Input::KI_END) {
        focus_button(toolbar, key == Rml::Input::KI_HOME ? 1 : -1, true);
        event.StopPropagation();
        return;
    }
    if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_UP ||
        key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_DOWN) {
        const int direction = key == Rml::Input::KI_LEFT || key == Rml::Input::KI_UP
                                ? -1
                                : 1;
        focus_button(toolbar, direction, false);
        event.StopPropagation();
        return;
    }
    if (key != Rml::Input::KI_RETURN && key != Rml::Input::KI_NUMPADENTER &&
        key != Rml::Input::KI_SPACE) {
        return;
    }
    Rml::Element *button = action_item(toolbar, event.GetTargetElement());
    if (!button) {
        const int index = focused_button_index(toolbar);
        if (index >= 0) {
            button = toolbar->buttons[index];
        }
    }
    if (button) {
        event.StopPropagation();
        activate_action(toolbar, button);
    }
}

void apply_state(editor_rml_toolbar *toolbar)
{
    set_enabled(toolbar->buttons[0], toolbar->state.back);
    set_enabled(toolbar->buttons[1], toolbar->state.forward);
    set_enabled(toolbar->buttons[2], toolbar->state.commands_available);
    set_enabled(toolbar->buttons[3], toolbar->state.commands_available);
    toolbar->root->SetClass("reduced-motion", toolbar->state.reduced_motion);

    const int focused = focused_button_index(toolbar);
    if (focused >= 0 && !action_enabled(toolbar->buttons[focused])) {
        focus_button(toolbar, 1, false);
    }
}

bool states_equal(const editor_rml_toolbar_state &left, const editor_rml_toolbar_state &right)
{
    return left.back == right.back && left.forward == right.forward &&
           left.commands_available == right.commands_available &&
           left.reduced_motion == right.reduced_motion;
}

bool rect_for_element(const editor_rml_toolbar *toolbar, Rml::Element *element, editor_rml_menu_rect *out)
{
    if (!toolbar || !element || !out) {
        return false;
    }
    const float scale = std::max(toolbar->context->GetDensityIndependentPixelRatio(), 0.001f);
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    out->x = offset.x / scale;
    out->y = offset.y / scale;
    out->width = element->GetOffsetWidth() / scale;
    out->height = element->GetOffsetHeight() / scale;
    return out->width > 0.0f && out->height > 0.0f;
}
} // namespace

void ToolbarListener::ProcessEvent(Rml::Event &event)
{
    if (event == "click") {
        handle_click(toolbar_, event);
        return;
    }
    if (event == "mousemove") {
        handle_mousemove(toolbar_);
        return;
    }
    if (event == "keydown") {
        handle_keydown(toolbar_, event);
    }
}

extern "C" editor_rml_toolbar *editor_rml_toolbar_create(
    void *context,
    editor_rml_menu_action_fn action,
    void *user
)
{
    if (!context) {
        return nullptr;
    }
    auto *toolbar = new (std::nothrow) editor_rml_toolbar{};
    if (!toolbar) {
        return nullptr;
    }
    toolbar->context = static_cast<Rml::Context *>(context);
    toolbar->action = action;
    toolbar->user = user;
    toolbar->document = toolbar->context->LoadDocument(CHIBI_RMLUI_RESOURCE_DIR "/toolbar.rml");
    if (!toolbar->document) {
        delete toolbar;
        return nullptr;
    }
    toolbar->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
    toolbar->root = toolbar->document->GetElementById("toolbar-root");
    bool complete = toolbar->root != nullptr;
    for (int index = 0; index < kButtonCount; ++index) {
        toolbar->buttons[index] = toolbar->document->GetElementById(kActionIds[index]);
        complete &= toolbar->buttons[index] != nullptr;
    }
    if (!complete) {
        toolbar->document->Close();
        toolbar->context->Update();
        delete toolbar;
        return nullptr;
    }
    toolbar->listener = new (std::nothrow) ToolbarListener(toolbar);
    if (!toolbar->listener) {
        toolbar->document->Close();
        toolbar->context->Update();
        delete toolbar;
        return nullptr;
    }
    toolbar->root->AddEventListener("click", toolbar->listener);
    toolbar->root->AddEventListener("mousemove", toolbar->listener);
    toolbar->root->AddEventListener("keydown", toolbar->listener);
    toolbar->root->SetClass("visible", false);
    apply_state(toolbar);
    toolbar->has_state = true;
    return toolbar;
}

extern "C" void editor_rml_toolbar_destroy(editor_rml_toolbar *toolbar)
{
    if (!toolbar) {
        return;
    }
    toolbar->root->RemoveEventListener("click", toolbar->listener);
    toolbar->root->RemoveEventListener("mousemove", toolbar->listener);
    toolbar->root->RemoveEventListener("keydown", toolbar->listener);
    editor_rml_toolbar_blur(toolbar);
    toolbar->document->Close();
    toolbar->context->Update();
    delete toolbar->listener;
    delete toolbar;
}

extern "C" void editor_rml_toolbar_set_state(editor_rml_toolbar *toolbar, const editor_rml_toolbar_state *state)
{
    if (!toolbar || !state ||
        (toolbar->has_state && states_equal(toolbar->state, *state))) {
        return;
    }
    toolbar->state = *state;
    apply_state(toolbar);
    toolbar->has_state = true;
}

extern "C" void editor_rml_toolbar_set_bounds(editor_rml_toolbar *toolbar, editor_rml_menu_rect bounds)
{
    if (!toolbar || !toolbar->root) {
        return;
    }
    toolbar->root->SetProperty(Rml::PropertyId::Left, Rml::Property(bounds.x, Rml::Unit::DP));
    toolbar->root->SetProperty(Rml::PropertyId::Top, Rml::Property(bounds.y, Rml::Unit::DP));
    toolbar->root->SetProperty(Rml::PropertyId::Width, Rml::Property(bounds.width, Rml::Unit::DP));
    toolbar->root->SetProperty(Rml::PropertyId::Height, Rml::Property(bounds.height, Rml::Unit::DP));
    toolbar->visible = bounds.width > 0.0f && bounds.height > 0.0f;
    toolbar->root->SetClass("visible", toolbar->visible);
}

extern "C" void editor_rml_toolbar_hide(editor_rml_toolbar *toolbar)
{
    if (!toolbar || !toolbar->root) {
        return;
    }
    editor_rml_toolbar_blur(toolbar);
    toolbar->visible = false;
    toolbar->root->SetClass("visible", false);
}

extern "C" void editor_rml_toolbar_focus(editor_rml_toolbar *toolbar)
{
    if (!toolbar || !toolbar->visible) {
        return;
    }
    if (focused_button_index(toolbar) >= 0) {
        set_keyboard_mode(toolbar, true);
        return;
    }
    focus_button(toolbar, 1, true);
    set_keyboard_mode(toolbar, true);
}

extern "C" void editor_rml_toolbar_blur(editor_rml_toolbar *toolbar)
{
    if (!toolbar || !toolbar->root) {
        return;
    }
    Rml::Element *focus = toolbar->context->GetFocusElement();
    if (focus && toolbar->root->Contains(focus)) {
        toolbar->document->Focus();
    }
    set_keyboard_mode(toolbar, false);
}

extern "C" bool editor_rml_toolbar_captures_keyboard(const editor_rml_toolbar *toolbar)
{
    if (!toolbar || !toolbar->visible) {
        return false;
    }
    Rml::Element *focus = toolbar->context->GetFocusElement();
    return focus && toolbar->root->Contains(focus);
}

extern "C" bool editor_rml_toolbar_item_rect(
    const editor_rml_toolbar *toolbar,
    const char *semantic_id,
    editor_rml_menu_rect *out
)
{
    if (!toolbar || !toolbar->visible || !semantic_id || !out) {
        return false;
    }
    return rect_for_element(toolbar, toolbar->document->GetElementById(semantic_id), out);
}

extern "C" float editor_rml_toolbar_preferred_width(const editor_rml_toolbar *)
{
    return kPreferredWidth;
}
