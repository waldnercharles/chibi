/* Studio kit internals shared by the reconciler (kit.cpp) and its feature
 * modules: motion, toasts, lists, drag and drop, charts. Not part of the C API. */
#ifndef CHIBI_EDITOR_RMLUI_KIT_INTERNAL_HPP
#define CHIBI_EDITOR_RMLUI_KIT_INTERNAL_HPP

#include "editor/ui/kit.h"

#include <RmlUi/Core.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class KitListener;

namespace kit_impl {

constexpr double kTooltipDelay = 0.5;
constexpr double kTooltipGroup = 0.4;
constexpr double kSubmenuDelay = 0.1;
constexpr double kStepRepeatDelay = 0.4;
constexpr double kStepRepeatInterval = 0.06;
constexpr float kScrubThreshold = 3.0f;
constexpr double kTypeaheadReset = 0.5;

enum class Kind
{
    Box,
    Root,
    Body,
    Button,
    Number,
    Check,
    Toggle,
    Select,
    Text,
    Search,
    Color,
    Panel,
    Prop,
    Row,
    Popup,
    MenuItem,
    Submenu,
    Trigger,
    Splitter,
    Surface,
    Command,
    CommandItem,
    List,
};

enum : unsigned
{
    EV_CLICK = 1 << 0,
    EV_DOUBLE = 1 << 1,
    EV_CONTEXT = 1 << 2,
    EV_PRESS = 1 << 3,
    EV_TWISTY = 1 << 4,
};

struct Node;

struct Widget
{
    bool open = false;
    bool open_init = false;
    bool pending = false;
    bool committed = false;
    bool editing = false;
    bool scrubbing = false;
    bool pressing = false;
    bool suppress_click = false;
    bool focused = false;
    bool invalid = false;
    double value = 0;
    double start = 0;
    double pending_value = 0;
    int pending_index = -1;
    int step_dir = 0;
    double step_next = 0;
    float press_x = 0;
    float press_y = 0;
    kit_number_desc desc{};
    /* "\x01" forces the first frame to apply visibility for empty values. */
    std::string unit = "\x01", addon = "\x01", addon_cls, draft, applied, label = "\x01", meta = "\x01", badge = "\x01", placeholder;
    std::vector<std::string> labels;
    uint32_t icon = UINT32_MAX;
    uint32_t color = 0;
    float hsv[3] = { 0, 0, 0 };
    int drag_part = 0; /* color picker: 1 plane, 2 hue */
    bool at_bottom = false; /* list: pinned to the bottom */
    Rml::Element *body = nullptr; /* list content */
    Rml::Element *value_el = nullptr, *fill_el = nullptr, *input_el = nullptr, *addon_el = nullptr,
                 *unit_el = nullptr, *label_el = nullptr, *icon_el = nullptr, *meta_el = nullptr,
                 *twisty_el = nullptr, *kbd_el = nullptr, *badge_el = nullptr, *head_el = nullptr,
                 *swatch_el = nullptr;
};

/* Motion (kit_motion.cpp). A spring's state along one axis, as displacement
 * from its target; see spring_step. */
struct Spring
{
    double x = 0, v = 0;
};

struct SpringParams
{
    double stiffness = 223.8, damping = 29.9, mass = 1; /* visual duration 0.35 s, no bounce */
};

/* Text in place (kit_text.cpp). A NumberFlow wheel position is in cells of a
 * 20-cell strip (0-9 twice), a spring around the target cell. */
struct FlowDigit
{
    Rml::Element *col = nullptr, *strip = nullptr;
    int place = 0; /* units 0, tens 1, tenths -1 */
    double target = 0;
    Spring pos;
    double appear = 1; /* 0 hidden to 1 shown */
    bool leaving = false;
};

struct FlowState
{
    bool init = false, grouping = false;
    double value = 0;
    std::string text, prefix, suffix;
    std::vector<FlowDigit> digits;
    std::vector<Rml::Element *> symbols;
    float digit_w = 0;
};

struct MorphSeg
{
    std::string id;
    Rml::Element *el = nullptr;
    enum
    {
        Stay,
        Enter,
        Leave,
    } state = Stay;
    float from_x = 0, from_y = 0, dx = 0, dy = 0;
};

struct MorphState
{
    bool init = false, measure = false;
    std::string text;
    std::vector<MorphSeg> segs;
    Rml::Element *box = nullptr;
    double start = -1;
    float from_w = 0, to_w = 0;
};

/* Charts (kit_chart.cpp): what the chart element draws this frame. */
struct ChartSeries
{
    std::vector<float> times, values;
    uint32_t color = 0;
};

struct ChartFrame
{
    std::vector<ChartSeries> series;
    std::vector<double> ticks;
    double now = 0, lo = 0, hi = 1, hover_value = 0;
    float window = 30, dp = 1, reference = 0, hover_x = -1;
    bool fill = false, live = false, reduced_motion = false;
};

struct ChartState
{
    bool ranged = false;
    double lo = 0, hi = 1, time = 0;
    std::vector<Rml::Element *> labels;
    Rml::Element *badge = nullptr, *hover = nullptr;
};

struct Node
{
    std::string key;
    std::string path;
    Kind kind = Kind::Box;
    Rml::Element *el = nullptr;
    Rml::Element *container = nullptr; /* where tracked children go */
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    size_t cursor = 0;
    uint64_t frame = 0;
    std::string cls, text, tip, shortcut, tag;
    std::unordered_map<std::string, std::string> attrs, styles;
    unsigned events = 0;
    Widget w;
    uint64_t created = 0;  /* frame of creation */
    bool presence = false; /* kept with data-ending-style until its transitions finish */
    bool layout = false;   /* position changes animate (FLIP) */
    bool layout_known = false;
    kit_rect layout_rect{};
    Spring layout_x, layout_y;
    std::unique_ptr<FlowState> flow;
    std::unique_ptr<MorphState> morph;
    std::unique_ptr<ChartState> chart;
    std::string drop_type; /* accepted drag type, set each frame by kit_drop_target */
};

/* A toast (kit_toast.cpp). Positions are springs around their targets, in dp. */
struct Toast
{
    int id = 0;
    kit_toast_type type = KIT_TOAST_DEFAULT;
    std::string title, description, action;
    double duration = 4, remaining = 4; /* seconds; a negative duration stays until closed */
    bool placed = false, removing = false, swiping = false, swiped = false, forced_height = false;
    float height = 0, press_x = 0, swipe_velocity = 0;
    double y_target = 0, x_target = 0, scale_target = 1, opacity_target = 1;
    Spring y, x, scale, opacity;
};

/* The drag in progress (kit_drag.cpp). */
struct DragState
{
    bool pending = false, active = false, dropped = false;
    std::string type, payload, label, source, over, target;
    uint32_t icon = 0;
    float x0 = 0, y0 = 0;
    double t0 = 0;
};

/* A value animated by kit_spring. */
struct SpringValue
{
    double target = 0, time = 0;
    Spring s;
    SpringParams params;
    float duration = 0;
    uint64_t frame = 0;
};

struct Exiting
{
    std::unique_ptr<Node> node;
    double until = 0;
};

struct Popup
{
    bool open = false;
    bool placed = false;
    bool at_point = false;
    bool keyboard = false;
    bool activated = false;
    bool menubar = false;
    float x = 0, y = 0;
    kit_rect anchor{};
    std::string parent; /* submenu parent popup */
    int highlight = -1;
    std::string sub_hover;
    double sub_hover_since = 0;
    std::vector<Node *> items;
    Node *node = nullptr;
        std::string typed;
    double typed_at = 0;
    float content = 0;       /* widest row this frame, dp; sizes the menu */
    float content_shown = 0; /* the width applied */
};

struct CommandState
{
    bool open = false;
    bool focus = false;
    std::string placeholder;
    std::string query;
    int selected = 0;
    int chosen = -1;
    bool reveal = false; /* keyboard moved the selection; scroll it into view */
    std::vector<int> shown; /* original indices in display order */
    Rml::Element *input = nullptr;
};

/* Markup and formatting. */
std::string escape(const char *text);
std::string utf8(uint32_t cp);
std::string format_number(double v, int precision, bool integer);
bool rect_contains(kit_rect r, float x, float y);

} // namespace kit_impl

struct kit
{
    Rml::Context *context = nullptr;
    Rml::ElementDocument *doc = nullptr;
    std::unique_ptr<KitListener> listener;
    std::unordered_map<std::string, std::unique_ptr<kit_impl::Node>> roots;
    std::unordered_map<Rml::Element *, kit_impl::Node *> by_element;
    std::unordered_map<std::string, kit_impl::Node *> by_path;
    std::vector<kit_impl::Node *> stack;
    kit_impl::Node *last = nullptr;
    kit_impl::Node *root = nullptr;
    kit_impl::Node *layer = nullptr;
    uint64_t frame = 1;
    kit_frame_input in{};
    std::map<std::string, kit_impl::Popup> popups;
    std::vector<std::string> popup_stack; /* open order, top last */
    std::vector<kit_impl::Node *> triggers;
    Rml::Element *tooltip = nullptr;
    Rml::Element *tip_target = nullptr;
    double tip_since = 0;
    double tip_warm_until = 0;
    bool tip_visible = false;
    uint64_t tip_shown_frame = 0;
    kit_impl::CommandState command;
    Rml::Element *command_backdrop = nullptr;
    std::string surface_capture;
    int surface_button = -1;
    std::vector<std::string> emitting; /* popups being emitted, innermost last */
    bool keyboard_edit = false;
    std::deque<std::string> strings; /* KIT_CX results, valid until kit_end */
    bool reduced_motion = false;
    double motion_time = 0;
    std::vector<kit_impl::Node *> entering; /* data-starting-style set at creation */
    std::vector<kit_impl::Exiting> exiting;
    std::vector<kit_impl::Node *> layout_nodes;
    std::unordered_map<std::string, kit_impl::SpringValue> springs;
    std::vector<kit_impl::Node *> text_motion; /* NumberFlow and Torph in flight */
    bool layout_pass = false;                  /* measure layout before this frame renders */
    kit_impl::DragState drag;
    std::vector<kit_impl::Toast> toasts;
    std::unordered_map<int, uint64_t> toast_actions; /* id -> frame the action was pressed */
    int toast_next = 0;
    double toast_time = 0;
    float front_height = 0;
    bool toasts_expanded = false;
};

namespace kit_impl {

/* Geometry in logical pixels. */
float dp(const kit *k);
kit_rect element_rect(const kit *k, Rml::Element *el);

/* Reconciled attribute writes: each touches the element only on change. */
void set_class(Node *n, const std::string &cls);
void set_attr(Node *n, const std::string &name, const std::string &value);
void set_flag(Node *n, const std::string &name, bool on);
void flag_element(Rml::Element *el, const char *name, bool on);
void set_text(Node *n, const char *text);
void set_element_text(Rml::Element *el, const std::string &text);

/* Untracked child elements owned by a widget. */
Rml::Element *raw(kit *k, Rml::Element *parent, const char *tag, const char *cls, const char *text = nullptr);
Rml::Element *raw_icon(kit *k, Rml::Element *parent, const char *cls, uint32_t cp);

/* Keyed emission. */
void unmap(kit *k, Node *n);
void prune(kit *k, Node *n);
Node *emit_in(kit *k, Node *parent, const char *key, Kind kind, const char *tag, const char *cls);
Node *emit(kit *k, const char *key, Kind kind, const char *tag, const char *cls);
void push_node(kit *k, Node *n);
void pop_node(kit *k);
Node *node_for(kit *k, Rml::Element *el);
bool within(Rml::Element *el, Rml::Element *ancestor);

/* Motion. */
void spring_step(Spring &s, const SpringParams &p, double dt);
SpringParams spring_params(double visual_duration, double bounce);
bool spring_resting(const Spring &s);
/* Remove a child element, or keep a presence node until its exit transitions end. */
void retire(kit *k, std::unique_ptr<Node> node);
void presence(kit *k, Node *n);
void motion_frame(kit *k);
void toasts_frame(kit *k);
void drag_begin_frame(kit *k);
void drag_end_frame(kit *k);
void drag_cancel(kit *k);
void register_chart_element();
void register_wires_element();
void register_dot_element();
void text_frame(kit *k, double dt);
void morph_label(kit *k, Node *n, Rml::Element *box, const char *text);
void forget_motion(kit *k, Node *n);

/* Popups. */
void close_popup(kit *k, const std::string &key);
void close_all_popups(kit *k);
void open_popup(kit *k, const std::string &key, kit_rect anchor, bool at_point, float x, float y, const std::string &parent);

} // namespace kit_impl

#endif
