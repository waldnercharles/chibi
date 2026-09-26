#ifndef CHIBI_EDITOR_STUDIO_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_INTERNAL_H

#include "editor/documents/component_presentation.h"
#include "editor/studio/studio.h"
#include "editor/ui/kit.h"
#include "editor/ui/kit_icons.h"
#include "editor/workspace/asset_index.h"
#include "editor/workspace/scene_internal.h"
#include "graph/authoring/arrange.h"
#include "render/ui.h"
#include "runtime/save.h"
#include "support/file_watch.h"
#include "ui/rml_backend.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

typedef enum
{
    STUDIO_EDITOR_SCENE,
    STUDIO_EDITOR_BEHAVIOR,
    STUDIO_EDITOR_OUTLINER,
    STUDIO_EDITOR_PROPERTIES,
    STUDIO_EDITOR_OUTPUT,
    STUDIO_EDITOR_ASSETS,
    STUDIO_EDITOR_PERFORMANCE,
    STUDIO_EDITOR_MEMBERS,
    STUDIO_EDITOR_PANEL, /* an authored panel of the Studio project, named in studio_prefs.panels */
    STUDIO_EDITOR_COUNT,
} studio_editor;

typedef enum
{
    STUDIO_AREA_TOP,
    STUDIO_AREA_BOTTOM,
    STUDIO_AREA_OUTLINER,
    STUDIO_AREA_PROPERTIES,
    STUDIO_AREA_DRAWER, /* collapsible, at the bottom of the main column */
    STUDIO_AREA_COUNT,
} studio_area_slot;

typedef enum
{
    STUDIO_TAB_SCENE,
    STUDIO_TAB_OBJECT,
    STUDIO_TAB_PHYSICS,
    STUDIO_TAB_MATERIAL,
    STUDIO_TAB_GAMEPLAY,
    STUDIO_TAB_BEHAVIORS,
    STUDIO_TAB_NODE,   /* the graph node selected in the Behavior area */
    STUDIO_TAB_MEMBER, /* the member selected in Members */
    STUDIO_TAB_COUNT,
} studio_tab;
_Static_assert(
    STUDIO_TAB_GAMEPLAY - STUDIO_TAB_OBJECT + 1 == COMPONENT_TAB_COUNT,
    "Object through Gameplay follow component_tab"
);

typedef enum
{
    STUDIO_THEME_SYSTEM,
    STUDIO_THEME_DARK,
    STUDIO_THEME_LIGHT,
    STUDIO_THEME_COUNT,
} studio_theme;

/* Editor preferences (studio_prefs.c). The one store for view settings and the
 * area layout; everything else is authoring state owned by the C model. */
typedef struct
{
    studio_theme theme;
    bool comfortable;
    bool reduced_motion;
    float ui_scale; /* the editor's size, as Blender's Resolution Scale; games' UI keeps its own */
    float side_width;
    float main_split;
    float side_split;
    bool drawer_open;
    float drawer_height;
    studio_editor editors[STUDIO_AREA_COUNT];
    char panels[STUDIO_AREA_COUNT][64]; /* the panel each STUDIO_EDITOR_PANEL area shows */
    kit_wire_style wires;               /* graph wire routing */
    bool short_bindings; /* graph chips name "Aim › Manual", not "Self.GameAim.manual" */
    bool title_flow;    /* unlabeled execution pins sit in node titles */
    bool compact_nodes; /* operators as symbols, one-output reads as pills */
} studio_prefs;

typedef struct
{
    kit_rect rect;
} studio_area;

/* One frame of Performance samples. */
typedef struct
{
    float time;
    float frame_ms;
    float ms[4]; /* behavior, physics, systems, rendering while the scene runs */
    bool running;
} studio_perf_sample;

/* One entry of the operation history shown by Output's Info view. */
typedef struct
{
    double time;
    kit_toast_type kind;
    char text[256];
} studio_record_entry;

/* A node where it was authored: the behavior or library holding it, its graph
 * and its id (0 for the graph alone). */
typedef struct
{
    char asset[128];
    uint64_t graph_id, node_id;
} studio_node_ref;

/* Runs of the playing scene that failed at one node with one message. */
typedef struct
{
    char behavior[128];
    studio_node_ref at;
    char node[96];
    char message[256];
    int count;
} studio_runtime_error;

/* A compile diagnostic or runtime failure drawn on a node of the open graph. */
typedef struct
{
    uint64_t node;
    bool error; /* else a warning */
    char text[256];
} studio_node_problem;

typedef struct
{
    SDL_Keycode key;
    SDL_Keymod mod; /* at press time: a quick chord is released before the frame runs */
    bool repeat; /* an auto-repeat of a held key */
} studio_key;

typedef enum
{
    STUDIO_GRAPH_IDLE,
    STUDIO_GRAPH_PRESS,   /* on a node, below the drag threshold */
    STUDIO_GRAPH_MOVE,    /* dragging the selected nodes */
    STUDIO_GRAPH_BOX,     /* marquee selection */
    STUDIO_GRAPH_PAN,     /* middle, Space or right drag */
    STUDIO_GRAPH_LINK,    /* dragging a wire from a socket */
    STUDIO_GRAPH_RIGHT,   /* right press, below the pan threshold */
    STUDIO_GRAPH_REROUTE, /* dragging a wire's reroute point */
    STUDIO_GRAPH_RESIZE,  /* dragging a comment frame's edge or corner */
} studio_graph_gesture;

typedef struct
{
    uint64_t graph_id;
    float zoom, x, y;
} studio_graph_view;

/* A Find in graph result (studio_graph_find.c). */
typedef struct
{
    uint64_t graph_id; /* the graph's saved ID */
    uint64_t node_id; /* the node drawn for the match: an inline match's owner */
    uint32_t icon;
    char title[96];
    char text[160]; /* what matched */
} studio_find_hit;

/* A graph Back and Forward return to (studio_graph_internal.h). */
typedef struct studio_visit studio_visit;

/* A Find References result in Output (studio_find.c). */
typedef struct
{
    int kind; /* asset_index_kind of the referencing asset */
    char asset[128];
    uint64_t graph_id, node_id; /* 0 for an asset-level reference */
    char place[160];            /* graph or object */
    bool object;                /* place names a scene object */
    const char *use;            /* Call, Read, Handles... */
} studio_reference;

enum
{
    STUDIO_VALUE_NAMES = 8, /* outputs per node the Outputs panel can name */
};

/* The behavior graph canvas (studio_graph*.c): its own RmlUi context whose dp
 * ratio is the zoom, so text stays crisp and clipping works at every scale. The
 * canvas logical space is graph units; `x, y` is the graph point at the canvas
 * top-left. */
typedef struct
{
    rml_backend *backend;
    kit *kit;
    kit_frame_input in;
    rml_input *events; /* window events replayed into the canvas */
    kit_surface_input surface;
    int area;
    bool focused;
    bool captured; /* a press began in the canvas and is held */
    float zoom, x, y;
    struct
    {
        float zoom, x, y;
        float from_zoom, from_x, from_y;
        double start;
        bool active;
    } camera;
    studio_graph_view *views; /* camera per graph, restored on return */
    uint64_t graph_id;
    bool framed;
    studio_graph_gesture gesture;
    float press_x, press_y;   /* window px */
    float press_wx, press_wy; /* the press in graph units */
    char press_path[96];      /* hovered kit path when the button went down */
    bool press_control;       /* the press landed on a field or button */
    int press_button;         /* -1 when no press arrived this frame */
    float world_x, world_y;   /* pointer in graph units */
    uint64_t press_node;
    uint64_t press_comment; /* a comment frame grabbed by its title or an edge */
    float comment_x, comment_y, comment_w, comment_h; /* its bounds at the press */
    unsigned resize_edges; /* the SG_EDGE_* a resize moves */
    uint64_t title_edit;   /* the frame whose title is being edited, or 0 */
    uint64_t bubble_edit;  /* the node whose comment is being edited, or 0 */
    bool comment_focus;    /* focus the comment field on its first frame */
    uint64_t pin_edit;     /* the expression input being renamed, or 0 */
    bool pin_focus;        /* focus its field on the first frame */
    uint64_t menu_comment;
    uint64_t comment; /* the selected comment frame, shown while no node is */
    bool press_toggle;
    struct
    {
        uint64_t id;
        float x, y;
    } *drag;
    float box[4];       /* marquee corners in graph units */
    uint64_t *box_base; /* selection before an additive marquee */
    uint64_t link_node;
    int link_kind; /* ED_PIN_INPUT or ED_PIN_OUTPUT */
    int link_pin;
    char link_error[160];
    uint64_t hover_node;
    int hover_kind, hover_pin; /* -1 without a socket */
    int hover_edge;            /* edge index under the pointer, or -1 */
    bool space;
    double value_edit_frame; /* a value gesture has an open undo snapshot */
    bool value_editing;
    uint64_t measured_key;
    arrange_routes routes;
    int *edge_route; /* per edge: its link in routes, or -1 */
    int *wire_edges; /* per drawn wire: its edge index */
    struct studio_node_layout *layouts; /* nodes measured this frame */
    int reroute_edge, reroute_point;    /* the reroute being dragged */
    float reroute_x, reroute_y;         /* where it was before the drag */
    bool reroute_added;                 /* the drag began by adding it */
    int hover_reroute;   /* edge * GRAPH_EDGE_MAX_REROUTES + point, or -1 */
    int insert_edge;     /* the wire a dragged node would join, or -1 */
    uint64_t names_node; /* node whose output names the Outputs panel edits */
    unsigned long long names_revision;
    char names[STUDIO_VALUE_NAMES][64];
    bool renaming; /* the name of rename_graph is being edited */
    uint64_t rename_graph;
    int rename_area; /* the area whose header holds the name field */
    bool rename_focus;
    char rename[128];
    int forms;          /* node settings the layouts follow */
    bool spawn_pending; /* the add-node palette spawns at spawn_x, spawn_y */
    float spawn_x, spawn_y;
    bool double_click; /* the press this frame completes a double click */
    double pan_time;   /* last auto-pan step */
    double last_press_time;
    float last_press_x, last_press_y;
    uint64_t menu_node; /* context menu target, or 0 */
    int menu_edge;      /* context menu wire, or -1 */
    bool menu_request; /* open the context menu at menu_x, menu_y (window px) */
    float menu_x, menu_y;
    uint64_t *unused; /* nodes no event reaches or whose values nothing reads */
    uint64_t expression_node; /* the expression being edited in Properties */
    char expression[512];
    char expression_source[512]; /* the node's source when expression was read */
    char expression_error[192];
    struct
    {
        int toast;   /* offers Keep Wire or Undo after a source edit */
        int history; /* the graph's undo depth when offered */
        uint64_t node;
        char from[128]; /* the renamed input, or empty for Undo */
        char to[128];
        char source[512];
    } rewire;
    studio_node_problem *problems; /* this frame's, by node */
    uint64_t reveal;   /* node to show once the canvas is laid out */
    bool reveal_frame; /* frame it, rather than pan it into view */
    struct
    {
        bool open;
        bool focus;  /* focus the field on the next frame */
        bool typing; /* the field had focus last frame */
        char query[128];
        char seen[128]; /* the query the results follow */
        int current;    /* the result shown in the graph, or -1 */
        studio_find_hit *hits;
    } find;
    studio_visit *visits; /* graphs visited, for Back and Forward */
    int visit;            /* the current one */
    uint64_t menu_pin;    /* the menu's pin on menu_node, or 0 */
    bool box_subtract;    /* a Ctrl marquee removes what it touches */
    bool frame_pending;   /* F frames on release unless it placed a node */
} studio_graph;

typedef enum
{
    STUDIO_MEMBER_NONE,
    STUDIO_MEMBER_GRAPH,
    STUDIO_MEMBER_VARIABLE,
    STUDIO_MEMBER_LOCAL, /* a named value of the open graph */
} studio_member_kind;

/* The Members editor (studio_members.c): the open behavior's graphs,
   variables and named values, after Unreal's My Blueprint. */
typedef struct
{
    studio_member_kind kind; /* the selected member */
    uint64_t id;             /* its graph, variable or local ID */
    uint64_t shown_node;     /* graph node selection last seen */
    studio_member_kind rename_kind;
    uint64_t rename_id;
    bool rename_focus;
    char filter[128];
    unsigned collapsed; /* bit per section */
    bool value_editing; /* a default value gesture has an open undo step */
    struct
    {
        int toast;        /* offers Undo while the delete is the latest edit */
        const char *step; /* the delete's undo step in the graph editor */
    } undo;
    struct
    {
        bool open;       /* the type search is choosing for this target */
        uint64_t id;     /* the variable, or the function whose pin it is */
        uint64_t pin_id; /* the Entry or Return pin, or 0 for the variable */
    } pick;
} studio_members;

/* The open project's editor/Components.yaml, re-read when it changes. */
typedef struct
{
    component_presentation hints; /* the last valid file; empty while it is missing */
    file_watch watch;
} studio_presentation;

/* The editor's own UI project: its panels, widgets and theme. */
#define STUDIO_PROJECT CHIBI_WORKSPACE_DIR "/projects/studio"

/* The editor's authored UI world (studio_ui.c). */
typedef struct studio_ui studio_ui;

/* A toast offering to reload a file over unsaved edits. */
typedef struct
{
    int toast;
    char *path;
} studio_disk_toast;

/* A project the workspace offers to open. */
typedef struct
{
    char *root; /* as chibi_project_open resolves it */
    char *name;
} studio_project;

struct studio
{
    app_scene_authoring *scene;
    engine_viewport *viewport;
    const char *project_root; /* the open project, owned by the application */
    bool own_project; /* the open project is the Studio project, whose settings are these preferences */
    uint64_t scene_prefs_seen, preview_prefs_seen;
    studio_project *projects;
    char *open_project; /* root chosen to open, until the application takes it */
    studio_disk_toast *disk_toasts; /* files that changed on disk under unsaved edits */
    int discard_toast;   /* toast confirming unsaved changes are discarded */
    int discard_project; /* the project that toast opens */
    rml_backend *backend;
    kit *kit;
    studio_ui *ui;  /* NULL when the Studio project could not load */
    ui_theme theme; /* the Studio project's, which every Studio document uses */
    kit_frame_input in;
    studio_area areas[STUDIO_AREA_COUNT];
    int hover_area;
    int focus_area;
    int maximized; /* the area filling the workspace (Blender's Ctrl Space), or -1 */
    studio_prefs prefs;
    studio_prefs applied; /* last applied copy; changes apply and save by comparison */
    double prefs_changed_at;
    bool system_light;
    bool mac; /* macOS conventions: Command shortcuts and ⌘ notation */
    bool light;
    int theme_switching; /* frames left with transitions suspended */
    bool drawer_dragging;
    SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];
    int cursor_shown; /* SDL_SystemCursor applied, -1 before the first frame */
    kit_rect main_rect;
    kit_rect side_rect;
    studio_tab tab;
    uint64_t shown_node; /* graph node the Properties tabs last followed */
    int shown_actor;     /* scene selection the Properties tabs last followed */
    studio_record_entry *records;
    studio_runtime_error *runtime_errors;
    bool session;          /* the scene or a preview ran last frame */
    uint64_t compiled_key; /* the revision the open behavior's problems describe */
    struct
    {
        bool active;        /* a run is paused at a breakpoint or step */
        studio_node_ref at; /* its current node */
        const void *run, *node; /* the run and node last shown; never dereferenced */
        unsigned long steps;
        bool holding; /* the pause chose the debug target */
        bool pinned;  /* the debug target's pin before that */
    } pause;
    studio_perf_sample *perf;
    double perf_readout_time;
    char perf_readout[96];
    int assets_kind;           /* asset_index_kind, or -1 for all */
    unsigned assets_collapsed; /* bit per asset_index_kind */
    char assets_filter[128];
    int assets_menu;             /* the asset under the row menu */
    char dropped_prototype[256]; /* placed by the viewport this frame */
    int output_view;
    char output_filter[128];
    char references_title[160]; /* what Output's Find view lists references to */
    studio_reference *references;
    char outliner_filter[128];
    bool outliner_collapsed;
    bool outliner_reveal; /* scroll the selection into view on the next frame */
    char property_search[128];
    bool text_input;
    kit_surface_input surface;
    studio_graph graph;
    studio_members members;
    uint64_t shown_member; /* member the Properties tabs last followed */
    studio_presentation presentation;
    struct
    {
        uint64_t entity;
        ecs_component_id component;
        int field;
    } context;                /* property under the field context menu */
    char prototype_edit[192]; /* component.field of the prototype gesture in progress */
    SDL_Event *events; /* queued by the SDL event watch, replayed in order */
    studio_key *keys;  /* keys pressed this frame */
    SDL_Keymod mods;   /* modifier state as of the last replayed key event */
    int nav_button; /* mouse button 4 (-1, Back) or 5 (1, Forward) this frame */
    int undo_toast; /* toast offering Undo for undo_toast_group */
    uint64_t undo_toast_group;
    char status[256];
    char seen_message[256]; /* last authoring message reported */
    double status_time;
    double time;
    double active_until; /* frames run at full rate until then, after input */
};

/* Whether frames run at full rate for input or a playing scene, rather than
 * only while the screen changes. */
bool studio_active(const studio *s);
/* Physical pixels per editor dp: the display's scale times UI Scale. */
float studio_pixel_ratio(const studio *s);

/* Platform shortcuts (studio_keys.c). */
bool studio_detect_mac();
bool studio_primary(const studio *s, SDL_Keymod mod);
const char *studio_chord(const studio *s, const char *chord);

/* Preferences. */
studio_prefs studio_prefs_default();
studio_prefs studio_prefs_load();
bool studio_prefs_save(const studio_prefs *prefs);
bool studio_prefs_equal(const studio_prefs *a, const studio_prefs *b);
/* Shares the preferences with authored panels as saved keys: a change a panel
 * made since `seen` applies first, then the preferences are written back. */
void studio_prefs_share(studio_prefs *prefs, game_save *save, uint64_t *seen);

/* A theme custom property a Theme panel row shows (studio_sources.c). */
typedef struct studio_token
{
    int file;       /* index in the theme's watches */
    int start, end; /* the value's bytes in the file when read */
    char name[64];
    char *value; /* as read */
    bool color;
} studio_token;

/* What authored panels bind beyond their world's `save` (studio_sources.c):
 * `world`, the Scene world's saved keys, and `theme`, the open project's
 * theme tokens. */
typedef struct studio_sources
{
    game_save *save; /* the one `world` shows */
    uint64_t save_seen;
    char content_root[1024]; /* whose theme `theme` shows */
    ui_theme theme;
    studio_token *tokens; /* in the order `theme` lists its rows */
    uint64_t writes_seen;
} studio_sources;

/* Adds the `world` and `theme` variables to a data model's JSON. */
void studio_sources_add(yyjson_mut_doc *doc);
/* Applies what bindings changed, then shows the current values. */
void studio_sources_sync(studio *s, studio_sources *sources, ui_data *data);
void studio_sources_free(studio_sources *sources);

/* Authored UI. Loads the Studio project's scene; NULL with an error when it
 * cannot. Free it after the kit, whose hosts hold its listener. */
studio_ui *studio_ui_new(rml_context *context, const char *project_root, char *error, size_t size);
void studio_ui_free(studio_ui *ui);
/* Steps the panels' behaviors and shares preferences; call after input. */
void studio_ui_frame(studio *s);
/* Shows the Studio scene entity `name`'s GameUi document in a kit host. */
void studio_ui_panel(studio *s, const char *key, const char *name);
/* The Studio scene's panels, each an editor type an area can show. */
int studio_ui_panel_count(const studio *s);
const char *studio_ui_panel_name(const studio *s, int index);
/* Shows panel `name` filling area `area`'s body, whether or not its GameUi
 * is hidden. */
void studio_ui_area(studio *s, int area, const char *name);
/* Shows it over the editor while its GameUi is not hidden, as for a dialog
 * whose behaviors hide it again. */
void studio_ui_modal(studio *s, const char *name);
/* Clears its GameUi.hidden. */
void studio_ui_show(studio *s, const char *name);

/* Content changed on disk (studio_disk.c): reloads it and raises failures and
   unsaved conflicts as toasts. */
void studio_disk_frame(studio *s);
void studio_disk_free(studio *s);

/* Projects (studio_projects.c). */
#ifdef __EMSCRIPTEN__
/* The browser keeps projects itself, without the local server (studio_shell.c). */
bool web_projects_available();
#endif
void studio_projects_load(studio *s);
void studio_projects_free(studio *s);
bool studio_project_current(const studio *s, int index);
/* Opens projects[index] after the frame, once unsaved changes are discarded. */
void studio_open_project(studio *s, int index);
void studio_projects_frame(studio *s);
/* File > Open Project. */
void studio_projects_menu(studio *s);

/* Shell. */
void studio_emit(studio *s);
void studio_report(studio *s, const char *message);
void studio_shortcuts(studio *s);
const char *studio_scene_title(const app_scene_authoring *scene, char *out, size_t size);

/* Some open document has edits that are not saved. */
bool studio_unsaved(studio *s);
/* Scene edits are allowed: the scene can move and shows no runtime snapshot. */
bool studio_editable(const studio *s);
/* Case-insensitive substring filter; an empty query matches everything. */
bool studio_matches(const char *query, const char *text);

/* Editors emit into an area body; headers into the area header. */
void studio_scene_header(studio *s, int area);
void studio_scene_body(studio *s, int area);
void studio_outliner_header(studio *s, int area);
void studio_outliner_body(studio *s, int area);
int *studio_outliner_rows(studio *s);
void studio_properties_header(studio *s, int area);
void studio_properties_body(studio *s, int area);
void studio_behavior_header(studio *s, int area);
void studio_behavior_body(studio *s, int area);
/* After the Studio document is emitted: drive and render the graph canvas. */
void studio_graph_frame(studio *s, double delta_seconds);
void studio_graph_free(studio *s);
bool studio_graph_wants_keyboard(const studio *s);
bool studio_graph_accepts_text(const studio *s);
const char *studio_graph_cursor(const studio *s);
/* The add-node palette: items for the command surface while it is open for the graph. */
bool studio_graph_palette(studio *s);
bool studio_graph_can_replay(studio *s, bool redo);
void studio_graph_history(studio *s, bool redo);
bool studio_graph_active(const studio *s);
/* The graph node Properties shows, or 0; and its panels. */
uint64_t studio_graph_node(const studio *s);
/* The comment frame selected on the canvas while no node is, or 0. */
uint64_t studio_graph_comment(const studio *s);
void studio_node_properties(studio *s);
void studio_rewire_toast(studio *s);
/* Members: the open behavior's graphs, variables and named values. */
void studio_members_header(studio *s, int area);
void studio_members_body(studio *s, int area);
/* Keys over the Members area; true when one was taken. */
bool studio_members_key(studio *s, SDL_Keycode key, SDL_Keymod mod);
/* A member dropped on the graph canvas: a variable's Get, or Set with Alt, or
   a call. Call after the canvas surface. */
void studio_members_drop(studio *s);
/* Runs the Undo a member delete's toast offers; call every frame. */
void studio_members_toast(studio *s);
/* The selected member as a key that changes with the selection, or 0. */
uint64_t studio_member_shown(studio *s);
void studio_member_properties(studio *s);
/* The type search: items for the command surface while it picks a type. */
bool studio_member_palette(studio *s);
void studio_assets_header(studio *s, int area);
void studio_assets_body(studio *s, int area);
const char *studio_asset_kind_name(int kind);
uint32_t studio_asset_kind_icon(int kind);
const char *studio_asset_kind_label(int kind); /* plural: "Behaviors" */
void studio_performance_sample(studio *s, double delta_seconds);
void studio_performance_header(studio *s, int area);
void studio_performance_body(studio *s, int area);
void studio_output_header(studio *s, int area);
void studio_output_body(studio *s, int area);
/* Shows Output's Find view, opening the drawer when no area shows Output. */
void studio_output_find(studio *s);

/* Find References (studio_find.c): lists what references an asset, or a
   function, variable, event or prototype, in Output's Find view; rows open
   their asset and reveal the node. Functions and variables belong to the open
   graph document. */
void studio_find_document_references(studio *s);
void studio_find_asset_references(studio *s, int asset);
void studio_find_graph_references(studio *s, const graph *gr);
void studio_find_variable_references(studio *s, uint64_t variable_id);
/* Calls, variable nodes, events and spawns reference something. */
bool studio_node_referable(const node *n);
void studio_find_node_references(studio *s, const node *n);
void studio_find_view(studio *s, float row);
void studio_find_clear(studio *s);
/* Opens a graph of the open document and frames one of its nodes. */
void studio_reveal_node(studio *s, uint64_t graph_id, uint64_t node_id);
void studio_record(studio *s, kit_toast_type kind, const char *text);

/* Debugging (studio_debug.c). */
typedef enum
{
    STUDIO_RESUME,
    STUDIO_STEP_OVER,
    STUDIO_STEP_INTO,
    STUDIO_STEP_OUT,
} studio_debug_command;
/* Before the frame is emitted: runtime errors, problems and the paused run. */
void studio_debug_frame(studio *s);
/* Opens the node's behavior and graph in a Behavior area, then selects and
   frames the node. */
void studio_reveal(studio *s, const studio_node_ref *at);
/* "Graph › Node" for a node of the open behavior; the graph alone without one. */
void studio_node_place(const studio *s, const studio_node_ref *at, char *out, size_t size);
/* Where the open behavior's compile problem was authored: in a library it
   calls, or in the behavior itself. */
studio_node_ref studio_problem_at(const studio *s, const compile_diagnostic *d);
/* Continues the paused run; false while none is paused. */
bool studio_debug(studio *s, studio_debug_command command);
/* F5, F10, F11 and Shift F11 act while a run is paused, even over a focused
   game, and repeat to walk a run. True when the key continued the run. */
bool studio_debug_key(studio *s, SDL_Keycode key, bool shift);
/* Step Over, Into and Out in the transport while a run is paused. */
void studio_debug_steps(studio *s);
/* Resume and the steps as menu items, enabled while a run is paused. */
void studio_debug_menu(studio *s);
/* The paused run's call stack over the Behavior area. */
void studio_debug_call_stack(studio *s);

/* Commands shared by menus, shortcuts and the command palette. */
void studio_toggle_drawer(studio *s, studio_editor editor);
void studio_frame_selection(studio *s);
void studio_duplicate(studio *s);
void studio_delete(studio *s);
void studio_add_object(studio *s, const char *prototype);
uint32_t studio_prototype_icon(const studio *s, const char *prototype);

#endif
