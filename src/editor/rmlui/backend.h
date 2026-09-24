#ifndef CHIBI_EDITOR_RMLUI_BACKEND_H
#define CHIBI_EDITOR_RMLUI_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct editor_rml_backend editor_rml_backend;

    typedef enum editor_rml_input_type
    {
        EDITOR_RML_POINTER_MOVE,
        EDITOR_RML_POINTER_LEAVE,
        EDITOR_RML_POINTER_DOWN,
        EDITOR_RML_POINTER_UP,
        EDITOR_RML_POINTER_WHEEL,
        EDITOR_RML_KEY_PRESS,
        EDITOR_RML_KEY_RELEASE,
        EDITOR_RML_TEXT_INPUT,
    } editor_rml_input_type;

    /* Values match RmlUi 6.3's Input::KeyIdentifier. */
    typedef enum editor_rml_key
    {
        EDITOR_RML_KEY_UNKNOWN = 0,
        EDITOR_RML_KEY_SPACE = 1,
        EDITOR_RML_KEY_0 = 2,
        EDITOR_RML_KEY_1 = 3,
        EDITOR_RML_KEY_2 = 4,
        EDITOR_RML_KEY_3 = 5,
        EDITOR_RML_KEY_4 = 6,
        EDITOR_RML_KEY_5 = 7,
        EDITOR_RML_KEY_6 = 8,
        EDITOR_RML_KEY_7 = 9,
        EDITOR_RML_KEY_8 = 10,
        EDITOR_RML_KEY_9 = 11,
        EDITOR_RML_KEY_A = 12,
        EDITOR_RML_KEY_B = 13,
        EDITOR_RML_KEY_C = 14,
        EDITOR_RML_KEY_D = 15,
        EDITOR_RML_KEY_E = 16,
        EDITOR_RML_KEY_F = 17,
        EDITOR_RML_KEY_G = 18,
        EDITOR_RML_KEY_H = 19,
        EDITOR_RML_KEY_I = 20,
        EDITOR_RML_KEY_J = 21,
        EDITOR_RML_KEY_K = 22,
        EDITOR_RML_KEY_L = 23,
        EDITOR_RML_KEY_M = 24,
        EDITOR_RML_KEY_N = 25,
        EDITOR_RML_KEY_O = 26,
        EDITOR_RML_KEY_P = 27,
        EDITOR_RML_KEY_Q = 28,
        EDITOR_RML_KEY_R = 29,
        EDITOR_RML_KEY_S = 30,
        EDITOR_RML_KEY_T = 31,
        EDITOR_RML_KEY_U = 32,
        EDITOR_RML_KEY_V = 33,
        EDITOR_RML_KEY_W = 34,
        EDITOR_RML_KEY_X = 35,
        EDITOR_RML_KEY_Y = 36,
        EDITOR_RML_KEY_Z = 37,
        EDITOR_RML_KEY_OEM_PLUS = 39,
        EDITOR_RML_KEY_OEM_MINUS = 41,
        EDITOR_RML_KEY_OEM_PERIOD = 42,
        EDITOR_RML_KEY_NUMPAD_ENTER = 61,
        EDITOR_RML_KEY_SUBTRACT = 65,
        EDITOR_RML_KEY_BACKSPACE = 69,
        EDITOR_RML_KEY_TAB = 70,
        EDITOR_RML_KEY_ENTER = 72,
        EDITOR_RML_KEY_ESCAPE = 81,
        EDITOR_RML_KEY_PAGE_UP = 86,
        EDITOR_RML_KEY_PAGE_DOWN = 87,
        EDITOR_RML_KEY_END = 88,
        EDITOR_RML_KEY_HOME = 89,
        EDITOR_RML_KEY_LEFT = 90,
        EDITOR_RML_KEY_UP = 91,
        EDITOR_RML_KEY_RIGHT = 92,
        EDITOR_RML_KEY_DOWN = 93,
        EDITOR_RML_KEY_INSERT = 98,
        EDITOR_RML_KEY_DELETE = 99,
        EDITOR_RML_KEY_F1 = 107,
        EDITOR_RML_KEY_F2 = 108,
        EDITOR_RML_KEY_F3 = 109,
        EDITOR_RML_KEY_F4 = 110,
        EDITOR_RML_KEY_F5 = 111,
        EDITOR_RML_KEY_F6 = 112,
        EDITOR_RML_KEY_F7 = 113,
        EDITOR_RML_KEY_F8 = 114,
        EDITOR_RML_KEY_F9 = 115,
        EDITOR_RML_KEY_F10 = 116,
        EDITOR_RML_KEY_F11 = 117,
        EDITOR_RML_KEY_F12 = 118,
        EDITOR_RML_KEY_LEFT_SHIFT = 138,
        EDITOR_RML_KEY_RIGHT_SHIFT = 139,
        EDITOR_RML_KEY_LEFT_CONTROL = 140,
        EDITOR_RML_KEY_RIGHT_CONTROL = 141,
        EDITOR_RML_KEY_LEFT_ALT = 142,
        EDITOR_RML_KEY_RIGHT_ALT = 143,
        EDITOR_RML_KEY_LEFT_META = 175,
        EDITOR_RML_KEY_RIGHT_META = 176,
    } editor_rml_key;

    typedef enum editor_rml_modifier
    {
        EDITOR_RML_MOD_CONTROL = 1 << 0,
        EDITOR_RML_MOD_SHIFT = 1 << 1,
        EDITOR_RML_MOD_ALT = 1 << 2,
        EDITOR_RML_MOD_META = 1 << 3,
        EDITOR_RML_MOD_CAPS_LOCK = 1 << 4,
        EDITOR_RML_MOD_NUM_LOCK = 1 << 5,
        EDITOR_RML_MOD_SCROLL_LOCK = 1 << 6,
    } editor_rml_modifier;

    typedef enum editor_rml_capture
    {
        EDITOR_RML_CAPTURE_POINTER = 1 << 0,
        EDITOR_RML_CAPTURE_KEYBOARD = 1 << 1,
        EDITOR_RML_CAPTURE_TEXT = 1 << 2,
    } editor_rml_capture;

    typedef struct editor_rml_input
    {
        editor_rml_input_type type;
        float x;
        float y;
        float wheel_x;
        float wheel_y;
        int button; /* Left 0, right 1, middle 2. */
        editor_rml_key key;
        unsigned modifiers;
        const char *text; /* UTF-8, used by EDITOR_RML_TEXT_INPUT. */
        size_t text_size;
    } editor_rml_input;

    /* Sizes and pointer coordinates use logical window pixels. The context and its
     * returned GPU texture use physical pixels; framebuffer_scale sets RmlUi's dp ratio. */
    editor_rml_backend *editor_rml_backend_create(int logical_width, int logical_height, float framebuffer_scale);
    void editor_rml_backend_destroy(editor_rml_backend *backend);
    void editor_rml_backend_resize(
        editor_rml_backend *backend,
        int logical_width,
        int logical_height,
        float framebuffer_scale
    );
    void editor_rml_backend_update(editor_rml_backend *backend, double delta_seconds);
    /* Called only from the live app's final draw phase, after a Cute GPU context exists. */
    void editor_rml_backend_render(editor_rml_backend *backend);
    /* CF texture handle for the transparent output canvas; zero until the first render. */
    uint64_t editor_rml_backend_texture(const editor_rml_backend *backend);
    /* Returns input consumed by RmlUi, as EDITOR_RML_CAPTURE_* flags. */
    unsigned editor_rml_backend_process_input(editor_rml_backend *backend, const editor_rml_input *input);
    /* Opaque Rml::Context pointer for the C++ menu controller. Valid until backend_destroy. */
    void *editor_rml_backend_context(editor_rml_backend *backend);
    /* Bind a host texture to "kit:<name>" sources, e.g. the Scene render target. */
    void editor_rml_backend_set_texture(editor_rml_backend *backend, const char *name, uint64_t texture);
    /* Cursor requested by the hovered element's RCSS `cursor` property. */
    const char *editor_rml_backend_cursor(const editor_rml_backend *backend);
    /* Queue the rendered UI over the app canvas; drawn by cf_app_draw_onto_screen. */
    void editor_rml_backend_present(editor_rml_backend *backend);

#ifdef __cplusplus
}
#endif

#endif
