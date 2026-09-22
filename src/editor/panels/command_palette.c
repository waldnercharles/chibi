#include "editor/panels/command_palette.h"

#include "editor/commands/command.h"
#include "editor/ui/ui_shell.h"
#include "editor/ui/widget_observation.h"
#include "editor/workspace/scene_internal.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

struct editor_command_palette
{
    bool open, focus;
    int choice;
    char query[1025], reply[4097], message[256];
};

static const char *string(yyjson_val *root, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(root, key));
    return value ? value : "";
}

static void send(app_scene_authoring *scene, const char *op, const char *key, const char *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "operation", op);
    if (key && !strcmp(key, "undo")) {
        yyjson_mut_obj_add_bool(doc, root, key, true);
    } else if (key) {
        yyjson_mut_obj_add_str(doc, root, key, value);
    }
    if (!strcmp(op, "respond") && !key) {
        yyjson_mut_obj_add_bool(doc, root, "approve", true);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    char *response = editor_commands_api(scene, json);
    yyjson_doc *read = yyjson_read(response, strlen(response), 0);
    yyjson_val *result = yyjson_doc_get_root(read);
    if (scene->commands) {
        scene->commands->message[0] = 0;
        if (!strcmp(string(result, "status"), "failed") ||
            !strcmp(string(result, "status"), "busy")) {
            snprintf(
                scene->commands->message,
                sizeof scene->commands->message,
                "%s",
                string(result, "message")
            );
        }
    }
    yyjson_doc_free(read);
    free(response);
    free(json);
    yyjson_mut_doc_free(doc);
}

void editor_command_palette_open(app_scene_authoring *scene)
{
    if (!scene->commands) {
        scene->commands = xcalloc(1, sizeof *scene->commands);
    }
    scene->commands->open = scene->commands->focus = true;
}

void editor_command_palette_free(editor_command_palette *palette)
{
    free(palette);
}
void editor_command_palette_frame(app_scene_authoring *scene)
{
    editor_commands_frame(scene);
}

void editor_command_palette_draw(app_scene_authoring *scene)
{
    ImGuiIO *io = ImGui_GetIO();
    if ((io->KeySuper || io->KeyCtrl) && ImGui_IsKeyPressed(ImGuiKey_K)) {
        editor_command_palette_open(scene);
    }
    editor_command_palette *p = scene->commands;
    if (!p || !p->open) {
        return;
    }
    if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
        send(scene, "cancel", NULL, NULL);
        p->open = false;
        return;
    }
    char *json = xstrdup(editor_commands_result(scene));
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *status = string(root, "status");
    bool running = !strcmp(status, "resolving");
    bool clarify = !strcmp(status, "needs_clarification");
    bool review = !strcmp(status, "needs_review");
    const ImGuiViewport *vp = ImGui_GetMainViewport();
    float width = fminf(720, vp->WorkSize.x - 32);
    ImGui_SetNextWindowPos(
        (ImVec2){ vp->WorkPos.x + (vp->WorkSize.x - width) * .5f,
                  vp->WorkPos.y + fmaxf(24, vp->WorkSize.y * .12f) },
        ImGuiCond_Appearing
    );
    ImGui_SetNextWindowSize((ImVec2){ width, 0 }, ImGuiCond_Always);
    editor_supervisor_begin(
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking,
        p->focus
    );
    if (p->focus) {
        ImGui_SetKeyboardFocusHere();
        p->focus = false;
    }
    ImGui_BeginDisabled(running);
    ImGui_SetNextItemWidth(-1);
    bool submit = ImGui_InputTextWithHint(
        "##command",
        "What would you like to do?",
        p->query,
        sizeof p->query,
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    editor_ui_track_control("commands.query", "Command", "textbox", p->query);
    bool edited = ImGui_IsItemEdited();
    ImGui_EndDisabled();
    if (edited && (clarify || review)) {
        send(scene, "cancel", NULL, NULL);
        clarify = review = false;
    }
    const char *message = p->message[0] ? p->message : string(root, "message");
    if (*message) {
        ImGui_TextWrapped("%s", message);
        editor_ui_track_control("commands.status", "Result", "status", message);
    }
    if (yyjson_obj_get(root, "elapsed_ms")) {
        ImGui_TextDisabled("%.2f s", yyjson_get_num(yyjson_obj_get(root, "elapsed_ms")) / 1000.0);
    }
    if (clarify) {
        yyjson_val *choices = yyjson_obj_get(root, "choices");
        int count = yyjson_arr_size(choices);
        if (count) {
            if (p->choice >= count) {
                p->choice = 0;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
                p->choice = (p->choice + 1) % count;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
                p->choice = (p->choice + count - 1) % count;
            }
            for (int i = 0; i < count; i++) {
                yyjson_val *choice = yyjson_arr_get(choices, i);
                ImGui_PushIDInt(i);
                bool picked = ImGui_SelectableEx(
                    string(choice, "label"),
                    p->choice == i,
                    0,
                    (ImVec2){ 0, 0 }
                );
                char id[64];
                snprintf(id, sizeof id, "commands.choice.%d", i);
                editor_ui_track_control(
                    id,
                    string(choice, "label"),
                    "option",
                    p->choice == i ? "selected" : ""
                );
                ImGui_PopID();
                if (picked || (submit && p->choice == i)) {
                    send(scene, "respond", "value", string(choice, "id"));
                    submit = false;
                }
            }
        } else if (*string(root, "binding")) {
            ImGui_SetNextItemWidth(-1);
            bool entered = ImGui_InputText("Value", p->reply, sizeof p->reply, ImGuiInputTextFlags_EnterReturnsTrue);
            editor_ui_track_control("commands.reply", "Value", "textbox", p->reply);
            if ((entered || ImGui_Button("Use value")) && p->reply[0]) {
                send(scene, "respond", "value", p->reply);
            }
        }
    }
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if ((review || !strcmp(status, "applied")) && yyjson_arr_size(ops)) {
        yyjson_val *summary = yyjson_obj_get(root, "summary");
        for (int i = 0; i < (int)yyjson_arr_size(summary); i++) {
            ImGui_TextWrapped("%s", yyjson_get_str(yyjson_arr_get(summary, i)));
        }
        if (review && (ImGui_Button("Apply") || submit)) {
            send(scene, "respond", NULL, NULL);
            submit = false;
        }
        if (review) {
            editor_ui_track_control("commands.apply", "Apply", "button", NULL);
        }
    }
    ImGui_BeginDisabled(running || !p->query[0]);
    bool pressed = ImGui_Button("Run");
    editor_ui_track_control("commands.run", "Run", "button", NULL);
    if (pressed || (submit && !clarify && !review && !running && p->query[0])) {
        if (clarify || review) {
            send(scene, "cancel", NULL, NULL);
        }
        send(scene, "submit", "command", p->query);
        p->choice = 0;
    }
    ImGui_EndDisabled();
    if (!strcmp(status, "applied") &&
        yyjson_get_bool(yyjson_obj_get(root, "undoable"))) {
        ImGui_SameLine();
        if (ImGui_Button("Undo")) {
            send(scene, "respond", "undo", "true");
        }
        editor_ui_track_control("commands.undo", "Undo", "button", NULL);
    }
    ImGui_SameLine();
    if (ImGui_Button(running ? "Cancel" : "Close")) {
        send(scene, "cancel", NULL, NULL);
        p->open = false;
    }
    editor_ui_track_control("commands.close", running ? "Cancel" : "Close", "button", NULL);
    if (ImGui_CollapsingHeader("Details", 0)) {
        if (!strcmp(string(root, "feedback"), "incorrect")) {
            ImGui_TextDisabled("Feedback recorded");
        } else if (ImGui_SmallButton("Report incorrect result")) {
            send(scene, "respond", "feedback", "incorrect");
        }
        ImGui_TextWrapped("%s", json);
    }
    ImGui_End();
    yyjson_doc_free(doc);
    free(json);
}
