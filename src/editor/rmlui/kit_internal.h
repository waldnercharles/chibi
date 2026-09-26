/* Studio kit internals shared by the reconciler (kit.c) and its feature
 * modules: controls, number fields, popups and menus, the command palette,
 * motion, text in place, toasts, lists, drag and drop, charts and wires. Not
 * part of the C API.
 *
 * Conventions:
 * - An owned string is a `char *`, NULL until first set. str_set replaces it
 *   with a copy and reports a change; str_eq compares. Both treat a NULL
 *   string or NULL text as "". A NULL string always changes on its first
 *   str_set, which applies a widget's label, unit, addon, meta and badge on
 *   their first frame even when empty.
 * - Vectors are support/array.h arrays (apush, acount, asetlen, afree), NULL
 *   when empty. An array marked owned frees its elements too.
 * - A node's attrs and styles are arrays of pairs searched linearly.
 * - Per-kit indexes are CF_MAPs: by_element keyed by the element pointer;
 *   by_path, popups and springs keyed by str_hash of a path or key the value
 *   carries, and every lookup compares it (path_node, popup_find). A popup is
 *   heap allocated, so pointers to it stay valid; spring values are inline and
 *   move on insertion. No popup loop depends on key order.
 * - Owned pointers (node children, flow, morph and chart; exiting nodes; roots,
 *   popups) are freed only by node_free and kit_destroy; update them with any
 *   new owned field.
 * - Strings for one frame (frame_format, kit_string, kit_classes) are freed by
 *   kit_end. */
#ifndef CHIBI_EDITOR_RMLUI_KIT_INTERNAL_H
#define CHIBI_EDITOR_RMLUI_KIT_INTERNAL_H

#include "editor/ui/kit.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/file_watch.h"
#include "ui/rml.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOOLTIP_DELAY 0.5
#define TOOLTIP_GROUP 0.4
#define SUBMENU_DELAY 0.1
#define STEP_REPEAT_DELAY 0.4
#define STEP_REPEAT_INTERVAL 0.06
#define SCRUB_THRESHOLD 3.0f
#define TYPEAHEAD_RESET 0.5
/* The buffer format_number writes. */
#define NUMBER_TEXT 64

typedef enum node_kind
{
    KIND_BOX,
    KIND_ROOT,
    KIND_BODY,
    KIND_BUTTON,
    KIND_NUMBER,
    KIND_CHECK,
    KIND_TOGGLE,
    KIND_SELECT,
    KIND_TEXT,
    KIND_SEARCH,
    KIND_COLOR,
    KIND_PANEL,
    KIND_PROP,
    KIND_ROW,
    KIND_POPUP,
    KIND_MENU_ITEM,
    KIND_SUBMENU,
    KIND_TRIGGER,
    KIND_SPLITTER,
    KIND_SURFACE,
    KIND_COMMAND,
    KIND_COMMAND_ITEM,
    KIND_LIST,
} node_kind;

/* Interaction recorded on a node during input, cleared by kit_end. */
enum
{
    EV_CLICK = 1 << 0,
    EV_DOUBLE = 1 << 1,
    EV_CONTEXT = 1 << 2,
    EV_PRESS = 1 << 3,
    EV_TWISTY = 1 << 4,
};

typedef struct node node;

typedef struct pair
{
    char *name;
    char *value;
} pair;

/* A splitter's arguments: the axis it drags along, 0 (x) or 1 (y); whether its
 * value grows against the pointer; and the value's bounds. */
typedef struct splitter_desc
{
    int axis;
    bool invert;
    double min, max;
} splitter_desc;

/* Control state. A new node's widget has pending_index -1 and icon
 * UINT32_MAX; everything else starts zero. */
typedef struct widget
{
    bool open;
    bool pending;
    bool committed;
    bool editing;
    bool scrubbing;
    bool pressing;
    bool suppress_click;
    bool focused;
    bool area; /* a text field over several lines */
    double value;
    double start;
    double pending_value;
    int pending_index;
    int step_dir;
    double step_next;
    float press_x;
    kit_number_desc desc; /* its strings are borrowed for one call only */
    splitter_desc split;
    char *unit, *addon, *addon_cls, *draft, *applied, *label, *meta, *badge,
        *placeholder;
    char **labels; /* owned strings */
    uint32_t icon;
    uint32_t color;
    float hsv[3];
    int drag_part;               /* color picker: 1 plane, 2 hue */
    bool at_bottom;              /* list: pinned to the bottom */
    rml_element *body;           /* list content */
    float body_height, body_top; /* list: the content sizing applied, in dp */
    rml_element *value_el, *fill_el, *input_el, *addon_el, *unit_el, *label_el,
        *icon_el, *meta_el, *twisty_el, *kbd_el, *badge_el, *head_el, *swatch_el;
} widget;

/* Motion (kit_motion.c). A spring's state along one axis, as displacement
 * from its target; see spring_step. */
typedef struct spring
{
    double x, v;
} spring;

typedef struct spring_params
{
    double stiffness, damping, mass;
} spring_params;

/* Text in place (kit_text.c). A NumberFlow wheel position is in cells of a
 * 20-cell strip (0-9 twice), a spring around the target cell. */
typedef struct flow_digit
{
    rml_element *col, *strip;
    int place; /* units 0, tens 1, tenths -1 */
    double target;
    spring pos;
    double appear; /* 0 hidden to 1 shown */
    bool leaving;
} flow_digit;

typedef struct flow_state
{
    bool init, grouping;
    double value;
    char *text, *prefix, *suffix;
    flow_digit *digits;
    rml_element **symbols;
    float digit_w;
} flow_state;

typedef enum morph_phase
{
    MORPH_STAY,
    MORPH_ENTER,
    MORPH_LEAVE,
} morph_phase;

typedef struct morph_seg
{
    char *id;
    rml_element *el;
    morph_phase state;
    float from_x, from_y, dx, dy;
} morph_seg;

typedef struct morph_state
{
    bool init, measure;
    char *text;
    morph_seg *segs;
    rml_element *box;
    double start; /* -1 while at rest */
    float from_w, to_w;
} morph_state;

/* Charts (kit_chart.c): what the chart element draws this frame. The chart
 * element's user data owns its frame. */
typedef struct chart_series
{
    float *times, *values;
    uint32_t color;
} chart_series;

typedef struct chart_frame
{
    chart_series *series;
    double *ticks;
    double now, lo, hi, hover_value;
    float window, dp, reference, hover_x;
    bool fill, live, reduced_motion;
} chart_frame;

typedef struct chart_state
{
    bool ranged;
    double lo, hi, time;
    rml_element **labels;
    rml_element *badge, *hover;
} chart_state;

struct node
{
    char *key;
    char *path;
    node_kind kind;
    rml_element *el;
    rml_element *container; /* where tracked children go */
    node *parent;
    node **children; /* owned */
    int cursor;
    uint64_t frame;
    char *cls, *text, *tag;
    pair *attrs, *styles;
    unsigned events; /* EV_* */
    widget w;
    uint64_t created; /* frame of creation */
    bool presence; /* kept with data-ending-style until its transitions finish */
    bool layout;   /* position changes animate (FLIP) */
    bool layout_known;
    kit_rect layout_rect;
    spring layout_x, layout_y;
    flow_state *flow;   /* owned, NULL until used */
    morph_state *morph; /* owned, NULL until used */
    chart_state *chart; /* owned, NULL until used */
    char *drop_type; /* accepted drag type, set each frame by kit_drop_target */
    bool hosted;     /* kit_host: its children are mounted content */
};

/* A toast (kit_toast.c). Positions are springs around their targets, in dp. */
typedef struct toast
{
    int id;
    kit_toast_type type;
    char *title, *description, *action;
    double duration, remaining; /* seconds; a negative duration stays until closed */
    bool placed, removing, swiping, swiped, forced_height;
    float height, press_x, swipe_velocity;
    double y_target, x_target, scale_target, opacity_target;
    spring y, x, scale, opacity;
} toast;

/* The drag in progress (kit_drag.c). */
typedef struct drag_state
{
    bool pending, active, dropped;
    char *type, *payload, *label, *source, *over, *target;
    uint32_t icon;
    float x0, y0;
    double t0;
} drag_state;

/* A value animated by kit_spring, keyed by its path. */
typedef struct spring_value
{
    char *key;
    double target, time;
    spring s;
    spring_params params; /* set from duration before the first step */
    float duration;
    uint64_t frame;
} spring_value;

/* A retired node kept until its exit transitions end. */
typedef struct exiting
{
    node *node; /* owned */
    double until;
} exiting;

/* popup_at creates one closed, with highlight -1. */
typedef struct popup
{
    char *key;
    bool open;
    bool placed;
    bool at_point;
    bool keyboard;
    bool activated;
    bool menubar;
    float x, y;
    kit_rect anchor;
    char *parent; /* a submenu's parent popup key */
    int highlight;
    char *sub_hover;
    double sub_hover_since;
    node **items; /* entries may be NULL */
    node *node;   /* the emitted popup, NULL once unmapped */
    char *typed;
    double typed_at;
    float content;       /* widest row this frame, dp; sizes the menu */
    float content_shown; /* the width applied */
} popup;

typedef struct command_state
{
    bool open;
    bool focus;
    char *placeholder;
    char *query;
    int selected;
    int chosen;  /* -1 none */
    bool reveal; /* keyboard moved the selection; scroll it into view */
    int *shown;  /* original indices in display order */
    rml_element *input;
} command_state;

struct kit
{
    rml_context *context;
    rml_element *doc;
    rml_listener *listener;
    file_watch *styles; /* the resource stylesheets; an edit restyles */
    char *sheet;        /* the document's own stylesheet, beside its markup */
    char *theme;        /* kit_theme's RCSS, before the document's own */
    pair *extra_styles; /* kit_style's RCSS, added after the stylesheets */
    bool restyle;       /* the theme or the extra styles changed */
    node **roots;       /* owned: the layer, then kit_root elements */
    CF_MAP(node *) by_element; /* keyed by the element pointer */
    CF_MAP(node *) by_path;    /* keyed by str_hash(path) */
    node **stack;
    node *last;
    node *root;
    node *layer;
    uint64_t frame; /* starts at 1 */
    kit_frame_input in;
    CF_MAP(popup *) popups; /* owned; keyed by str_hash(key) */
    popup **popup_stack;    /* open order, top last */
    node **triggers;        /* entries may be NULL */
    rml_element *tooltip;
    rml_element *tip_target;
    double tip_since;
    double tip_warm_until;
    bool tip_visible;
    uint64_t tip_shown_frame;
    command_state command;
    rml_element *command_backdrop;
    char *surface_capture;
    int surface_button; /* -1 none */
    popup **emitting;   /* popups being emitted, innermost last */
    bool keyboard_edit;
    char **strings; /* owned frame strings; freed by kit_end */
    bool reduced_motion;
    double motion_time;
    node **entering; /* data-starting-style set at creation */
    exiting *exiting;
    node **layout_nodes;
    CF_MAP(spring_value) springs; /* keyed by str_hash(key) */
    node **text_motion;           /* NumberFlow and Torph in flight */
    bool layout_pass;             /* measure layout before this frame renders */
    drag_state drag;
    toast *toasts;
    CF_MAP(uint64_t) toast_actions; /* toast id -> frame the action was pressed */
    int toast_next;
    double toast_time;
    float front_height;
    bool toasts_expanded;
};

/* ---------- Core (kit.c) ---------- */

/* Strings. str_cat, str_catf and escape append to an owned string, starting
 * one when NULL. */
uint64_t str_hash(const char *s);
bool str_eq(const char *a, const char *b);
bool str_set(char **s, const char *text);
void str_cat(char **s, const char *text);
void str_catf(char **s, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
/* Appends text with &, <, > and " as RML entities. */
void escape(char **s, const char *text);
void utf8(char out[static 5], uint32_t cp);
void format_number(char out[static NUMBER_TEXT], double v, int precision, bool integer);
/* A formatted string that lives until kit_end. */
const char *frame_format(kit *k, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
bool rect_contains(kit_rect r, float x, float y);

/* Element reads, as owned strings. */
char *element_value(const rml_element *el);
char *element_attribute(const rml_element *el, const char *name);
char *element_inner_rml(const rml_element *el);

/* Geometry in logical pixels. */
float dp(const kit *k);
kit_rect element_rect(const kit *k, rml_element *el);

/* Reconciled attribute writes: each touches the element only on change. */
void set_class(node *n, const char *cls);
void set_attr(node *n, const char *name, const char *value);
void set_flag(node *n, const char *name, bool on);
void flag_element(rml_element *el, const char *name, bool on);
void set_text(node *n, const char *text);
void set_element_text(rml_element *el, const char *text);
/* Escapes text into the element's content unconditionally. */
void set_inner_text(rml_element *el, const char *text);

/* Untracked child elements owned by a widget; text may be NULL. */
rml_element *raw(rml_element *parent, const char *tag, const char *cls, const char *text);
rml_element *raw_icon(rml_element *parent, const char *cls, uint32_t cp);

/* Keyed emission. node_free frees a node, its children and everything they
 * own, never their elements. */
void node_free(node *n);
void unmap(kit *k, node *n);
node *emit_in(kit *k, node *parent, const char *key, node_kind kind, const char *tag, const char *cls);
node *emit(kit *k, const char *key, node_kind kind, const char *tag, const char *cls);
void push_node(kit *k, node *n);
void pop_node(kit *k);
node *node_for(const kit *k, rml_element *el);
node *path_node(const kit *k, const char *path);
bool within(rml_element *el, rml_element *ancestor);
/* The index of n in a node array, or -1. */
int nodes_index(node *const *list, const node *n);
/* Removes every n from a node array, keeping order. */
void nodes_remove(node **list, const node *n);

/* Popups by key: find returns NULL when absent, at creates one closed. */
popup *popup_find(const kit *k, const char *key);
popup *popup_at(kit *k, const char *key);

/* Event routing: the listener dispatches each event by node kind to the
 * handlers below. Returns KIT_MOD_* bits. */
unsigned event_modifiers(const rml_event *e);

/* ---------- Motion (kit_motion.c) ---------- */

void spring_step(spring *s, spring_params p, double dt);
/* Motion's parameters for a visual duration and bounce. */
spring_params spring_for(double visual_duration, double bounce);
/* Takes the node: removes its element now, or keeps a presence node until its
 * exit transitions end. */
void retire(kit *k, node *n);
void presence(kit *k, node *n);
void motion_frame(kit *k);
void forget_motion(kit *k, node *n);

/* ---------- Text in place (kit_text.c) ---------- */

void text_frame(kit *k, double dt);
void morph_label(kit *k, node *n, rml_element *box, const char *text);

/* ---------- Toasts (kit_toast.c) ---------- */

void toasts_frame(kit *k);

/* ---------- Drag and drop (kit_drag.c) ---------- */

void drag_begin_frame(kit *k);
void drag_end_frame(kit *k);
void drag_cancel(kit *k);

/* ---------- Custom elements (kit_chart.c, kit_wires.c) ---------- */

/* Registered per kit: RmlUi's factory is rebuilt whenever it reinitializes. */
void register_chart_element();
void register_wires_element();
void register_dot_element();

/* ---------- Number fields (kit_number.c) ---------- */

void repeat_steppers(kit *k);
void number_press(kit *k, node *n, const rml_event *e);
void number_release(node *n);
void number_click(kit *k, node *n);
/* Dragstart, drag and dragend, told apart by e->id. */
void number_drag(kit *k, node *n, const rml_event *e);
void number_key(kit *k, node *n, const rml_event *e);
void number_blur(kit *k, node *n, rml_element *target);

/* ---------- Other controls (kit_controls.c) ---------- */

/* Handlers ignore kinds they do not own. */
void control_press(kit *k, node *n, const rml_event *e);
void control_click(kit *k, node *n, rml_element *target);
void control_drag(kit *k, node *n, const rml_event *e);
void control_key(kit *k, node *n, const rml_event *e);
void control_change(node *n, rml_element *target);
void control_focus(node *n, rml_element *target);
void control_blur(node *n, rml_element *target);

/* ---------- Popups, menus and tooltips (kit_popup.c) ---------- */

void close_popup(kit *k, const char *key);
void close_all_popups(kit *k);
/* parent is NULL or "" for a top-level popup. */
void open_popup(kit *k, const char *key, kit_rect anchor, bool at_point, float x, float y, const char *parent);
void place_popups(kit *k);
void open_hovered_submenus(kit *k);
void close_activated_menus(kit *k);
/* Sees every press; one outside the popups and their triggers closes them. */
void popup_press(kit *k, node *n, rml_element *target);
void trigger_press(kit *k, node *n, const rml_event *e);
void menu_click(kit *k, node *n);
void menu_hover(kit *k, node *n);
/* False when no popup is open. */
bool popup_key(kit *k, const rml_event *e);
void set_tip(node *n, const char *label, const char *shortcut);
void update_tooltip(kit *k);

/* ---------- Command palette (kit_command.c) ---------- */

/* Key and change handlers return false for events that are not the
 * palette's. */
void command_press(kit *k, rml_element *target);
bool command_key(kit *k, const rml_event *e);
bool command_change(kit *k, rml_element *target);
void command_click(kit *k, node *n);
void command_hover(kit *k, node *n);

#endif
