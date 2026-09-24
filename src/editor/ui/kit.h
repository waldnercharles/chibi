#ifndef CHIBI_EDITOR_UI_KIT_H
#define CHIBI_EDITOR_UI_KIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Immediate-mode calls reconciled onto a retained RmlUi document.
     *
     * Each frame the editor emits its UI with keyed calls. Elements are created on
     * first use, updated when their properties change and removed when a frame no
     * longer emits them. Interaction results recorded during input processing are
     * returned by the next call with the same key, so widgets read like ImGui:
     * `if (kit_button(...))`, `kit_number(...)` returns true on change, and
     * kit_item_* queries describe the last emitted item.
     *
     * Keys are unique among siblings. Paths join ancestor keys with '/' and name
     * items for tests and inspection. Styling lives in kit.rcss; behavior follows
     * the Base UI contracts in docs/prototypes/studio/kit.html. */

    typedef struct kit kit;

    typedef struct kit_rect
    {
        float x, y, w, h;
    } kit_rect;

    enum
    {
        KIT_MOD_CTRL = 1 << 0,
        KIT_MOD_SHIFT = 1 << 1,
        KIT_MOD_ALT = 1 << 2,
        KIT_MOD_META = 1 << 3,
    };

    enum
    {
        KIT_DISABLED = 1 << 0,
        KIT_READONLY = 1 << 1,
        KIT_OVERRIDDEN = 1 << 2,
        KIT_ON = 1 << 3,     /* pressed toggle, checked item, selected row */
        KIT_ACTIVE = 1 << 4, /* active tree row */
        KIT_INVALID = 1 << 5,
    };

    /* Borrowed Rml::Context*; the document stays loaded until kit_destroy. */
    kit *kit_create(void *context, const char *document_path);
    void kit_destroy(kit *k);

    /* Host pointer state for the frame, in logical pixels. */
    typedef struct kit_frame_input
    {
        double time;
        float dp_ratio;
        float width, height;
        float mouse_x, mouse_y;
        bool down[3], pressed[3], released[3];
        float wheel;
        unsigned modifiers; /* KIT_MOD_* */
    } kit_frame_input;

    /* Emit the frame between begin and end. Input for this frame must already be
     * processed; end removes anything not emitted and applies deferred work. */
    void kit_begin(kit *k, const kit_frame_input *input);
    void kit_end(kit *k);

    /* Structure. `tag` defaults to div; `cls` is a space-separated class list. */
    void kit_root(kit *k, const char *element_id);
    void kit_push(kit *k, const char *key, const char *tag, const char *cls);
    void kit_pop(kit *k);
    void kit_leaf(kit *k, const char *key, const char *tag, const char *cls);
    void kit_label(kit *k, const char *key, const char *cls, const char *text);
    void kit_icon(kit *k, const char *key, const char *cls, uint32_t codepoint);
    /* A span whose content is markup the caller built and escaped, e.g. by
     * highlight_markup; reapplied only when it changes. */
    void kit_markup(kit *k, const char *key, const char *cls, const char *rml);
    /* Modify the last emitted element. */
    void kit_text(kit *k, const char *text);
    void kit_attr(kit *k, const char *name, const char *value);
    void kit_flag(kit *k, const char *name, bool on);
    void kit_style(kit *k, const char *property, const char *value);
    void kit_tip(kit *k, const char *label, const char *shortcut);

    /* Motion. Presence (Motion's AnimatePresence, Base UI's starting and ending
     * styles): the last item mounts with data-starting-style for one frame and,
     * once no longer emitted, stays with data-ending-style until the transitions
     * its RCSS declares finish. Layout: position changes within its parent animate
     * with a spring (FLIP). Reduced motion makes both immediate. */
    void kit_presence(kit *k);
    void kit_layout(kit *k);
    /* Motion's useSpring: a value that eases toward target over `duration`
     * seconds without bounce and keeps its velocity when the target moves.
     * Keyed under the current parent; reduced motion returns the target. */
    float kit_spring(kit *k, const char *key, float target, float duration);

    /* NumberFlow: digits spin to each new value with the trend of the change
     * and keep their place value. For discrete changes (counts, zoom steps); a
     * continuously changing value should stay plain text. */
    typedef struct kit_number_flow_desc
    {
        int precision; /* fraction digits */
        bool grouping; /* thousands separators */
        const char *prefix;
        const char *suffix;
    } kit_number_flow_desc;
    void kit_number_flow(kit *k, const char *key, const char *cls, double value, const kit_number_flow_desc *desc);
    /* Torph: when the text changes, kept characters (or words) slide to their
     * new places, removed ones fade in place and new ones fade in. Never for
     * text being edited. */
    void kit_morph(kit *k, const char *key, const char *cls, const char *text);

    /* Last-item queries, valid until the next emitted item. */
    bool kit_item_clicked(kit *k);
    bool kit_item_double_clicked(kit *k);
    bool kit_item_context_clicked(kit *k); /* right press; open a context menu */
    bool kit_item_pressed(kit *k);         /* left press this frame */
    bool kit_item_hovered(kit *k);
    bool kit_item_active(kit *k);    /* scrubbing, editing or held */
    bool kit_item_committed(kit *k); /* a gesture or edit finished this frame */
    kit_rect kit_item_rect(kit *k);  /* logical pixels, latest layout */

    /* Conditional class lists (clsx): KIT_CX(k, "area", { "fill", fill }, { "live", live })
     * joins the base with each class whose condition holds. The string lives until
     * kit_end. State belongs in data-* attributes (kit_flag), not classes. */
    typedef struct kit_class
    {
        const char *name;
        bool on;
    } kit_class;
    const char *kit_classes(kit *k, const char *base, const kit_class *list, int count);
    /* A copy of text that lives until kit_end, for labels built per frame. */
    const char *kit_string(kit *k, const char *text);
#define KIT_CX(k, base, ...)                                                   \
    kit_classes(                                                               \
        k,                                                                     \
        base,                                                                  \
        (const kit_class[]){ __VA_ARGS__ },                                    \
        (int)(sizeof((const kit_class[]){ __VA_ARGS__ }) / sizeof(kit_class))  \
    )

    /* Variants (cva): each maps to classes in one table in kit.cpp. */
    typedef enum
    {
        KIT_GHOST, /* default: transparent until hovered */
        KIT_SUBTLE,
        KIT_PRIMARY,
        KIT_OUTLINE,
        KIT_DANGER,
    } kit_variant;
    typedef enum
    {
        KIT_MD,
        KIT_SM,
    } kit_size;

    /* Controls. Values are read every frame; a true return means the user
     * changed the value this frame and *value holds the new value. */
    typedef struct kit_button_desc
    {
        const char *label;
        uint32_t icon; /* an icon without a label renders square */
        kit_variant variant;
        kit_size size;
        const char *cls; /* placement hooks for the surrounding layout, never variants */
        bool morph; /* the label morphs when it changes (Torph) */
        const char *tip;
        const char *shortcut;
        unsigned flags;
    } kit_button_desc;
    bool kit_button(kit *k, const char *key, const kit_button_desc *desc);

    typedef struct kit_number_desc
    {
        double min, max; /* both zero means unbounded */
        double step;     /* zero means 1 */
        int precision;
        bool integer;
        bool slider; /* bounded values draw a fill */
        const char *unit;
        const char *addon; /* leading label inside the field, e.g. "X" */
        const char *addon_cls;
        unsigned flags;
    } kit_number_desc;
    /* Scrub, click-to-type, expressions and keyboard steps; see the kit contract.
     * kit_item_committed() is true once per gesture; record Undo there. */
    bool kit_number(kit *k, const char *key, double *value, const kit_number_desc *desc);
    bool kit_checkbox(kit *k, const char *key, bool *value, const char *label, unsigned flags);
    /* Up to three options render as a toggle group, more as a select. */
    bool kit_choice(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags);
    /* Commits on Enter or blur; Escape restores. Returns true on commit. */
    bool kit_text_field(kit *k, const char *key, char *buffer, size_t size, const char *placeholder, unsigned flags);
    /* Filters live; returns true whenever the text changes. Escape clears. */
    bool kit_search(kit *k, const char *key, char *buffer, size_t size, const char *placeholder);
    /* RGBA as 0xRRGGBBAA. Live while the picker is open; commits on close. */
    bool kit_color(kit *k, const char *key, uint32_t *rgba, unsigned flags);

    /* Containers. */
    bool kit_panel_begin(kit *k, const char *key, const char *title, const char *badge, bool default_open);
    void kit_panel_end(kit *k);
    void kit_prop_begin(kit *k, const char *key, const char *label);
    void kit_prop_end(kit *k);

    typedef struct kit_row_desc
    {
        const char *label;
        uint32_t icon;
        int depth;
        int twisty; /* -1 none, 0 collapsed, 1 expanded */
        const char *meta;
        unsigned flags; /* KIT_ON selected, KIT_ACTIVE active */
    } kit_row_desc;
    enum
    {
        KIT_ROW_CLICK = 1 << 0,
        KIT_ROW_DOUBLE_CLICK = 1 << 1,
        KIT_ROW_CONTEXT = 1 << 2,
        KIT_ROW_TWISTY = 1 << 3,
    };
    /* Begins a row; emit trailing toggles inside, then kit_row_end. Returns KIT_ROW_* flags;
     * modifier state for selection comes from kit_modifiers(). */
    unsigned kit_row_begin(kit *k, const char *key, const kit_row_desc *desc);
    void kit_row_end(kit *k);

    unsigned kit_modifiers(const kit *k);

    /* Virtualized list (Virtuoso) of fixed-height rows. Begin sizes a scroll
     * container for every row and returns the visible range with overscan; emit
     * rows first..last in order, then end. Rows keep stable keys, so scrolling
     * reuses their elements. */
    typedef struct kit_list_desc
    {
        int count;
        float row_height; /* dp; every row must be exactly this tall */
        float overscan;   /* dp beyond the viewport; 0 means 120 */
        bool follow; /* stay pinned to the bottom while already there (logs) */
        int reveal; /* scroll this row into view when the index changes; -1 none */
    } kit_list_desc;
    bool kit_list_begin(
        kit *k,
        const char *key,
        const char *cls,
        const kit_list_desc *desc,
        int *first,
        int *last
    );
    void kit_list_end(kit *k);

    /* Popups live in the document's overlay layer. Open anchors below the last item
     * or at a point; begin returns true while open; end closes on activation. */
    void kit_popup_open(kit *k, const char *key);
    void kit_popup_open_at(kit *k, const char *key, float x, float y);
    void kit_popup_close(kit *k, const char *key);
    bool kit_popup_is_open(const kit *k, const char *key);
    bool kit_popup_begin(kit *k, const char *key, const char *cls);
    void kit_popup_end(kit *k);

    typedef struct kit_menu_item_desc
    {
        const char *label;
        uint32_t icon;
        const char *shortcut;
        int checked; /* -1 not checkable */
        bool disabled;
        bool current; /* the active choice among items that keep their icons */
    } kit_menu_item_desc;
    bool kit_menu_item(kit *k, const char *key, const kit_menu_item_desc *desc);
    void kit_menu_separator(kit *k, const char *key);
    void kit_menu_group(kit *k, const char *key, const char *label);
    bool kit_submenu_begin(kit *k, const char *key, const char *label, uint32_t icon);
    void kit_submenu_end(kit *k);

    /* A menubar trigger opens its popup on press; hovering another trigger
     * while one is open switches (Base UI Menubar). While open it begins the
     * popup and returns true: emit items, then kit_popup_end. */
    bool kit_menubar_trigger(kit *k, const char *key, const char *label);

    /* Command palette (cmdk). Emit items while open; returns the chosen index or -1. */
    typedef struct kit_command_item
    {
        const char *label;
        const char *group;
        const char *meta;
        const char *shortcut;
        const char *keywords;
        uint32_t icon;
    } kit_command_item;
    void kit_command_open(kit *k, const char *placeholder);
    bool kit_command_is_open(const kit *k);
    int kit_command(kit *k, const kit_command_item *items, int count);
    /* Items already filtered and ranked by the caller for the current query,
     * shown in order and grouped by first appearance. */
    int kit_command_ranked(kit *k, const kit_command_item *items, int count);
    const char *kit_command_query(const kit *k);

    /* Splitter: drag along axis 0 (x) or 1 (y). Returns true while value changes. */
    bool kit_splitter(kit *k, const char *key, int axis, float *value, float minimum, float maximum, bool invert);

    /* A surface shows a host texture ("kit:<name>") and receives raw pointer input. */
    typedef struct kit_surface_input
    {
        kit_rect rect;
        float x, y; /* pointer relative to rect */
        bool hovered;
        bool pressed[3], released[3], down[3];
        float wheel;
    } kit_surface_input;
    void kit_surface(kit *k, const char *key, const char *cls, const char *texture, kit_surface_input *input);

    /* Toasts (Sonner): a bottom-right stack, three visible, four seconds each, paused
     * while hovered, dismissed by a drag to the right. Passing the id of a live toast
     * updates it in place (a loading toast becoming success); 0 creates one. */
    typedef enum
    {
        KIT_TOAST_DEFAULT,
        KIT_TOAST_SUCCESS,
        KIT_TOAST_INFO,
        KIT_TOAST_WARNING,
        KIT_TOAST_ERROR,
        KIT_TOAST_LOADING, /* stays until updated */
    } kit_toast_type;
    typedef struct kit_toast_desc
    {
        kit_toast_type type;
        const char *title;
        const char *description;
        const char *action; /* one button, e.g. "Undo"; pressing it dismisses the toast */
        double duration; /* seconds; 0 means 4, negative stays until closed */
    } kit_toast_desc;
    int kit_toast(kit *k, int id, const kit_toast_desc *desc);
    void kit_toast_dismiss(kit *k, int id);
    /* True once, the frame after the toast's action was pressed. */
    bool kit_toast_action(kit *k, int id);

    /* Drag and drop (dnd-kit). The last item becomes a source: moving 5 dp with
     * the button held, or holding 200 ms within 10 dp, picks it up; an overlay
     * follows the pointer and Escape cancels. The last item becomes a target
     * for one type: it is marked data-drop-over while a matching drag is over
     * it and returns true, with the payload, on the frame a drop lands. */
    typedef struct kit_drag_desc
    {
        const char *type; /* e.g. "prototype" */
        const char *payload;
        const char *label; /* overlay text; defaults to the payload */
        uint32_t icon;
    } kit_drag_desc;
    void kit_drag_source(kit *k, const kit_drag_desc *desc);
    bool kit_drop_target(kit *k, const char *type, char *payload, size_t size);
    /* A drag of this type (any when NULL) is in progress. */
    bool kit_dragging(const kit *k, const char *type);

    /* Live line chart (Liveline, with Recharts' ticks). Series are time-ascending
     * samples; the right edge is `now` and `window` seconds are shown. The range
     * follows the data with a 12% margin and eases toward it; curves never
     * overshoot a sample. The chart's `color` draws the first series and its
     * `border-color` the grid. Returns the time under the pointer, or -1. */
    typedef struct kit_chart_series
    {
        const float *times; /* seconds, ascending */
        const float *values;
        int count;
        uint32_t color; /* 0xRRGGBBAA; 0 uses the chart's color */
    } kit_chart_series;
    typedef struct kit_chart_desc
    {
        const kit_chart_series *series;
        int series_count;
        double now;
        float window;    /* seconds; 0 means 30 */
        float min, max;  /* both 0: the range follows the data */
        float reference; /* a dashed line such as a frame budget; 0 none */
        const char *unit;
        int precision;
        bool fill; /* area under the first series */
        bool live; /* pulse the newest point */
    } kit_chart_desc;
    float kit_chart(kit *k, const char *key, const char *cls, const kit_chart_desc *desc);

    /* Node-editor wires and the canvas grid, drawn by one element (xyflow
     * edges, Blender noodles). A wire end anchors to the center of the element
     * at a kit path, resolved after layout, or to a point; points and waypoints
     * are in the element's own dp. Wires take the shape arrange_path gives
     * their style, so the styles follow arrange_route_style. */
    typedef enum kit_wire_style
    {
        KIT_WIRE_CURVED,
        KIT_WIRE_ORTHOGONAL, /* level and vertical runs with rounded corners */
        KIT_WIRE_STRAIGHT,
        KIT_WIRE_SUBWAY, /* level runs joined by 45 degree diagonals */
        KIT_WIRE_STYLE_COUNT,
    } kit_wire_style;
    enum
    {
        KIT_WIRE_DASHED = 1 << 0,
        KIT_WIRE_FLOW = 1 << 1, /* markers travel from source to target */
    };
    typedef struct kit_wire
    {
        const char *from, *to; /* kit paths; NULL uses the point */
        float from_x, from_y, to_x, to_y;
        const float *via; /* waypoints as x, y pairs */
        int via_count;
        uint32_t color; /* 0xRRGGBBAA */
        float width;    /* dp; 0 means 1.5 */
        unsigned flags;
        float flow; /* seconds, the marker phase with KIT_WIRE_FLOW */
    } kit_wire;
    typedef struct kit_wires_desc
    {
        const kit_wire *wires;
        int count;
        kit_wire_style style;
        float grid;           /* line spacing in dp; 0 draws no grid */
        float grid_x, grid_y; /* grid coordinate at the element's top-left */
        uint32_t grid_color, grid_major_color;
    } kit_wires_desc;
    void kit_wires(kit *k, const char *key, const char *cls, const kit_wires_desc *desc);
    /* The cubic pieces of a wire drawn in `style` through `count` knots, given
     * as x, y pairs with both ends included. Writes up to `max` pieces as
     * x0 y0 x1 y1 x2 y2 x3 y3 and returns the piece count, which may exceed
     * max. */
    int kit_wire_shape(kit_wire_style style, const float *knots, int count, float *pieces, int max);
    /* The last drawn curve of wire `index` as x, y pairs in the element's dp.
     * Returns the point count, which may exceed max. */
    int kit_wire_points(const kit *k, const char *path, int index, float *xy, int max);

    /* A vector glyph that fills its box with one-pixel antialiasing at any
     * scale: a node socket, a status dot. The ring is drawn inside the box. */
    typedef enum kit_dot_shape
    {
        KIT_DOT_CIRCLE,
        KIT_DOT_ARROW,  /* pointing right */
        KIT_DOT_SQUARE, /* a many-valued socket: arrays and sets */
    } kit_dot_shape;
    typedef struct kit_dot_desc
    {
        kit_dot_shape shape;
        uint32_t fill; /* 0xRRGGBBAA */
        uint32_t ring;
        float ring_width; /* dp */
    } kit_dot_desc;
    void kit_dot(kit *k, const char *key, const char *cls, const kit_dot_desc *desc);

    /* Host queries. */
    bool kit_wants_keyboard(const kit *k);
    bool kit_wants_text(const kit *k, kit_rect *caret);
    /* True while focus can receive typed characters, including a focused number field. */
    bool kit_accepts_text(const kit *k);
    bool kit_wants_pointer(const kit *k);
    /* Width in dp of one line of Inter at `size` dp and `weight` (400 regular),
     * as drawn at `ratio` pixels per dp: text renders at whole pixel sizes with
     * hinting, so small sizes run wider than a scaled measurement. */
    float kit_text_width(const kit *k, const char *text, float size, int weight, float ratio);
    /* Focuses the item emitted last, selecting a field's text (Base UI autoFocus). */
    void kit_focus(kit *k);
    /* Drops keyboard focus, as a press elsewhere in the window does. */
    void kit_blur(kit *k);
    /* The path of the innermost emitted item under the pointer, or NULL. */
    const char *kit_hovered_path(const kit *k);
    /* True over a control that takes the pointer itself: fields, buttons, choices. */
    bool kit_pointer_on_control(const kit *k);
    /* The path of the item emitted last, valid while it stays emitted. */
    const char *kit_item_path(const kit *k);
    bool kit_find(const kit *k, const char *path, kit_rect *out);
    void kit_set_theme(kit *k, const char *name, bool active);
    void kit_set_body_class(kit *k, const char *name, bool on);

#ifdef __cplusplus
}
#endif

#endif
