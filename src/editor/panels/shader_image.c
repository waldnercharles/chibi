#include "editor/panels/shader_details.h"
#include "editor/workspace/editor_internal.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_images.h"

#include <assert.h>
#include <ctype.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

static bool image_matches(const char *text, const char *query)
{
    for (int start = 0;; start++) {
        int i = 0;
        while (query[i] && text[start + i] &&
               tolower((unsigned char)query[i]) ==
                   tolower((unsigned char)text[start + i])) {
            i++;
        }
        if (!query[i]) {
            return true;
        }
        if (!text[start]) {
            return false;
        }
    }
}

static void set_image(editor *e, node *n, const char *path)
{
    const char *current = n->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str;
    if (!strcmp(current ? current : "", path)) {
        return;
    }
    ed_snapshot_before(e);
    pin_value value = { .type = PIN_STR, .str = path };
    int result = graph_set_pin_default(e->active_graph, n, 0, SHADER_IMAGE_INPUT_IMAGE, &value);
    assert(result == 0);
    (void)result;
}

static void image_drop(editor *e, node *n)
{
    if (!ImGui_BeginDragDropTarget()) {
        return;
    }
    const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CHIBI_IMAGE", 0);
    if (payload && payload->Delivery && payload->DataSize > 1) {
        const char *path = payload->Data;
        if (path[payload->DataSize - 1] == '\0') {
            set_image(e, n, path);
        }
    }
    ImGui_EndDragDropTarget();
}

static void image_choices(editor *e, node *n, const char *path)
{
    if (ImGui_IsWindowAppearing()) {
        e->shader_image_search[0] = '\0';
        snprintf(e->shader_image_path, sizeof e->shader_image_path, "%s", path);
        ImGui_SetKeyboardFocusHere();
    }
    editor_ui_search_field(
        "shader.image.search",
        e->shader_image_search,
        sizeof e->shader_image_search,
        "Search images"
    );
    const editor_choices *choices = e->choices_resolver
                                      ? e->choices_resolver(n, 0, SHADER_IMAGE_INPUT_IMAGE, e->choices_user)
                                      : NULL;
    const char *chosen = NULL;
    ImGui_BeginChild("##images", (ImVec2){ 0, ImGui_GetTextLineHeightWithSpacing() * 7 }, 0, 0);
    if (ImGui_SelectableEx("None", !path[0], 0, (ImVec2){ 0 })) {
        chosen = "";
    }
    editor_ui_track_item("shader.image.none");
    int matches = 0;
    for (int i = 0; choices && i < choices->count; i++) {
        const editor_choice *choice = &choices->items[i];
        if (!image_matches(choice->label, e->shader_image_search)) {
            continue;
        }
        matches++;
        ImGui_PushID(choice->name);
        bool selected = ImGui_SelectableEx(choice->label, !strcmp(path, choice->name), 0, (ImVec2){ 0 });
        char id[1200];
        snprintf(id, sizeof id, "shader.image.choice.%s", choice->name);
        editor_ui_track_item(id);
        if (selected) {
            chosen = choice->name;
        }
        ImGui_PopID();
    }
    if (!matches) {
        ImGui_TextDisabled(e->shader_image_search[0] ? "No matching images" : "No images in content");
        editor_ui_track_item("shader.image.empty");
    }
    ImGui_EndChild();
    if (chosen) {
        set_image(e, n, chosen);
        ImGui_CloseCurrentPopup();
        return;
    }
    ImGui_Separator();
    ImGui_SetNextItemWidth(-1);
    bool entered = ImGui_InputTextWithHint(
        "##path",
        "Image path (.png or .jpg)",
        e->shader_image_path,
        sizeof e->shader_image_path,
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    editor_ui_track_item("shader.image.path");
    if (ImGui_Button("Use path") || entered) {
        set_image(e, n, e->shader_image_path);
        ImGui_CloseCurrentPopup();
    }
    editor_ui_track_item("shader.image.use_path");
}

void ed_draw_shader_image(editor *e, node *n)
{
    const char *path = n->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str;
    path = path ? path : "";
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    bool open = ImGui_BeginCombo("Image", path[0] ? name : "Choose image...", ImGuiComboFlags_HeightLarge);
    editor_ui_track_item("shader.image.select");
    if (open) {
        image_choices(e, n, path);
        ImGui_EndCombo();
    } else {
        if (path[0] && ImGui_IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui_SetTooltip("%s", path);
        }
        image_drop(e, n);
    }
    path = n->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str;
    if (!path || !path[0]) {
        return;
    }
    char message[256];
    render_image_load(path, message, sizeof message);
    int width, height;
    if (render_image_info(path, &width, &height)) {
        ImGui_TextDisabled("%d × %d", width, height);
        editor_ui_track_item("shader.image.dimensions");
        ImGui_SameLine();
    }
    if (ImGui_SmallButton("Reload image")) {
        render_image_reload(path, message, sizeof message);
    }
    editor_ui_track_item("shader.image.reload");
    const char *problem = render_image_error(path);
    if (problem[0]) {
        ImGui_TextWrapped("%s", problem);
        editor_ui_track_item("shader.image.error");
    }
}
