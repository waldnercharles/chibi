#ifndef CHIBI_UI_RML_BACKEND_H
#define CHIBI_UI_RML_BACKEND_H

#include "runtime/ui/font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rml_backend rml_backend;
    /* Borrowed straight RGBA8 bytes, valid for the synchronous load call.
     * Prepared backends reject absent resources without filesystem fallback. */
    typedef bool (*rml_image_provider)(void *, const char *, const unsigned char **, int *, int *);
    void rml_backend_set_image_provider(rml_image_provider, void *);
    void rml_backend_require_resident_images(rml_backend *, bool);
    /* Initializes only RmlUi/font/native-element state, without a GPU backend.
     * Release is a host shutdown operation and requires no live backends. */
    bool rml_backend_prepare_fonts();
    void rml_backend_release_prepared_fonts();
    /* Successful faces remain owned until the final backend shuts RmlUi down.
     * Existing face identities only accept identical bytes and fallback policy. */
    bool rml_backend_install_font(const ui_font *, char *error, size_t);
    bool rml_backend_install_font_budget(const ui_font *, size_t *remaining, char *error, size_t);

    typedef struct rml_backend_stats
    {
        uint64_t geometry_compiles;
        uint64_t uploaded_bytes;
        uint64_t render_passes;
        uint64_t static_layer_updates;
        uint64_t static_partial_updates;
        uint64_t static_commands_replayed; /* Rml paint commands queued into static targets. */
        uint64_t output_updates;
        uint64_t texture_composites;
        uint64_t cached_texture_bytes; /* Current static layer storage. */
        uint64_t context_updates;
        uint64_t render_captures;
        uint64_t retained_frames; /* Frames skipping both context update and capture. */
    } rml_backend_stats;

    typedef enum rml_input_type
    {
        RML_INPUT_POINTER_MOVE,
        RML_INPUT_POINTER_LEAVE,
        RML_INPUT_POINTER_DOWN,
        RML_INPUT_POINTER_UP,
        RML_INPUT_POINTER_WHEEL,
        RML_INPUT_KEY_PRESS,
        RML_INPUT_KEY_RELEASE,
        RML_INPUT_TEXT_INPUT,
    } rml_input_type;

    /* Values match RmlUi 6.3's Input::KeyIdentifier. */
    typedef enum rml_input_key
    {
        RML_INPUT_KEY_UNKNOWN = 0,
        RML_INPUT_KEY_SPACE = 1,
        RML_INPUT_KEY_0 = 2,
        RML_INPUT_KEY_1 = 3,
        RML_INPUT_KEY_2 = 4,
        RML_INPUT_KEY_3 = 5,
        RML_INPUT_KEY_4 = 6,
        RML_INPUT_KEY_5 = 7,
        RML_INPUT_KEY_6 = 8,
        RML_INPUT_KEY_7 = 9,
        RML_INPUT_KEY_8 = 10,
        RML_INPUT_KEY_9 = 11,
        RML_INPUT_KEY_A = 12,
        RML_INPUT_KEY_B = 13,
        RML_INPUT_KEY_C = 14,
        RML_INPUT_KEY_D = 15,
        RML_INPUT_KEY_E = 16,
        RML_INPUT_KEY_F = 17,
        RML_INPUT_KEY_G = 18,
        RML_INPUT_KEY_H = 19,
        RML_INPUT_KEY_I = 20,
        RML_INPUT_KEY_J = 21,
        RML_INPUT_KEY_K = 22,
        RML_INPUT_KEY_L = 23,
        RML_INPUT_KEY_M = 24,
        RML_INPUT_KEY_N = 25,
        RML_INPUT_KEY_O = 26,
        RML_INPUT_KEY_P = 27,
        RML_INPUT_KEY_Q = 28,
        RML_INPUT_KEY_R = 29,
        RML_INPUT_KEY_S = 30,
        RML_INPUT_KEY_T = 31,
        RML_INPUT_KEY_U = 32,
        RML_INPUT_KEY_V = 33,
        RML_INPUT_KEY_W = 34,
        RML_INPUT_KEY_X = 35,
        RML_INPUT_KEY_Y = 36,
        RML_INPUT_KEY_Z = 37,
        RML_INPUT_KEY_OEM_1 = 38, /* ; */
        RML_INPUT_KEY_OEM_PLUS = 39,
        RML_INPUT_KEY_OEM_COMMA = 40,
        RML_INPUT_KEY_OEM_MINUS = 41,
        RML_INPUT_KEY_OEM_PERIOD = 42,
        RML_INPUT_KEY_OEM_2 = 43, /* / */
        RML_INPUT_KEY_OEM_4 = 45, /* [ */
        RML_INPUT_KEY_OEM_6 = 47, /* ] */
        RML_INPUT_KEY_NUMPAD_ENTER = 61,
        RML_INPUT_KEY_SUBTRACT = 65,
        RML_INPUT_KEY_BACKSPACE = 69,
        RML_INPUT_KEY_TAB = 70,
        RML_INPUT_KEY_ENTER = 72,
        RML_INPUT_KEY_ESCAPE = 81,
        RML_INPUT_KEY_PAGE_UP = 86,
        RML_INPUT_KEY_PAGE_DOWN = 87,
        RML_INPUT_KEY_END = 88,
        RML_INPUT_KEY_HOME = 89,
        RML_INPUT_KEY_LEFT = 90,
        RML_INPUT_KEY_UP = 91,
        RML_INPUT_KEY_RIGHT = 92,
        RML_INPUT_KEY_DOWN = 93,
        RML_INPUT_KEY_INSERT = 98,
        RML_INPUT_KEY_DELETE = 99,
        RML_INPUT_KEY_F1 = 107,
        RML_INPUT_KEY_F2 = 108,
        RML_INPUT_KEY_F3 = 109,
        RML_INPUT_KEY_F4 = 110,
        RML_INPUT_KEY_F5 = 111,
        RML_INPUT_KEY_F6 = 112,
        RML_INPUT_KEY_F7 = 113,
        RML_INPUT_KEY_F8 = 114,
        RML_INPUT_KEY_F9 = 115,
        RML_INPUT_KEY_F10 = 116,
        RML_INPUT_KEY_F11 = 117,
        RML_INPUT_KEY_F12 = 118,
        RML_INPUT_KEY_LEFT_SHIFT = 138,
        RML_INPUT_KEY_RIGHT_SHIFT = 139,
        RML_INPUT_KEY_LEFT_CONTROL = 140,
        RML_INPUT_KEY_RIGHT_CONTROL = 141,
        RML_INPUT_KEY_LEFT_ALT = 142,
        RML_INPUT_KEY_RIGHT_ALT = 143,
        RML_INPUT_KEY_LEFT_META = 175,
        RML_INPUT_KEY_RIGHT_META = 176,
    } rml_input_key;

    typedef enum rml_input_modifier
    {
        RML_INPUT_MOD_CONTROL = 1 << 0,
        RML_INPUT_MOD_SHIFT = 1 << 1,
        RML_INPUT_MOD_ALT = 1 << 2,
        RML_INPUT_MOD_META = 1 << 3,
        RML_INPUT_MOD_CAPS_LOCK = 1 << 4,
        RML_INPUT_MOD_NUM_LOCK = 1 << 5,
        RML_INPUT_MOD_SCROLL_LOCK = 1 << 6,
    } rml_input_modifier;

    typedef enum rml_input_capture
    {
        RML_INPUT_CAPTURE_POINTER = 1 << 0,
        RML_INPUT_CAPTURE_KEYBOARD = 1 << 1,
        RML_INPUT_CAPTURE_TEXT = 1 << 2,
    } rml_input_capture;

    typedef struct rml_input
    {
        rml_input_type type;
        float x;
        float y;
        float wheel_x;
        float wheel_y;
        int button; /* Left 0, right 1, middle 2. */
        rml_input_key key;
        unsigned modifiers;
        const char *text; /* UTF-8, used by RML_INPUT_TEXT_INPUT. */
        size_t text_size;
    } rml_input;

    /* Sizes and pointer coordinates use logical window pixels. The context and its
     * returned GPU texture use physical pixels; framebuffer_scale sets RmlUi's dp ratio. */
    rml_backend *rml_backend_create(int logical_width, int logical_height, float framebuffer_scale);
    void rml_backend_destroy(rml_backend *backend);
    void rml_backend_resize(rml_backend *backend, int logical_width, int logical_height, float framebuffer_scale);
    /* Sizes the canvas in physical pixels with any dp ratio, as a zoomed canvas
     * needs; pointer input is then in physical / dp_ratio units. */
    void rml_backend_resize_pixels(rml_backend *backend, int physical_width, int physical_height, float dp_ratio);
    void rml_backend_update(rml_backend *backend, double delta_seconds);
    /* Called only from the live app's final draw phase, after a Cute GPU context exists. */
    void rml_backend_render(rml_backend *backend);
    /* Studio opt-in replacement for update + render, in the final draw phase.
     * Advances the shared clock even when retaining the previous draw commands.
     * dirty must cover DOM/model/kit/custom-renderer mutations and direct context
     * updates made through rml_backend_context(). Input, backend resource changes,
     * actual resize/DP changes and Rml deadlines wake automatically. Visible
     * kit-code elements keep full traversal until Zep supplies wake deadlines.
     * External handle/revision changes recompose retained commands in paint order;
     * otherwise the output and its revision stay unchanged. Borrowed host textures
     * must remain live through this call. Do not also call update for its delta. */
    void rml_backend_frame_retained(rml_backend *backend, double delta_seconds, bool dirty);
    /* Whether a render since the last call drew anything the render before it
     * did not, as an animation does each frame; then clears it. */
    bool rml_backend_take_changed(rml_backend *backend);
    /* CF texture handle for the transparent output canvas; zero until the first render. */
    uint64_t rml_backend_texture(const rml_backend *backend);
    /* Increases when the output pixels are redrawn, including a resized target. */
    uint64_t rml_backend_texture_revision(const rml_backend *backend);
    rml_backend_stats rml_backend_get_stats(const rml_backend *backend);
    /* Returns input consumed by RmlUi, as RML_INPUT_CAPTURE_* flags. */
    unsigned rml_backend_process_input(rml_backend *backend, const rml_input *input);
    /* Uses browser paste data for the next RmlUi clipboard read. The Studio
     * web bridge clears it at the end of the input frame if no field took it. */
    void rml_backend_set_pending_paste_text(const char *text);
    void rml_backend_clear_pending_paste_text();
    /* Opaque Rml::Context pointer for the C++ menu controller. Valid until backend_destroy. */
    void *rml_backend_context(rml_backend *backend);
    /* Bind a host texture to "kit:<name>" sources, e.g. the Scene render target. */
    void rml_backend_set_texture(rml_backend *backend, const char *name, uint64_t texture);
    /* Binds a host texture to "kit:<name>" sources as a still image, whose
     * pixels change only when it is bound again, as a thumbnail's do: it
     * draws in the retained paint as RmlUi's own images do, so many of them
     * cost no live runs, and each call paints again. 0 draws nothing. */
    void rml_backend_set_image(rml_backend *backend, const char *name, uint64_t texture);
    /* A retained producer supplies its content revision even when the GPU handle
     * stays the same. The ordinary setter treats each call as new content. */
    void rml_backend_set_texture_revision(
        rml_backend *backend,
        const char *name,
        uint64_t texture,
        uint64_t revision
    );
    /* Cursor requested by the hovered element's RCSS `cursor` property. */
    const char *rml_backend_cursor(const rml_backend *backend);
    /* Queue the rendered UI over the app canvas; drawn by cf_app_draw_onto_screen. */
    void rml_backend_present(rml_backend *backend);
    /* The same for one region of it, x0 y0 x1 y1 as fractions of its width
     * and height from the top left, stretched over the canvas. */
    void rml_backend_present_region(rml_backend *backend, const float region[4]);
    /* The first warning or error RmlUi logged since the last call, or "" when
     * it logged none; a caller brackets an operation with two calls. */
    const char *rml_backend_take_warning();
    /* How many warnings and errors RmlUi logged since the process began,
     * which no take resets: tests and benches watch it for any or a flood. */
    int rml_backend_warning_count();

    /* ---------- UI language service (workstream B) ---------- */

    /* Keeps every warning and error RmlUi logs from now on, for a check that
     * reports them all. */
    void rml_backend_keep_warnings();
    /* The warnings kept since rml_backend_keep_warnings, in order, as an owned
     * support/array.h array of owned strings; NULL when there were none.
     * Stops keeping them. */
    char **rml_backend_take_warnings();

#ifdef __cplusplus
}
#endif

#endif
