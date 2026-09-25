#ifndef CHIBI_EDITOR_STUDIO_GRAPH_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_GRAPH_INTERNAL_H

/* Shared by the graph canvas modules (studio_graph*.c). */
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor_internal.h"

enum
{
    CODE_LINES = 12,
};

static const float ROW = 22; /* graph.rcss row pitch */
static const float SOCKET_PAD = 10, PART_GAP = 6, EDGE = 1;
/* Below this many device pixels per graph unit only titles show: 12 dp pin
   labels would draw under 7 px. Zoom alone would hide legible text on HiDPI. */
static const float FAR_PIXELS = 0.6f;

typedef enum
{
    FORM_FULL,
    FORM_OPERATOR,
    FORM_GETTER,
} node_form;

struct studio_node_layout
{
    uint64_t id;
    node_form form;
    float w, h;
    float label;           /* input label column, so input editors line up */
    float top;             /* the first row's top, below the node's */
    float column;          /* an operator's input column */
    int head_in, head_out; /* pins drawn in the title, or -1 */
    int code_lines;        /* an expression's source lines */
};
typedef struct studio_node_layout node_layout;

typedef enum
{
    VALUE_NONE,
    VALUE_CHOICE,
    VALUE_BOOL,
    VALUE_VECTOR,
    VALUE_TEXT,
    VALUE_NUMBER,
    VALUE_CHIP,
} value_kind;

typedef struct
{
    const pin_spec *spec;
    bool connected;
    bool inline_source;
    bool show_label;
    char label[128];
    char chip[160];
    uint32_t chip_icon;
    value_kind value;
} pin_parts;

/* Model access (studio_graph.c). */
editor *sg_graph_editor(const studio *s);
graph *sg_active_graph(const studio *s);
bool sg_graph_readonly(const studio *s);
bool sg_node_visible(const graph *g, const node *n);
node *sg_node_by_id(const studio *s, uint64_t id);
graph_comment_box *sg_comment_by_id(const studio *s, uint64_t id);
void sg_link_ref_of(const edge *ed, ed_link_ref *out);
bool sg_edge_ends(const graph *gr, const edge *ed, node **from, int *from_pin, node **to, int *to_pin);

/* Node layout and measurement (studio_graph_layout.c). */
bool sg_pin_exec(const node *n, int kind, int pin);
bool sg_in_head(const node_layout *l, int kind, int pin);
bool sg_pin_in_rows(const graph *g, const node *n, const node_layout *l, int kind, int pin);
int sg_pin_order(const graph *g, const node *n, const node_layout *l, int kind, int pin);
int sg_exec_rows(const graph *g, const node *n, const node_layout *l);
int sg_row_count(const graph *g, const node *n, const node_layout *l);
const char *sg_node_kind(const studio *s, const node *n);
uint32_t sg_node_icon(const studio *s, const node *n);
value_kind sg_value_kind_of(const studio *s, node *n, int output, int p);
void sg_pin_parts_of(const studio *s, node *n, int kind, int p, pin_parts *out);
const char *sg_node_title(const studio *s, node *n, node_form form, char *buf, size_t size);
int sg_code_lines(const studio *s, const char *code, float width, int starts[CODE_LINES]);
float sg_code_room(const node_layout *l);
float sg_text_width(const studio *s, const char *text, float size, int weight);
const node_layout *sg_layout_of(const studio *s, node *n);
float sg_pin_y(const studio *s, node *n, int kind, int pin);
bool sg_node_box(const studio *s, node *n, float box[4]);
void sg_socket_at(const studio *s, node *n, int kind, int pin, float *x, float *y);

/* Camera and arrangement (studio_graph_view.c). */
void sg_to_world(const studio_graph *g, float wx, float wy, float *x, float *y);
void sg_zoom_at(studio_graph *g, float zoom, float wx, float wy);
/* Zoomed out past FAR_PIXELS: nodes show titles only. */
bool sg_far(const studio *s);
void sg_camera_step(studio *s);
void sg_frame_nodes(studio *s, bool selection, bool animate);
/* Frames the selection holding n, or pans n into view unless it is in view. */
void sg_show_node(studio *s, node *n, bool frame);
void sg_follow_graph(studio *s);
void sg_update_routes(studio *s);
void sg_arrange(studio *s, bool automatic_only);

/* Edits (studio_graph_edit.c). */
void sg_comment_contents(studio *s, uint64_t id, bool frame_only);
void sg_frame_selection(studio *s);
bool sg_connect_pins(studio *s, node *a, int a_kind, int a_pin, node *b, int b_kind, int b_pin);
bool sg_pins_compatible(const node *a, int a_kind, int a_pin, const node *b, int b_kind, int b_pin);
int sg_body_target(const graph *gr, const node *from, int kind, int pin, const node *to);
void sg_show_error(studio *s, const char *message);
void sg_select_only(editor *e, node *n);
void sg_delete_selection(studio *s);
void sg_copy_selection(studio *s);
void sg_paste(studio *s);
bool sg_is_unused(const studio_graph *g, uint64_t id);
void sg_find_unused(studio *s);
void sg_collapse_to_function(studio *s);
void sg_convert_to_expression(studio *s);
void sg_optimize(studio *s);
void sg_collapse_math(studio *s);

/* Input (studio_graph_input.c). */
void sg_replay(studio *s);
void sg_pointer(studio *s);
void sg_shortcuts(studio *s);
void sg_cancel_gesture(studio *s);

/* Comment frames and node comments (studio_graph_comment.c). */
enum
{
    SG_EDGE_LEFT = 1,
    SG_EDGE_TOP = 2,
    SG_EDGE_RIGHT = 4,
    SG_EDGE_BOTTOM = 8,
};
void sg_emit_comment(studio *s, graph_comment_box *box);
void sg_comment_menu(studio *s, graph_comment_box *box);
void sg_edit_title(studio *s, uint64_t comment);
void sg_begin_resize(studio *s, graph_comment_box *box, unsigned edges);
void sg_resize(studio *s);
void sg_end_resize(studio *s, bool cancel);
const char *sg_resize_cursor(unsigned edges);
void sg_emit_bubble(studio *s, node *n);
void sg_edit_bubble(studio *s, uint64_t node_id);
bool sg_node_comment_items(studio *s, node *n);
void sg_comment_properties(studio *s, node *n);

/* Find in graph (studio_graph_find.c). */
void sg_find_open(studio *s);
void sg_find_panel(studio *s);

/* Navigation (studio_graph_nav.c). */
/* Back, Forward and the crumbs up to the open graph's own, which the caller
   emits before closing the crumbs with kit_pop. */
void sg_nav_header(studio *s);
void sg_nav_keys(studio *s);

/* Rendering (studio_graph_draw.c). */
/* A pin's socket color as 0xRRGGBBAA. */
uint32_t sg_pin_color(const studio *s, const pin_spec *spec);
void sg_emit_value_in(studio *s, kit *k, node *n, int output, int p);
void sg_emit_canvas(studio *s);

#endif
