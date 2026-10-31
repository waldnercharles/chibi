#ifndef CHIBI_UI_RML_H
#define CHIBI_UI_RML_H

/* A C binding for RmlUi 6.3. It is pure pass-through: rml.cpp only converts
 * types and forwards calls to RmlUi. Keep editor logic, state machines and
 * policy out of it; they belong in C on top of it.
 *
 * Handles are the RmlUi objects themselves, and a document is an rml_element.
 * Enum values match RmlUi's, which rml.cpp checks. A string RmlUi returns by
 * value is copied as snprintf does: the call returns its full length and
 * writes at most size - 1 bytes and a NUL. */

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rml_context rml_context;
    typedef struct rml_element rml_element;
    typedef struct rml_listener rml_listener;
    typedef struct rml_renderer rml_renderer;
    typedef struct rml_instancer rml_instancer;
    typedef struct rml_render_manager rml_render_manager;
    typedef struct rml_geometry rml_geometry;
    typedef struct rml_string rml_string;
    typedef struct rml_data_model rml_data_model;

    typedef struct rml_vec2
    {
        float x;
        float y;
    } rml_vec2;

    typedef struct rml_rect
    {
        int x;
        int y;
        int width;
        int height;
    } rml_rect;

    typedef struct rml_color
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    } rml_color;

    /* Rml::Vertex; the color is premultiplied. */
    typedef struct rml_vertex
    {
        rml_vec2 position;
        rml_color color;
        rml_vec2 tex_coord;
    } rml_vertex;

    typedef struct rml_mesh
    {
        const rml_vertex *vertices;
        size_t vertex_count;
        const int *indices;
        size_t index_count;
    } rml_mesh;

    typedef enum rml_clip_mask_operation
    {
        RML_CLIP_MASK_SET,
        RML_CLIP_MASK_SET_INVERSE,
        RML_CLIP_MASK_INTERSECT,
    } rml_clip_mask_operation;

    /* A gradient's Rml::ColorStop, at a normalized position; the color is
     * premultiplied. */
    typedef struct rml_color_stop
    {
        float position;
        rml_color color;
    } rml_color_stop;

    /* The Rml::Dictionary a filter or shader compiles from. A key RmlUi did
     * not pass reads as zero. */
    typedef struct rml_effect_parameters
    {
        float value;     /* opacity and the color filters */
        float sigma;     /* blur, drop-shadow */
        float angle;     /* conic-gradient, radians */
        rml_vec2 offset; /* drop-shadow */
        rml_color color; /* drop-shadow, straight alpha */
        rml_vec2 p0, p1; /* linear-gradient */
        rml_vec2 center; /* radial- and conic-gradient */
        rml_vec2 radius; /* radial-gradient */
        bool repeating;
        const rml_color_stop *stops;
        size_t stop_count;
    } rml_effect_parameters;

    typedef enum rml_blend_mode
    {
        RML_BLEND_MODE_BLEND,
        RML_BLEND_MODE_REPLACE,
    } rml_blend_mode;

    typedef struct rml_transition
    {
        float duration;
        float delay;
    } rml_transition;

    /* Input::KeyIdentifier, the keys the editor names. */
    typedef enum rml_key
    {
        RML_KEY_UNKNOWN = 0,
        RML_KEY_SPACE = 1,
        RML_KEY_0 = 2,
        RML_KEY_1 = 3,
        RML_KEY_2 = 4,
        RML_KEY_3 = 5,
        RML_KEY_4 = 6,
        RML_KEY_5 = 7,
        RML_KEY_6 = 8,
        RML_KEY_7 = 9,
        RML_KEY_8 = 10,
        RML_KEY_9 = 11,
        RML_KEY_A = 12,
        RML_KEY_B = 13,
        RML_KEY_C = 14,
        RML_KEY_D = 15,
        RML_KEY_E = 16,
        RML_KEY_F = 17,
        RML_KEY_G = 18,
        RML_KEY_H = 19,
        RML_KEY_I = 20,
        RML_KEY_J = 21,
        RML_KEY_K = 22,
        RML_KEY_L = 23,
        RML_KEY_M = 24,
        RML_KEY_N = 25,
        RML_KEY_O = 26,
        RML_KEY_P = 27,
        RML_KEY_Q = 28,
        RML_KEY_R = 29,
        RML_KEY_S = 30,
        RML_KEY_T = 31,
        RML_KEY_U = 32,
        RML_KEY_V = 33,
        RML_KEY_W = 34,
        RML_KEY_X = 35,
        RML_KEY_Y = 36,
        RML_KEY_Z = 37,
        RML_KEY_OEM_PLUS = 39,
        RML_KEY_OEM_MINUS = 41,
        RML_KEY_OEM_PERIOD = 42,
        RML_KEY_NUMPADENTER = 61,
        RML_KEY_SUBTRACT = 65,
        RML_KEY_BACK = 69,
        RML_KEY_TAB = 70,
        RML_KEY_RETURN = 72,
        RML_KEY_ESCAPE = 81,
        RML_KEY_PRIOR = 86,
        RML_KEY_NEXT = 87,
        RML_KEY_END = 88,
        RML_KEY_HOME = 89,
        RML_KEY_LEFT = 90,
        RML_KEY_UP = 91,
        RML_KEY_RIGHT = 92,
        RML_KEY_DOWN = 93,
        RML_KEY_INSERT = 98,
        RML_KEY_DELETE = 99,
        RML_KEY_F1 = 107,
        RML_KEY_F2 = 108,
        RML_KEY_F3 = 109,
        RML_KEY_F4 = 110,
        RML_KEY_F5 = 111,
        RML_KEY_F6 = 112,
        RML_KEY_F7 = 113,
        RML_KEY_F8 = 114,
        RML_KEY_F9 = 115,
        RML_KEY_F10 = 116,
        RML_KEY_F11 = 117,
        RML_KEY_F12 = 118,
        RML_KEY_LSHIFT = 138,
        RML_KEY_RSHIFT = 139,
        RML_KEY_LCONTROL = 140,
        RML_KEY_RCONTROL = 141,
        RML_KEY_LMENU = 142,
        RML_KEY_RMENU = 143,
        RML_KEY_LMETA = 175,
        RML_KEY_RMETA = 176,
    } rml_key;

    /* Input::KeyModifier bits. */
    typedef enum rml_key_modifier
    {
        RML_KM_CTRL = 1 << 0,
        RML_KM_SHIFT = 1 << 1,
        RML_KM_ALT = 1 << 2,
        RML_KM_META = 1 << 3,
        RML_KM_CAPSLOCK = 1 << 4,
        RML_KM_NUMLOCK = 1 << 5,
        RML_KM_SCROLLLOCK = 1 << 6,
    } rml_key_modifier;

    typedef enum rml_event_id
    {
        RML_EVENT_INVALID,
        RML_EVENT_MOUSEDOWN,
        RML_EVENT_MOUSESCROLL,
        RML_EVENT_MOUSEOVER,
        RML_EVENT_MOUSEOUT,
        RML_EVENT_FOCUS,
        RML_EVENT_BLUR,
        RML_EVENT_KEYDOWN,
        RML_EVENT_KEYUP,
        RML_EVENT_TEXTINPUT,
        RML_EVENT_MOUSEUP,
        RML_EVENT_CLICK,
        RML_EVENT_DBLCLICK,
        RML_EVENT_LOAD,
        RML_EVENT_UNLOAD,
        RML_EVENT_SHOW,
        RML_EVENT_HIDE,
        RML_EVENT_MOUSEMOVE,
        RML_EVENT_DRAGMOVE,
        RML_EVENT_DRAG,
        RML_EVENT_DRAGSTART,
        RML_EVENT_DRAGOVER,
        RML_EVENT_DRAGDROP,
        RML_EVENT_DRAGOUT,
        RML_EVENT_DRAGEND,
        RML_EVENT_HANDLEDRAG,
        RML_EVENT_RESIZE,
        RML_EVENT_SCROLL,
        RML_EVENT_ANIMATIONEND,
        RML_EVENT_TRANSITIONEND,
        RML_EVENT_CHANGE,
        RML_EVENT_SUBMIT,
        RML_EVENT_TABCHANGE,
        /* A text field's edit, as HTML's; change follows once it commits
         * (third_party/rmlui/input-events.patch). */
        RML_EVENT_INPUT,
    } rml_event_id;

    typedef enum rml_box_area
    {
        RML_BOX_MARGIN,
        RML_BOX_BORDER,
        RML_BOX_PADDING,
        RML_BOX_CONTENT,
        RML_BOX_AUTO,
    } rml_box_area;

    typedef enum rml_scroll_alignment
    {
        RML_SCROLL_START,
        RML_SCROLL_CENTER,
        RML_SCROLL_END,
        RML_SCROLL_NEAREST,
        RML_SCROLL_ADAPTIVE,
    } rml_scroll_alignment;

    typedef enum rml_modal_flag
    {
        RML_MODAL_NONE,
        RML_MODAL_MODAL,
        RML_MODAL_KEEP,
    } rml_modal_flag;

    typedef enum rml_focus_flag
    {
        RML_FOCUS_NONE,
        RML_FOCUS_DOCUMENT,
        RML_FOCUS_KEEP,
        RML_FOCUS_AUTO,
    } rml_focus_flag;

    typedef enum rml_scroll_flag
    {
        RML_SCROLL_FLAG_NONE,
        RML_SCROLL_FLAG_AUTO,
    } rml_scroll_flag;

    typedef enum rml_font_style
    {
        RML_FONT_NORMAL,
        RML_FONT_ITALIC,
    } rml_font_style;

    typedef enum rml_log_type
    {
        RML_LOG_ALWAYS,
        RML_LOG_ERROR,
        RML_LOG_ASSERT,
        RML_LOG_WARNING,
        RML_LOG_INFO,
        RML_LOG_DEBUG,
    } rml_log_type;

    /* ---------- Interfaces the application implements ---------- */

    /* SystemInterface. Set before rml_initialize; it must outlive
     * rml_shutdown. */
    typedef struct rml_system_interface
    {
        double (*elapsed_time)();
        void (*set_mouse_cursor)(const char *name);
        void (*set_clipboard_text)(const char *text);
        void (*get_clipboard_text)(rml_string *text);
        bool (*log_message)(rml_log_type type, const char *message);
    } rml_system_interface;

    /* RenderInterface. Handles are the application's; zero means none. The
     * transform is 16 floats in column-major order, or NULL for identity. */
    typedef struct rml_render_interface
    {
        uintptr_t (*compile_geometry)(void *user, rml_mesh mesh);
        void (*render_geometry)(void *user, uintptr_t geometry, rml_vec2 translation, uintptr_t texture);
        void (*release_geometry)(void *user, uintptr_t geometry);
        uintptr_t (*load_texture)(void *user, const char *source, int *width, int *height);
        uintptr_t (*generate_texture)(void *user, const uint8_t *pixels, int width, int height);
        void (*release_texture)(void *user, uintptr_t texture);
        void (*enable_scissor_region)(void *user, bool enable);
        void (*set_scissor_region)(void *user, rml_rect region);
        void (*set_transform)(void *user, const float *transform);
        void (*enable_clip_mask)(void *user, bool enable);
        void (*render_to_clip_mask)(
            void *user,
            rml_clip_mask_operation operation,
            uintptr_t geometry,
            rml_vec2 translation
        );
        uintptr_t (*push_layer)(void *user);
        void (*composite_layers)(
            void *user,
            uintptr_t source,
            uintptr_t destination,
            rml_blend_mode blend_mode,
            const uintptr_t *filters,
            size_t filter_count
        );
        void (*pop_layer)(void *user);
        uintptr_t (*save_layer_as_texture)(void *user);
        /* Returns a filter handle that masks the next composite. */
        uintptr_t (*save_layer_as_mask_image)(void *user);
        uintptr_t (*compile_filter)(void *user, const char *name, const rml_effect_parameters *parameters);
        void (*release_filter)(void *user, uintptr_t filter);
        uintptr_t (*compile_shader)(void *user, const char *name, const rml_effect_parameters *parameters);
        void (*render_shader)(
            void *user,
            uintptr_t shader,
            uintptr_t geometry,
            rml_vec2 translation,
            uintptr_t texture
        );
        void (*release_shader)(void *user, uintptr_t shader);
    } rml_render_interface;

    /* A custom element's Element overrides; any may be NULL. release runs
     * when the element is destroyed. update runs at the start of each
     * context update, before the element's style and its children's are
     * computed, so what it sets shows in that frame (workstream N). layout
     * runs each time a layout places the element, whether or not it moved;
     * a document lays out all of its elements at once. attribute runs after
     * each of the element's attributes is set or removed, `name` naming it,
     * with the user data NULL until the element sets one (workstream F2). */
    typedef struct rml_element_hooks
    {
        void (*render)(rml_element *element, void *user);
        void (*resize)(rml_element *element, void *user);
        void (*release)(void *user);
        void (*update)(rml_element *element, void *user);
        void (*layout)(rml_element *element, void *user);
        void (*attribute)(rml_element *element, void *user, const char *name);
    } rml_element_hooks;

    /* Event and the parameters the editor reads. Valid during the callback. */
    typedef struct rml_event
    {
        rml_event_id id;
        rml_element *target;
        rml_element *current;
        rml_element *drag_element;
        float mouse_x;
        float mouse_y;
        int button;
        rml_key key;
        unsigned modifiers; /* rml_key_modifier bits */
        void *native;       /* the Rml::Event */
    } rml_event;

    typedef void (*rml_event_fn)(void *user, const rml_event *event);

    /* ---------- Core ---------- */

    void rml_set_system_interface(const rml_system_interface *system);
    bool rml_initialize();
    void rml_shutdown();
    /* The data must outlive the face. */
    bool rml_load_font_face(
        const uint8_t *data,
        size_t size,
        const char *family,
        rml_font_style style,
        int weight,
        bool fallback
    );
    void rml_string_set(rml_string *string, const char *text);

    /* The renderer must outlive its contexts and rml_release_render_managers. */
    rml_renderer *rml_renderer_create(const rml_render_interface *render, void *user);
    void rml_renderer_destroy(rml_renderer *renderer);
    void rml_release_render_managers();

    /* ---------- Contexts ---------- */

    rml_context *rml_create_context(const char *name, int width, int height, rml_renderer *renderer);
    bool rml_remove_context(const char *name);
    void rml_context_set_dimensions(rml_context *context, int width, int height);
    void rml_context_set_dp_ratio(rml_context *context, float dp_ratio);
    float rml_context_dp_ratio(const rml_context *context);
    /* Seconds until the context next needs an update and render, for a caret
     * blink or a transition; infinity when only input can change it. */
    double rml_context_next_update_delay(const rml_context *context);
    bool rml_context_update(rml_context *context);
    /* Opt-in disjoint Context::Update timings, in seconds. Keep this order
     * aligned with third_party/rmlui/context-update-profile.patch. */
    typedef enum rml_update_part
    {
        RML_UPDATE_MODELS,
        RML_UPDATE_ROOT,
        RML_UPDATE_LAYOUT,
        RML_UPDATE_RELEASE,
        RML_UPDATE_PARTS,
    } rml_update_part;
    bool rml_context_update_profiled(rml_context *context, double seconds[RML_UPDATE_PARTS]);
    bool rml_context_render(rml_context *context);
    /* Empty body document, without loading markup or updating data models. */
    rml_element *rml_context_create_document(rml_context *context, const char *source_url);
    rml_element *rml_context_load_document(rml_context *context, const char *path);
    /* `source_url` names the document in messages and resolves relative paths. */
    rml_element *rml_context_load_document_from_memory(
        rml_context *context,
        const char *rml,
        const char *source_url
    );
    rml_element *rml_context_hover_element(rml_context *context);
    rml_element *rml_context_focus_element(rml_context *context);
    void rml_context_activate_theme(rml_context *context, const char *name, bool active);
    bool rml_context_is_mouse_interacting(const rml_context *context);

    /* Input. Each returns true when RmlUi did not consume it. */
    bool rml_context_process_mouse_move(rml_context *context, int x, int y, unsigned modifiers);
    bool rml_context_process_mouse_leave(rml_context *context);
    bool rml_context_process_mouse_button_down(rml_context *context, int button, unsigned modifiers);
    bool rml_context_process_mouse_button_up(rml_context *context, int button, unsigned modifiers);
    bool rml_context_process_mouse_wheel(rml_context *context, rml_vec2 delta, unsigned modifiers);
    bool rml_context_process_key_down(rml_context *context, rml_key key, unsigned modifiers);
    bool rml_context_process_key_up(rml_context *context, rml_key key, unsigned modifiers);
    bool rml_context_process_text_input(rml_context *context, const char *text, size_t size);

    /* ---------- Documents ---------- */

    void rml_document_show(
        rml_element *document,
        rml_modal_flag modal,
        rml_focus_flag focus,
        rml_scroll_flag scroll
    );
    void rml_document_close(rml_element *document);
    void rml_document_update(rml_element *document);
    /* Resolves styles, animations and layout without repeating OnUpdate hooks. */
    void rml_document_refresh_layout(rml_element *document);

    /* ---------- Elements ---------- */

    /* Creates an element with the owner document and inserts it before
     * `before`, or appends it when `before` is NULL. */
    rml_element *rml_element_create_child(rml_element *parent, const char *tag, rml_element *before);
    /* Removes `child` from its parent and inserts it into `parent`; under
     * the same data model its views stay bound, as a DOM move's do. */
    void rml_element_move_child(rml_element *parent, rml_element *child, rml_element *before);
    /* Removes and destroys the child. */
    void rml_element_remove_child(rml_element *parent, rml_element *child);
    rml_element *rml_element_parent(const rml_element *element);
    int rml_element_child_count(const rml_element *element);
    rml_element *rml_element_child(const rml_element *element, int index);
    rml_element *rml_element_first_child(const rml_element *element);
    rml_context *rml_element_context(const rml_element *element);
    rml_element *rml_element_owner_document(const rml_element *element);
    rml_element *rml_element_by_id(rml_element *element, const char *id);
    rml_element *rml_element_query_selector(rml_element *element, const char *selector);
    /* Writes up to `max` matches and returns how many there are. */
    int rml_element_query_selector_all(
        rml_element *element,
        const char *selectors,
        rml_element **matches,
        int max
    );

    /* Valid while the element keeps its id and tag. */
    const char *rml_element_id(const rml_element *element);
    const char *rml_element_tag_name(const rml_element *element);

    void rml_element_set_attribute(rml_element *element, const char *name, const char *value);
    void rml_element_remove_attribute(rml_element *element, const char *name);
    bool rml_element_has_attribute(const rml_element *element, const char *name);
    size_t rml_element_attribute(const rml_element *element, const char *name, char *buffer, size_t size);
    void rml_element_set_class(rml_element *element, const char *name, bool on);
    bool rml_element_is_class_set(const rml_element *element, const char *name);
    void rml_element_set_class_names(rml_element *element, const char *names);
    bool rml_element_is_pseudo_class_set(const rml_element *element, const char *name);

    bool rml_element_set_property(rml_element *element, const char *name, const char *value);
    void rml_element_remove_property(rml_element *element, const char *name);
    rml_color rml_element_color(const rml_element *element);
    rml_color rml_element_border_top_color(const rml_element *element);
    /* The computed transitions, `transition: none` having none. */
    int rml_element_transitions(const rml_element *element, rml_transition *transitions, int max);

    void rml_element_set_inner_rml(rml_element *element, const char *rml);
    size_t rml_element_inner_rml(const rml_element *element, char *buffer, size_t size);

    /* Form controls. Select and the selection range need an <input>. */
    size_t rml_element_value(const rml_element *element, char *buffer, size_t size);
    void rml_element_set_value(rml_element *element, const char *value);
    void rml_element_select(rml_element *element);
    void rml_element_set_selection_range(rml_element *element, int start, int end);
    void rml_element_selection_range(rml_element *element, int *start, int *end);

    bool rml_element_focus(rml_element *element, bool focus_visible);
    /* Simulates a click, as a pointer press and release on the element do. */
    void rml_element_click(rml_element *element);
    void rml_element_blur(rml_element *element);
    void rml_element_scroll_into_view(
        rml_element *element,
        rml_scroll_alignment vertical,
        rml_scroll_alignment horizontal
    );
    float rml_element_scroll_top(rml_element *element);
    void rml_element_set_scroll_top(rml_element *element, float scroll_top);
    float rml_element_client_height(rml_element *element);
    float rml_element_scroll_height(rml_element *element);
    float rml_element_scroll_left(rml_element *element);
    void rml_element_set_scroll_left(rml_element *element, float scroll_left);
    float rml_element_client_width(rml_element *element);
    float rml_element_scroll_width(rml_element *element);
    rml_vec2 rml_element_absolute_offset(rml_element *element, rml_box_area area);
    rml_vec2 rml_element_relative_offset(rml_element *element, rml_box_area area);
    rml_vec2 rml_element_box_size(rml_element *element, rml_box_area area);
    bool rml_element_is_visible(const rml_element *element, bool include_ancestors);

    /* ---------- Events ---------- */

    rml_listener *rml_listener_create(rml_event_fn fn, void *user);
    void rml_listener_destroy(rml_listener *listener);
    void rml_element_add_event_listener(
        rml_element *element,
        rml_event_id id,
        rml_listener *listener,
        bool capture
    );
    void rml_element_remove_event_listener(
        rml_element *element,
        rml_event_id id,
        rml_listener *listener,
        bool capture
    );
    void rml_event_stop_propagation(const rml_event *event);
    /* Dispatches event `type` at `element` with `event`'s pointer, button
     * and modifiers as its parameters, as RmlUi's own events carry them; an
     * event RmlUi does not define, such as contextmenu, bubbles. False when a
     * listener stopped it. */
    bool rml_element_dispatch_event(rml_element *element, const char *type, const rml_event *event);

    /* ---------- Data models ---------- */

    /* An expression's emit('Name', ...), as Vue's $emit passes arguments:
     * `event` is the one whose data-event-* binding ran it, and its current
     * element holds that binding; `args` are the `argc` arguments after the
     * name, as text. */
    typedef void (*rml_emit_fn)(
        void *user,
        const rml_event *event,
        const char *name,
        int argc,
        const char *const *args
    );

    /* The data model `name` in `context`, for RmlUi's data bindings ({{ }},
     * data-for, data-value, data-checked, data-event-*, ...). Its variables
     * are the members of `doc`'s root object when it is created: objects bind
     * by member name, arrays by index and .size, and other values as scalars,
     * which bindings may overwrite. As in JavaScript, a name no variable has
     * reads as empty, and a missing member reads as null until set. The
     * doc must outlive the model and keep each variable's value in place,
     * changing its contents instead. Expressions may call emit('Name', ...). */
    rml_data_model *rml_data_model_create(
        rml_context *context,
        const char *name,
        yyjson_mut_doc *doc,
        rml_emit_fn emit,
        void *user
    );
    /* Removes the model from its context, unbinding its elements. */
    void rml_data_model_destroy(rml_data_model *model);
    /* Binds `value`, a member the caller added to the root of the model's
     * document, as variable `name`, before any markup binds it. */
    void rml_data_model_bind(rml_data_model *model, const char *name, yyjson_mut_val *value);
    /* Updates what binds `variable` after its value changed. Views look up
     * array items in constant time (ui/json_items.h); a dirty variable makes
     * them list arrays again, so an item replaced in place shows. */
    void rml_data_model_dirty(rml_data_model *model, const char *variable);
    /* Dirties every variable, so the next update evaluates each view once:
     * the reads it adds count the model's views. */
    void rml_data_model_dirty_all(rml_data_model *model);
    /* How many values bindings have written, so a caller sees when to read. */
    uint64_t rml_data_model_writes(const rml_data_model *model);
    /* What views read since the model was created: the values they
     * evaluated and the members and items they looked up on the way, each
     * view walking its address from a variable. A dirty variable costs one
     * evaluation for every view bound to it. */
    typedef struct rml_data_reads
    {
        uint64_t values;
        uint64_t lookups;
    } rml_data_reads;
    rml_data_reads rml_data_model_reads(const rml_data_model *model);
    /* Binds the element and its descendants to the context's model `name`,
     * as data-model="name" in markup does. The element must have a parent. */
    void rml_element_set_data_model(rml_element *element, const char *name);

    /* ---------- Custom elements and geometry ---------- */

    /* An ElementInstancer for elements with these hooks. It must outlive the
     * elements it creates; hooks must outlive the instancer. */
    rml_instancer *rml_instancer_create(const rml_element_hooks *hooks);
    void rml_instancer_destroy(rml_instancer *instancer);
    void rml_register_element_instancer(const char *tag, rml_instancer *instancer);
    /* A custom element's user data, passed to its hooks. */
    void rml_element_set_user(rml_element *element, void *user);
    void *rml_element_user(const rml_element *element);

    /* NULL when the element is in no context. */
    rml_render_manager *rml_element_render_manager(const rml_element *element);
    rml_geometry *rml_geometry_create(rml_render_manager *manager, rml_mesh mesh);
    void rml_geometry_render(const rml_geometry *geometry, rml_vec2 translation);
    void rml_geometry_destroy(rml_geometry *geometry);

    /* ---------- Fonts ---------- */

    /* Zero when no face matches. */
    uintptr_t rml_font_face(const char *family, rml_font_style style, int weight, int size);
    /* Width in pixels, shaped with no language. */
    int rml_font_string_width(uintptr_t face, const char *text);

    /* ---------- UI source maps (workstream A) ---------- */

    /* Replaces the document's style sheets with `rcss`, or adds it after
     * them; false, changing nothing, when RmlUi reads no style sheet from it.
     * The sheet is named `name`: RmlUi reports the name as the path of each
     * property's PropertySource, with the rule's 1-based line in `rcss`
     * (support/ui_style.h). The name is a plain file name, without '/', ':'
     * or '|', so relative image paths resolve as before. */
    bool rml_document_set_named_style_sheet(rml_element *document, const char *name, const char *rcss);
    bool rml_document_add_named_style_sheet(rml_element *document, const char *name, const char *rcss);
    /* The name of the sheet and the line of the rule that set `property` on
     * `element`, an ancestor's for an inherited value; false when no sheet's
     * rule set it, as for an inline style or a default. */
    bool rml_element_property_source(
        rml_element *element,
        const char *property,
        char *sheet,
        size_t size,
        int *line
    );

    /* ---------- UI language service (workstream B) ---------- */

    /* The name of the property, or shorthand, StyleSheetSpecification
     * registers with `id`; NULL for an id it has not. Valid from
     * rml_initialize until rml_shutdown. */
    const char *rml_property_name(uint8_t id);
    const char *rml_shorthand_name(uint8_t id);

    /* ---------- Inspection (the UI Inspector) ---------- */

    /* A property as an element's style holds it, and where it was declared:
     * the sheet's name (ui_style_origin maps it and the line to a file) and
     * the rule's line and selector. An inline property has an empty path and
     * rule and line 0. The strings are valid during the callback. */
    typedef struct rml_property_info
    {
        const char *name;
        const char *value;
        const char *path;
        int line;
        const char *rule;
        int specificity;
    } rml_property_info;

    typedef void (*rml_property_fn)(void *user, const rml_property_info *property);
    typedef void (*rml_attribute_fn)(void *user, const char *name, const char *value);

    int rml_context_document_count(const rml_context *context);
    rml_element *rml_context_document(rml_context *context, int index);
    /* The element that takes the pointer at `point`, in context pixels,
     * outside `ignore`, which may be NULL; NULL when none does. */
    rml_element *rml_context_element_at_point(
        rml_context *context,
        rml_vec2 point,
        const rml_element *ignore
    );

    /* Calls fn with each attribute's name and value. */
    void rml_element_attributes(const rml_element *element, rml_attribute_fn fn, void *user);
    size_t rml_element_class_names(const rml_element *element, char *buffer, size_t size);
    /* The pseudo classes set on the element, separated by spaces. */
    size_t rml_element_pseudo_classes(const rml_element *element, char *buffer, size_t size);
    void rml_element_set_pseudo_class(rml_element *element, const char *name, bool on);
    /* Calls fn with each property the element's style defines, or with
     * `inherited_by`, only those that element inherits from it. */
    void rml_element_local_properties(
        const rml_element *element,
        rml_element *inherited_by,
        rml_property_fn fn,
        void *user
    );
    /* The value the element uses for property `name`, from its style, an
     * ancestor or the default; 0 and "" for an unknown property. */
    size_t rml_element_property(rml_element *element, const char *name, char *buffer, size_t size);
    /* A text node's text; the element's tag must be "#text". */
    size_t rml_element_text(const rml_element *element, char *buffer, size_t size);

    /* ---------- Text drawn by a custom element (workstream C, kit_code) ---------- */

    /* A face's vertical metrics, in pixels. */
    typedef struct rml_font_metrics
    {
        float ascent;  /* above the baseline */
        float descent; /* below the baseline */
        float line_spacing;
    } rml_font_metrics;
    rml_font_metrics rml_font_face_metrics(uintptr_t face);
    /* Changes when the face's glyph textures do, which retires its text. */
    int rml_font_version(uintptr_t face);
    /* Width in pixels of text[0, size), as rml_font_string_width. */
    int rml_font_string_width_n(uintptr_t face, const char *text, size_t size);

    /* One line of text in a face, as meshes over the face's glyph textures
     * with the baseline's start at the origin. The color is premultiplied. */
    typedef struct rml_text rml_text;
    rml_text *rml_text_create(
        rml_render_manager *manager,
        uintptr_t face,
        const char *text,
        size_t size,
        rml_color color
    );
    void rml_text_render(const rml_text *text, rml_vec2 translation);
    void rml_text_destroy(rml_text *text);

    /* The scissor region in pixels; false while none is set. */
    bool rml_render_scissor(const rml_render_manager *manager, rml_rect *region);
    /* NULL removes the scissor. */
    void rml_render_set_scissor(rml_render_manager *manager, const rml_rect *region);

    /* A textinput event's text, copied as snprintf does. */
    size_t rml_event_text(const rml_event *event, char *buffer, size_t size);
    /* A mousescroll event's wheel delta; positive y scrolls down. */
    rml_vec2 rml_event_wheel(const rml_event *event);

    /* ---------- Code intelligence (workstream I) ---------- */

    /* Punctuation keys the Text Editor's chords name, with rml_key's values
     * (RmlUi 6.3's Input::KeyIdentifier). */
    enum
    {
        RML_KEY_OEM_1 = 38,     /* ; */
        RML_KEY_OEM_COMMA = 40, /* , */
        RML_KEY_OEM_2 = 43,     /* / */
        RML_KEY_OEM_4 = 45,     /* [ */
        RML_KEY_OEM_6 = 47,     /* ] */
    };
    /* What StyleSheetSpecification knows of RCSS property or shorthand
     * `name`: a property's initial value, and whether it inherits; false for
     * a name it does not know. */
    bool rml_property_describe(const char *name, char *initial, size_t size, bool *inherited, bool *shorthand);
    /* Changes each time rml_release_render_managers releases the font
     * resources: face handles from before are gone. */
    unsigned rml_font_generation();

    /* ---------- Wave 2 follow-ups (workstream P) ---------- */

    /* Takes the oldest value a binding changed since it was last taken, each
     * value once; false when there is none. Values stay in the model's doc. */
    bool rml_data_model_take_written(rml_data_model *model, yyjson_mut_val **value);

    /* ---------- Native elements for authored UI (workstream N) ---------- */

    /* The system interface's clock, in seconds: what transitions follow. */
    double rml_elapsed_time();
    /* Asks for the context's next update within `delay` seconds, as an
     * element that animates does. */
    void rml_context_request_next_update(rml_context *context, double delay);
    bool rml_context_theme_active(const rml_context *context, const char *name);
    /* Sets a text node's text; the element's tag must be "#text". */
    void rml_element_set_text(rml_element *element, const char *text);
    /* Single-line text managed locally after its initial normal layout.
     * Suppression keeps text changes from relaying out the owner document;
     * normal layout still positions the node when its box or style changes. */
    void rml_element_suppress_text_layout(rml_element *element);
    void rml_element_clear_text_lines(rml_element *element);
    void rml_element_add_text_line(rml_element *element, rml_vec2 offset, const char *text);
    /* Shaped width in pixels, using the element's computed font and spacing. */
    int rml_element_string_width(rml_element *element, const char *text);

    /* An element that may be destroyed while held: the ref reads NULL once
     * it is. */
    typedef struct rml_element_ref rml_element_ref;
    rml_element_ref *rml_element_ref_create(rml_element *element);
    rml_element *rml_element_ref_get(const rml_element_ref *ref);
    void rml_element_ref_destroy(rml_element_ref *ref);

    /* Handles every data-if binding's change of whether its element shows,
     * in place of setting or removing its inline display: none; `first`
     * marks a binding's first evaluation. Returning false leaves the change
     * to the binding (third_party/rmlui/data-if-handler.patch). NULL
     * removes the handler. */
    typedef bool (*rml_data_if_fn)(rml_element *element, bool show, bool first);
    void rml_set_data_if_handler(rml_data_if_fn fn);

    /* ---------- One document per area (workstream F5) ---------- */

    /* Gives `document` a copy of `source`'s style sheets, as though each
     * had been set on it too; nothing when `source` has none. */
    void rml_document_copy_style_sheet(rml_element *document, rml_element *source);

    /* ---------- Native controls (workstream F2) ---------- */

    /* Dispatches a `type` event from the element that bubbles, as a form
     * control's input and change do: data-event-<type> reads ev.value, the
     * text `value`, ev.canceled, and ev.name, the element's name attribute
     * (as a web handler reads event.target.name). */
    void rml_element_dispatch_value(rml_element *element, const char *type, const char *value, bool canceled);
    /* Listens for events named `type`, such as "input", which rml_event_id
     * does not name; the listener's events carry another id. */
    void rml_element_add_named_listener(
        rml_element *element,
        const char *type,
        rml_listener *listener,
        bool capture
    );

    /* ---------- Lists and drag (workstream F4) ---------- */

    /* Calls `create` for each context created and `destroy` for each one
     * destroyed from now until rml_shutdown, as an RmlUi plugin hears them;
     * either may be NULL. The hooks must outlive rml_shutdown. */
    typedef struct rml_context_hooks
    {
        void (*create)(rml_context *context);
        void (*destroy)(rml_context *context);
    } rml_context_hooks;
    void rml_add_context_hooks(const rml_context_hooks *hooks);
    /* The element holding the context's documents: every event in the
     * context passes it, first while capturing and last while bubbling. Its
     * box is the context's size in pixels. */
    rml_element *rml_context_root(rml_context *context);
    /* Dispatches a `type` event from the element that bubbles, with `count`
     * text parameters, which data-event-<type> reads as ev.<names[i]>. False
     * when a listener stopped it. */
    bool rml_element_dispatch_texts(
        rml_element *element,
        const char *type,
        int count,
        const char *const *names,
        const char *const *values
    );

    /* ---------- Members (workstream P5) ---------- */

    /* Calls `created` for each element RmlUi creates from now until
     * rml_shutdown, its attributes set, before it joins a document. */
    void rml_add_element_created(void (*created)(rml_element *element));

#ifdef __cplusplus
}
#endif

#endif
