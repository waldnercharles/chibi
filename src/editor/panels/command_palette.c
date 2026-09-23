#include "editor/panels/command_palette.h"

#include "editor/commands/codex_auth.h"
#include "editor/commands/command.h"
#include "editor/commands/model.h"
#include "editor/ui/ui_shell.h"
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
    bool open, focus, settings_requested;
    int choice, host_choice;
    char query[1025], reply[4097], message[256];
    char host[256], model[128];
    editor_codex_login *login;
};

static const struct
{
    const char *label;
    const char *host;
} host_presets[] = {
    { "Ollama", "http://127.0.0.1:11435/api" },
    { "MLX", "http://127.0.0.1:11436/v1" },
    { "llama.cpp", "http://127.0.0.1:11437/v1" },
#ifndef __EMSCRIPTEN__
    { "ChatGPT", "codex" },
#endif
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
        editor_command_model_get_settings(
            scene->commands->host,
            sizeof scene->commands->host,
            scene->commands->model,
            sizeof scene->commands->model
        );
    }
    scene->commands->open = scene->commands->focus = true;
}

void editor_command_palette_free(editor_command_palette *palette)
{
    if (!palette) {
        return;
    }
    editor_codex_login_free(palette->login);
    free(palette);
}
void editor_command_palette_frame(app_scene_authoring *scene)
{
    editor_commands_frame(scene);
    editor_command_palette *palette = scene->commands;
    if (palette && palette->login &&
        editor_codex_login_poll(
            palette->login,
            NULL,
            palette->message,
            sizeof palette->message
        )) {
        editor_codex_login_free(palette->login);
        palette->login = NULL;
    }
}

static void draw_settings(editor_command_palette *p)
{
    if (p->settings_requested) {
        ImGui_OpenPopup("command_settings", 0);
        p->settings_requested = false;
    }
    if (!ImGui_BeginPopup("command_settings", 0)) {
        return;
    }
    const char *preview = "Custom";
    for (int i = 0; i < (int)(sizeof host_presets / sizeof host_presets[0]); i++) {
        if (!strcmp(p->host, host_presets[i].host)) {
            preview = host_presets[i].label;
            p->host_choice = i;
        }
    }
    if (ImGui_BeginCombo("Provider", preview, 0)) {
        for (int i = 0; i < (int)(sizeof host_presets / sizeof host_presets[0]); i++) {
            bool selected = !strcmp(p->host, host_presets[i].host);
            if (ImGui_SelectableEx(host_presets[i].label, selected, 0, (ImVec2){ 0, 0 })) {
                snprintf(p->host, sizeof p->host, "%s", host_presets[i].host);
                p->host_choice = i;
                if (!strcmp(p->host, "codex")) {
                    snprintf(p->model, sizeof p->model, "gpt-6-luna");
                }
            }
        }
        ImGui_EndCombo();
    }
    bool codex = !strcmp(p->host, "codex");
    if (!codex) {
        ImGui_Text("Host");
        ImGui_SetNextItemWidth(360.0f);
        ImGui_InputText("##command_host", p->host, sizeof p->host, 0);
    }
    ImGui_Text("Model");
    ImGui_SetNextItemWidth(360.0f);
    ImGui_InputText("##command_model", p->model, sizeof p->model, 0);
    if (codex) {
        ImGui_Text("Account: %s", editor_codex_authenticated() ? "Signed in" : "Signed out");
        if (!p->login && !editor_codex_authenticated() && ImGui_Button("Sign in")) {
            p->login = editor_codex_login_start(p->message, sizeof p->message);
            if (p->login) {
                snprintf(p->message, sizeof p->message, "Complete sign-in in your browser");
            }
        }
        if (!p->login && editor_codex_authenticated() && ImGui_Button("Sign out")) {
            editor_codex_sign_out();
            p->message[0] = 0;
        }
    } else {
        bool json_schema = editor_command_model_json_schema();
        if (ImGui_Checkbox("JSON schema", &json_schema)) {
            editor_command_model_set_json_schema(json_schema);
        }
    }
    if (p->message[0]) {
        ImGui_TextWrapped("%s", p->message);
    }
    if (ImGui_Button("Save")) {
        char error[128];
        if (editor_command_model_set_settings(p->host, p->model, error, sizeof error)) {
            p->message[0] = 0;
            ImGui_CloseCurrentPopup();
        } else {
            snprintf(p->message, sizeof p->message, "%s", error);
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("Cancel")) {
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
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
    bool running = !strcmp(status, "pending");
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
    if (p->focus) {
        ImGui_SetNextWindowFocus();
    }
    ImGui_Begin(
        "Commands",
        NULL,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
    );
    if (p->focus) {
        ImGui_SetKeyboardFocusHere();
        p->focus = false;
    }
    draw_settings(p);
    ImGui_BeginDisabled(running);
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x - 30.0f);
    bool submit = ImGui_InputTextWithHint(
        "##command",
        "What would you like to do?",
        p->query,
        sizeof p->query,
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui_SameLine();
    if (editor_ui_icon_button("commands.settings", EDITOR_ICON_FA_COG, true)) {
        editor_command_model_get_settings(
            p->host,
            sizeof p->host,
            p->model,
            sizeof p->model
        );
        p->settings_requested = true;
    }
    bool edited = ImGui_IsItemEdited();
    ImGui_EndDisabled();
    if (edited && (clarify || review)) {
        send(scene, "cancel", NULL, NULL);
        clarify = review = false;
    }
    const char *message = p->message[0] ? p->message : string(root, "message");
    if (*message) {
        ImGui_TextWrapped("%s", message);
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
                ImGui_PopID();
                if (picked || (submit && p->choice == i)) {
                    send(scene, "respond", "value", string(choice, "id"));
                    submit = false;
                }
            }
        } else if (*string(root, "binding")) {
            ImGui_SetNextItemWidth(-1);
            bool entered = ImGui_InputText("Value", p->reply, sizeof p->reply, ImGuiInputTextFlags_EnterReturnsTrue);
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
    }
    ImGui_BeginDisabled(running || !p->query[0]);
    bool pressed = ImGui_Button("Run");
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
    }
    ImGui_SameLine();
    if (ImGui_Button(running ? "Cancel" : "Close")) {
        send(scene, "cancel", NULL, NULL);
        p->open = false;
    }
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
