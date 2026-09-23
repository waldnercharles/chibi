#include "editor/ui/retained_menu.h"

#include "editor/rmlui/backend.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdlib.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

#define MENU_POPUP_ID ((ImGuiID)0x5e7a2d43u)

typedef struct
{
    ImDrawList *draw;
    editor_rml_menu_rect rect;
} menu_surface;

static editor_rml_backend *backend;
static editor_rml_menu *menu;
static char **actions;
static char *last_action;
static int action_index;
static bool keyboard_captured;
static bool restore_focus;
static bool keyboard_action;
static ImGuiID bar_window_id;
static ImGuiID return_window_id;
static ImGuiID return_item_id;
static ImGuiID return_focus_scope;
static ImGuiNavLayer return_nav_layer;
static ImRect return_nav_rect;
static ImVec2 origin;
static ImVec2 size;
static menu_surface bar;
static menu_surface popup;

static void queue_action(void *, const char *id)
{
    apush(actions, xstrdup(id));
}

static unsigned modifiers()
{
    ImGuiIO *io = ImGui_GetIO();
    return (io->KeyCtrl ? EDITOR_RML_MOD_CONTROL : 0) |
           (io->KeyShift ? EDITOR_RML_MOD_SHIFT : 0) |
           (io->KeyAlt ? EDITOR_RML_MOD_ALT : 0) |
           (io->KeySuper ? EDITOR_RML_MOD_META : 0);
}

static void key_input(ImGuiKey native, editor_rml_key key, bool allow)
{
    if (!allow) {
        return;
    }
    bool down = ImGui_IsKeyPressedEx(native, true);
    bool up = ImGui_IsKeyReleased(native);
    if (!down && !up) {
        return;
    }
    keyboard_action |= down;
    bool enter_bar = native == ImGuiKey_F10 || native == ImGuiKey_LeftAlt ||
                     native == ImGuiKey_RightAlt;
    if (down && enter_bar && !editor_rml_menu_captures_keyboard(menu)) {
        ImGui_ClearActiveID();
        editor_rml_menu_focus_bar(menu);
    }
    editor_rml_input input = {
        .type = down ? EDITOR_RML_KEY_PRESS : EDITOR_RML_KEY_RELEASE,
        .key = key,
        .modifiers = modifiers(),
    };
    editor_rml_backend_process_input(backend, &input);
    if (keyboard_captured || editor_rml_menu_captures_keyboard(menu)) {
        ImGui_SetKeyOwner(native, MENU_POPUP_ID, ImGuiInputFlags_LockThisFrame);
    }
}

static bool contains(editor_rml_menu_rect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
           y < rect.y + rect.height;
}

static void menu_input()
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiContext *context = ImGui_GetCurrentContext();
    int count = context->OpenPopupStack.Size;
    bool other_popup = count && context->OpenPopupStack.Data[count - 1].PopupId !=
                                    MENU_POPUP_ID;
    keyboard_captured = editor_rml_menu_captures_keyboard(menu);
    restore_focus = false;
    keyboard_action = false;
    if (!keyboard_captured && context->NavWindow &&
        context->NavWindow->ID != bar_window_id && !other_popup) {
        return_window_id = context->NavWindow->ID;
        return_item_id = context->NavId;
        return_focus_scope = context->NavFocusScopeId;
        return_nav_layer = context->NavLayer;
        return_nav_rect = context->NavWindow->NavRectRel[context->NavLayer];
    }
    if (other_popup || io->AppFocusLost) {
        editor_rml_menu_close(menu);
        keyboard_captured = false;
        return;
    }
    editor_rml_input input = {
        .type = EDITOR_RML_POINTER_MOVE,
        .x = io->MousePos.x - origin.x,
        .y = io->MousePos.y - origin.y,
        .modifiers = modifiers(),
    };
    if (io->MousePos.x <= -FLT_MAX / 2) {
        input.type = EDITOR_RML_POINTER_LEAVE;
    }
    editor_rml_backend_process_input(backend, &input);
    bool outside_click = false;
    if (io->MouseClicked[0] || io->MouseClicked[1]) {
        editor_rml_menu_rect rect;
        bool over_bar = contains(
            (editor_rml_menu_rect){ 0, 0, size.x, editor_rml_menu_bar_height(menu) },
            input.x,
            input.y
        );
        bool over_popup = editor_rml_menu_popup_rect(menu, &rect) &&
                          contains(rect, input.x, input.y);
        if (!over_bar && !over_popup) {
            outside_click = true;
            editor_rml_menu_close(menu);
        }
    }
    for (int button = 0; button < 3; button++) {
        input.button = button;
        if (io->MouseClicked[button]) {
            input.type = EDITOR_RML_POINTER_DOWN;
            editor_rml_backend_process_input(backend, &input);
        }
        if (io->MouseReleased[button]) {
            input.type = EDITOR_RML_POINTER_UP;
            editor_rml_backend_process_input(backend, &input);
        }
    }
    if (io->MouseWheel || io->MouseWheelH) {
        input.type = EDITOR_RML_POINTER_WHEEL;
        input.wheel_x = -io->MouseWheelH;
        input.wheel_y = -io->MouseWheel;
        editor_rml_backend_process_input(backend, &input);
    }
    static const struct
    {
        ImGuiKey native;
        editor_rml_key key;
    } keys[] = {
        { ImGuiKey_F10, EDITOR_RML_KEY_F10 },
        { ImGuiKey_LeftAlt, EDITOR_RML_KEY_LEFT_ALT },
        { ImGuiKey_RightAlt, EDITOR_RML_KEY_RIGHT_ALT },
        { ImGuiKey_Tab, EDITOR_RML_KEY_TAB },
        { ImGuiKey_Enter, EDITOR_RML_KEY_ENTER },
        { ImGuiKey_Space, EDITOR_RML_KEY_SPACE },
        { ImGuiKey_Escape, EDITOR_RML_KEY_ESCAPE },
        { ImGuiKey_LeftArrow, EDITOR_RML_KEY_LEFT },
        { ImGuiKey_RightArrow, EDITOR_RML_KEY_RIGHT },
        { ImGuiKey_UpArrow, EDITOR_RML_KEY_UP },
        { ImGuiKey_DownArrow, EDITOR_RML_KEY_DOWN },
        { ImGuiKey_Home, EDITOR_RML_KEY_HOME },
        { ImGuiKey_End, EDITOR_RML_KEY_END },
    };
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        bool active = keyboard_captured || editor_rml_menu_captures_keyboard(menu);
        bool entry = i == 0 ||
                     (i < 3 && !io->ConfigMacOSXBehaviors && !io->WantTextInput);
        key_input(keys[i].native, keys[i].key, entry || active);
    }
    for (int i = 0; i < 26; i++) {
        key_input(
            ImGuiKey_A + i,
            EDITOR_RML_KEY_A + i,
            keyboard_captured || editor_rml_menu_captures_keyboard(menu)
        );
    }
    bool active = editor_rml_menu_captures_keyboard(menu);
    restore_focus = keyboard_captured && !active && !outside_click;
    keyboard_captured |= active;
    if (keyboard_captured) {
        context->NavActivateId = context->NavActivateDownId = context->NavActivatePressedId = 0;
        ImGui_NavMoveRequestCancel();
    }
}

static void menu_windows()
{
    const int flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoFocusOnAppearing;
    bar = (menu_surface){ .rect = { 0, 0, size.x, editor_rml_menu_bar_height(menu) } };
    popup = (menu_surface){ 0 };
    ImGui_SetNextWindowPos(origin, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ bar.rect.width, bar.rect.height }, ImGuiCond_Always);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0, 0 });
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowMinSize, (ImVec2){ 1, 1 });
    ImGui_PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui_PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
    ImGui_Begin("##retained_menu_bar", NULL, flags);
    bar_window_id = ImGui_GetCurrentWindow()->ID;
    bar.draw = ImGui_GetWindowDrawList();
    bool open = editor_rml_menu_popup_rect(menu, &popup.rect);
    if (open) {
        if (!ImGui_IsPopupOpenID(MENU_POPUP_ID, ImGuiPopupFlags_None)) {
            ImGui_OpenPopupEx(MENU_POPUP_ID);
        }
        ImGui_SetNextWindowPos(
            (ImVec2){ origin.x + popup.rect.x, origin.y + popup.rect.y },
            ImGuiCond_Always
        );
        ImGui_SetNextWindowSize((ImVec2){ popup.rect.width, popup.rect.height }, ImGuiCond_Always);
    }
    if (ImGui_BeginPopupEx(MENU_POPUP_ID, flags)) {
        if (open) {
            popup.draw = ImGui_GetWindowDrawList();
        } else {
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
    ImGui_End();
    ImGui_PopStyleVarEx(4);
    if (restore_focus && return_window_id) {
        ImGuiWindow *window = ImGui_FindWindowByID(return_window_id);
        if (window && window->WasActive) {
            ImGui_FocusWindow(window, ImGuiFocusRequestFlags_UnlessBelowModal);
            ImGui_SetNavID(return_item_id, return_nav_layer, return_focus_scope, return_nav_rect);
            if (keyboard_action) {
                ImGui_GetCurrentContext()->NavCursorVisible = true;
            }
        }
    }
}

void editor_menu_frame(const editor_rml_menu_state *state)
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiViewport *viewport = ImGui_GetMainViewport();
    origin = viewport->Pos;
    size = (ImVec2){ fmaxf(1, io->DisplaySize.x), fmaxf(1, io->DisplaySize.y) };
    int width = (int)size.x;
    int height = (int)size.y;
    float scale = io->DisplayFramebufferScale.x > 0
                    ? io->DisplayFramebufferScale.x
                    : 1;
    if (!backend) {
        backend = editor_rml_backend_create(width, height, scale);
        assert(backend);
        menu = editor_rml_menu_create(editor_rml_backend_context(backend), queue_action, NULL);
        assert(menu);
    }
    editor_rml_backend_resize(backend, width, height, scale);
    editor_rml_menu_set_state(menu, state);
    editor_rml_backend_update(backend, io->DeltaTime);
    menu_input();
    editor_rml_backend_update(backend, 0);
    menu_windows();
}

const char *editor_menu_take_action()
{
    free(last_action);
    last_action = NULL;
    if (action_index < acount(actions)) {
        last_action = actions[action_index++];
        return last_action;
    }
    afree(actions);
    action_index = 0;
    return NULL;
}

float editor_menu_height()
{
    return menu ? editor_rml_menu_bar_height(menu) : 0;
}

bool editor_menu_captures_keyboard()
{
    return menu && (keyboard_captured || editor_rml_menu_captures_keyboard(menu));
}

bool editor_menu_item_rect(const char *id, editor_rml_menu_rect *out)
{
    if (!menu || !editor_rml_menu_item_rect(menu, id, out)) {
        return false;
    }
    out->x += origin.x;
    out->y += origin.y;
    return true;
}

static void compose(menu_surface surface, ImTextureRef texture)
{
    if (!surface.draw) {
        return;
    }
    editor_rml_menu_rect r = surface.rect;
    ImDrawList_AddImageEx(
        surface.draw,
        texture,
        (ImVec2){ origin.x + r.x, origin.y + r.y },
        (ImVec2){ origin.x + r.x + r.width, origin.y + r.y + r.height },
        (ImVec2){ r.x / size.x, r.y / size.y },
        (ImVec2){ (r.x + r.width) / size.x, (r.y + r.height) / size.y },
        0xffffffffu
    );
}

void editor_menu_render()
{
    if (!backend) {
        return;
    }
    editor_rml_backend_render(backend);
    CF_Texture target = { .id = editor_rml_backend_texture(backend) };
    ImTextureRef texture = { ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target) };
    compose(bar, texture);
    compose(popup, texture);
}

void editor_menu_shutdown()
{
    if (!backend) {
        return;
    }
    editor_rml_menu_destroy(menu);
    editor_rml_backend_destroy(backend);
    menu = NULL;
    backend = NULL;
    while (editor_menu_take_action()) {
    }
    keyboard_captured = false;
    restore_focus = false;
    bar_window_id = return_window_id = return_item_id = 0;
    bar = popup = (menu_surface){ 0 };
}
