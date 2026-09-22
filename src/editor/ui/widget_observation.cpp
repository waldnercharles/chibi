#include "widget_observation.h"

#include <cstdio>
#include <cstring>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <vector>

static std::vector<editor_widget> items;

extern "C" void editor_widgets_focus_window(const char *name)
{
    ImGui::FocusWindow(ImGui::FindWindowByName(name));
}

extern "C" void editor_supervisor_begin(int flags, bool focus)
{
    ImGuiContext &ctx = *ImGui::GetCurrentContext();
    ImGuiWindow *popup = ctx.OpenPopupStack.empty()
                           ? nullptr
                           : ctx.OpenPopupStack.back().Window;
    ImGui::Begin("Commands", nullptr, flags);
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    // Review must stay usable while the agent is operating a modal dialog.
    // Join that popup's input scope without closing or stealing its controls.
    if (popup) {
        window->ParentWindowInBeginStack = popup;
        ImGui::BringWindowToDisplayFront(window);
    }
    if (focus) {
        ImGui::SetWindowFocus();
    }
}

extern "C" void editor_widgets_begin()
{
    items.clear();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
}

extern "C" const editor_widget *editor_widgets(int *count)
{
    *count = (int)items.size();
    return items.data();
}

extern "C" void editor_widgets_label(unsigned id, const char *label)
{
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->id != id) {
            continue;
        }
        if (!it->label[0]) {
            std::snprintf(it->label, sizeof it->label, "%s", label);
        }
        return;
    }
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &box, const ImGuiLastItemData *data)
{
    ImGuiWindow *window = ctx->CurrentWindow;
    if (!id || !window || !data || !box.Overlaps(window->ClipRect) ||
        !std::strncmp(window->Name, "Commands", 8) ||
        !std::strncmp(window->Name, "##command", 9)) {
        return;
    }
    editor_widget item{};
    ImRect visible = box;
    visible.ClipWith(window->ClipRect);
    item.id = id;
    item.x = visible.Min.x;
    item.y = visible.Min.y;
    item.width = visible.GetWidth();
    item.height = visible.GetHeight();
    item.depth = ctx->BeginPopupStack.Size;
    item.enabled = !(data->ItemFlags & ImGuiItemFlags_Disabled);
    item.active = ctx->ActiveId == id;
    item.focused = ctx->NavId == id;
    std::snprintf(item.window, sizeof item.window, "%s", window->Name);
    items.push_back(item);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags flags)
{
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->id != id) {
            continue;
        }
        std::snprintf(
            it->label,
            sizeof it->label,
            "%.*s",
            (int)(std::strstr(label, "##") ? std::strstr(label, "##") - label
                                           : std::strlen(label)),
            label
        );
        const char *role = flags & ImGuiItemStatusFlags_Inputable ? "textbox"
                         : flags & ImGuiItemStatusFlags_Checkable ? "checkbox"
                         : flags & ImGuiItemStatusFlags_Openable  ? "disclosure"
                                                                  : "button";
        std::snprintf(it->role, sizeof it->role, "%s", role);
        if (flags & ImGuiItemStatusFlags_Checkable) {
            std::snprintf(it->value, sizeof it->value, "%s", flags & ImGuiItemStatusFlags_Checked ? "on" : "off");
        }
        if (flags & ImGuiItemStatusFlags_Openable) {
            std::snprintf(
                it->value,
                sizeof it->value,
                "%s",
                flags & ImGuiItemStatusFlags_Opened ? "open" : "closed"
            );
        }
        if (auto input = ImGui::GetInputTextState(id); input && ctx->ActiveId == id) {
            std::snprintf(it->value, sizeof it->value, "%s", input->GetText());
        }
        return;
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}
const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID)
{
    return nullptr;
}
