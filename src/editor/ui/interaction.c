#include "editor/ui/interaction.h"

#include "editor/ui/ui_shell.h"
#include "editor/ui/widget_observation.h"
#include "support/xalloc.h"

#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

struct editor_interaction
{
    editor_ui_control target, destination;
    char text[512];
    int phase, kind, button;
    ImGuiKey key;
    float wheel;
    ImVec2 pointer_before, pointer_target;
    bool moved_pointer;
};
enum
{
    CLICK,
    TYPE,
    KEY,
    SCROLL,
    DRAG
};
static bool input_active;

static const editor_ui_control *find(const char *id)
{
    int count;
    const editor_ui_control *items = editor_ui_controls(&count);
    for (int i = 0; id && i < count; i++) {
        if (!strcmp(id, items[i].id)) {
            return &items[i];
        }
    }
    return NULL;
}

char *editor_interaction_snapshot()
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "popup_depth", editor_ui_popup_depth());
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, root, "controls");
    int count;
    const editor_ui_control *items = editor_ui_controls(&count);
    for (int i = 0; i < count; i++) {
        const editor_ui_control *c = &items[i];
        if (!strncmp(c->id, "commands.", 9) || !strcmp(c->id, "content.commands") ||
            !strncmp(c->window, "Commands", 8) ||
            !strncmp(c->window, "##command", 9)) {
            continue;
        }
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_str(doc, item, "id", c->id);
        yyjson_mut_obj_add_str(doc, item, "label", c->label);
        yyjson_mut_obj_add_str(doc, item, "role", c->role);
        yyjson_mut_obj_add_str(doc, item, "value", c->value);
        yyjson_mut_obj_add_str(doc, item, "window", c->window);
        yyjson_mut_obj_add_bool(doc, item, "enabled", c->enabled);
        yyjson_mut_obj_add_bool(doc, item, "active", c->active);
        yyjson_mut_obj_add_bool(doc, item, "blocked", c->popup_depth < editor_ui_popup_depth());
        yyjson_mut_obj_add_real(doc, item, "x", c->rect.x);
        yyjson_mut_obj_add_real(doc, item, "y", c->rect.y);
        yyjson_mut_obj_add_real(doc, item, "width", c->rect.width);
        yyjson_mut_obj_add_real(doc, item, "height", c->rect.height);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static bool same(const editor_ui_control *now, yyjson_val *offered)
{
    const char *label = yyjson_get_str(yyjson_obj_get(offered, "label"));
    const char *value = yyjson_get_str(yyjson_obj_get(offered, "value"));
    return now && label && value && !strcmp(now->label, label) &&
           !strcmp(now->value, value) && now->enabled &&
           now->popup_depth >= editor_ui_popup_depth() &&
           fabs(now->rect.x - yyjson_get_num(yyjson_obj_get(offered, "x"))) < 1 &&
           fabs(now->rect.y - yyjson_get_num(yyjson_obj_get(offered, "y"))) < 1 &&
           fabs(now->rect.width - yyjson_get_num(yyjson_obj_get(offered, "width"))) < 1 &&
           fabs(now->rect.height - yyjson_get_num(yyjson_obj_get(offered, "height"))) < 1;
}

static bool offered_control(yyjson_val *root, const char *id, editor_ui_control *out)
{
    size_t i, n;
    yyjson_val *item;
    yyjson_arr_foreach(yyjson_obj_get(root, "controls"), i, n, item)
    {
        const char *key = yyjson_get_str(yyjson_obj_get(item, "id"));
        if (!key || !id || strcmp(key, id)) {
            continue;
        }
        const editor_ui_control *control = find(id);
        if (!same(control, item)) {
            return false;
        }
        *out = *control;
        return true;
    }
    return false;
}

editor_interaction *editor_interaction_start(
    const char *observation,
    const char *action,
    char *error,
    int size
)
{
    if (input_active) {
        snprintf(error, size, "Another editor action is in progress");
        return NULL;
    }
    yyjson_doc *o = yyjson_read(observation, strlen(observation), 0),
               *a = yyjson_read(action, strlen(action), 0);
    yyjson_val *root = a ? yyjson_doc_get_root(a) : NULL;
    const char *op = yyjson_get_str(yyjson_obj_get(root, "action"));
    editor_interaction *input = xcalloc(1, sizeof *input);
    input->pointer_before = ImGui_GetIO()->MousePos;
    bool valid = o && a && op;
    if (valid && !strcmp(op, "press")) {
        const char *key = yyjson_get_str(yyjson_obj_get(root, "key"));
        input->kind = KEY;
        input->key = key && !strcmp(key, "Enter")  ? ImGuiKey_Enter
                   : key && !strcmp(key, "Escape") ? ImGuiKey_Escape
                   : key && !strcmp(key, "Tab")    ? ImGuiKey_Tab
                                                   : ImGuiKey_None;
        valid = input->key != ImGuiKey_None &&
                offered_control(
                    yyjson_doc_get_root(o),
                    yyjson_get_str(yyjson_obj_get(root, "id")),
                    &input->target
                );
    } else if (valid) {
        valid = offered_control(
            yyjson_doc_get_root(o),
            yyjson_get_str(yyjson_obj_get(root, "id")),
            &input->target
        );
        if (!strcmp(op, "click")) {
            input->kind = CLICK;
        } else if (!strcmp(op, "right_click")) {
            input->kind = CLICK;
            input->button = 1;
        } else if (!strcmp(op, "type")) {
            input->kind = TYPE;
            const char *value = yyjson_get_str(yyjson_obj_get(root, "text"));
            valid &= value && strlen(value) < sizeof input->text &&
                     !strcmp(input->target.role, "textbox");
            if (valid) {
                snprintf(input->text, sizeof input->text, "%s", value);
            }
        } else if (!strcmp(op, "scroll")) {
            input->kind = SCROLL;
            input->wheel = yyjson_get_num(yyjson_obj_get(root, "amount"));
            valid &= isfinite(input->wheel) && fabsf(input->wheel) <= 8 &&
                     input->wheel != 0;
        } else if (!strcmp(op, "drag")) {
            input->kind = DRAG;
            valid &= offered_control(
                yyjson_doc_get_root(o),
                yyjson_get_str(yyjson_obj_get(root, "destination")),
                &input->destination
            );
        } else {
            valid = false;
        }
    }
    yyjson_doc_free(o);
    yyjson_doc_free(a);
    if (!valid) {
        free(input);
        snprintf(error, size, "The observed control changed or the action is invalid");
        return NULL;
    }
    input_active = true;
    return input;
}

int editor_interaction_frame(editor_interaction *input)
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiKey modifier = io->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    int p = input->phase++;
    if (p == 0) {
        const editor_ui_control *now = find(input->target.id);
        if (!now || !now->enabled || now->popup_depth < editor_ui_popup_depth() ||
            strcmp(now->label, input->target.label) ||
            strcmp(now->value, input->target.value) ||
            fabsf(now->rect.x - input->target.rect.x) > 1 ||
            fabsf(now->rect.y - input->target.rect.y) > 1 ||
            fabsf(now->rect.width - input->target.rect.width) > 1 ||
            fabsf(now->rect.height - input->target.rect.height) > 1) {
            return -1;
        }
    }
    if (input->kind == KEY) {
        if (p == 0) {
            editor_widgets_focus_window(input->target.window);
            if (!strcmp(input->target.role, "textbox")) {
                input->pointer_target = (ImVec2){
                    input->target.rect.x + input->target.rect.width * .5f,
                    input->target.rect.y + input->target.rect.height * .5f
                };
                input->moved_pointer = true;
                ImGuiIO_AddMousePosEvent(
                    io,
                    input->pointer_target.x,
                    input->pointer_target.y
                );
            }
        }
        if (input->moved_pointer && (p == 1 || p == 2)) {
            ImGuiIO_AddMouseButtonEvent(io, 0, p == 1);
        }
        if (p == 4) {
            ImGuiIO_AddKeyEvent(io, input->key, true);
        }
        if (p == 5) {
            ImGuiIO_AddKeyEvent(io, input->key, false);
        }
        return p >= 9;
    }
    if (p == 0) {
        input->pointer_target = (ImVec2){
            input->target.rect.x + input->target.rect.width * .5f,
            input->target.rect.y + input->target.rect.height * .5f
        };
        input->moved_pointer = true;
        ImGuiIO_AddMousePosEvent(
            io,
            input->pointer_target.x,
            input->pointer_target.y
        );
    }
    if (input->kind == SCROLL) {
        if (p == 1) {
            ImGuiIO_AddMouseWheelEvent(io, 0, input->wheel);
        }
        return p >= 8;
    }
    if (p == 1) {
        ImGuiIO_AddMouseButtonEvent(io, input->button, true);
    }
    if (input->kind == DRAG) {
        if (p == 2) {
            input->pointer_target = (ImVec2){
                input->destination.rect.x + input->destination.rect.width * .5f,
                input->destination.rect.y + input->destination.rect.height * .5f
            };
            ImGuiIO_AddMousePosEvent(
                io,
                input->pointer_target.x,
                input->pointer_target.y
            );
        }
        if (p == 3) {
            ImGuiIO_AddMouseButtonEvent(io, 0, false);
        }
        return p >= 7;
    }
    if (p == 2) {
        ImGuiIO_AddMouseButtonEvent(io, input->button, false);
    }
    if (input->kind == CLICK) {
        return p >= 6;
    }
    if (p == 3) {
        ImGuiIO_AddMouseButtonEvent(io, 0, true);
    }
    if (p == 4) {
        ImGuiIO_AddMouseButtonEvent(io, 0, false);
    }
    if (p == 6) {
        const editor_ui_control *now = find(input->target.id);
        if (!now || !now->active) {
            return -1;
        }
        ImGuiIO_AddKeyEvent(io, modifier, true);
        ImGuiIO_AddKeyEvent(io, ImGuiKey_A, true);
    }
    if (p == 7) {
        ImGuiIO_AddKeyEvent(io, ImGuiKey_A, false);
        ImGuiIO_AddKeyEvent(io, modifier, false);
    }
    if (p == 8) {
        ImGuiIO_AddInputCharactersUTF8(io, input->text);
    }
    if (p == 11) {
        const editor_ui_control *now = find(input->target.id);
        return now && !strcmp(now->value, input->text) ? 1 : -1;
    }
    return 0;
}

void editor_interaction_free(editor_interaction *input)
{
    if (!input) {
        return;
    }
    ImGuiIO *io = ImGui_GetIO();
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    ImGuiIO_AddMouseButtonEvent(io, 1, false);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_A, false);
    ImGuiIO_AddKeyEvent(io, io->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, false);
    if (input->kind == KEY) {
        ImGuiIO_AddKeyEvent(io, input->key, false);
    }
    if (input->moved_pointer && fabsf(io->MousePos.x - input->pointer_target.x) < 1 &&
        fabsf(io->MousePos.y - input->pointer_target.y) < 1) {
        ImGuiIO_AddMousePosEvent(
            io,
            input->pointer_before.x,
            input->pointer_before.y
        );
    }
    free(input);
    input_active = false;
}
