#ifndef CHIBI_EDITOR_STUDIO_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_INTERNAL_H

#include "assets/new_asset.h"
#include "assets/project.h"
#include "assets/ui_source.h"
#include "editor/documents/asset_code.h"
#include "editor/documents/component_presentation.h"
#include "editor/documents/ui_code.h"
#include "editor/studio/studio.h"
#include "editor/ui/kit.h"
#include "editor/ui/kit_icons.h"
#include "editor/workspace/asset_index.h"
#include "editor/workspace/scenario_playback.h"
#include "editor/workspace/scene_internal.h"
#include "graph/authoring/arrange.h"
#include "render/ui.h"
#include "runtime/save.h"
#include "support/file_watch.h"
#include "support/resources.h"
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
    STUDIO_EDITOR_CODE, /* Blender's Text Editor: the active asset's source */
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
    STUDIO_THEME_DRACULA,
    STUDIO_THEME_SOLARIZED_LIGHT,
    STUDIO_THEME_SOLARIZED_DARK,
    STUDIO_THEME_TOKYO_NIGHT,
    STUDIO_THEME_TOKYO_STORM,
    STUDIO_THEME_TOKYO_DAY,
    STUDIO_THEME_CATPPUCCIN_LATTE,
    STUDIO_THEME_CATPPUCCIN_FRAPPE,
    STUDIO_THEME_CATPPUCCIN_MACCHIATO,
    STUDIO_THEME_CATPPUCCIN_MOCHA,
    STUDIO_THEME_ROSE_PINE,
    STUDIO_THEME_ROSE_PINE_MOON,
    STUDIO_THEME_ROSE_PINE_DAWN,
    STUDIO_THEME_GRUVBOX_LIGHT,
    STUDIO_THEME_GRUVBOX_DARK,
    STUDIO_THEME_COUNT,
} studio_theme;

typedef enum
{
    STUDIO_COMMAND_SEARCH,
    STUDIO_COMMAND_SETTINGS,
    STUDIO_COMMAND_SAVE,
    STUDIO_COMMAND_RELOAD,
    STUDIO_COMMAND_UNDO,
    STUDIO_COMMAND_REDO,
    STUDIO_COMMAND_DUPLICATE,
    STUDIO_COMMAND_DELETE,
    STUDIO_COMMAND_OPEN_PROTOTYPE,
    STUDIO_COMMAND_FRAME,
    STUDIO_COMMAND_RESET_CAMERA,
    STUDIO_COMMAND_TOGGLE_ASSETS,
    STUDIO_COMMAND_TOGGLE_OUTPUT,
    STUDIO_COMMAND_TOGGLE_PERFORMANCE,
    STUDIO_COMMAND_MAXIMIZE,
    STUDIO_COMMAND_RESET_LAYOUT,
    STUDIO_COMMAND_ZOOM_IN,
    STUDIO_COMMAND_ZOOM_OUT,
    STUDIO_COMMAND_ZOOM_RESET,
    STUDIO_COMMAND_PLAY,
    STUDIO_COMMAND_STOP,
    STUDIO_COMMAND_STEP,
    STUDIO_COMMAND_RECORD_GAMEPLAY,
    STUDIO_COMMAND_DENSITY,
    STUDIO_COMMAND_MOTION,
    STUDIO_COMMAND_THEME,
    STUDIO_COMMAND_INSPECT, /* UI Inspector: pick an element */
    STUDIO_COMMAND_PREVIEW_UI,
    /* Authored top bar and area headers (workstream T) */
    STUDIO_COMMAND_SELECT_TAB,
    STUDIO_COMMAND_CLOSE_TAB,
    STUDIO_COMMAND_CLOSE_DRAWER,
    STUDIO_COMMAND_RESUME,
    STUDIO_COMMAND_STEP_OVER,
    STUDIO_COMMAND_STEP_INTO,
    STUDIO_COMMAND_STEP_OUT,
    STUDIO_COMMAND_SELECT_PREVIOUS,
    STUDIO_COMMAND_SELECT_NEXT,
    /* Added after the persisted command ids above to keep shortcut prefs stable. */
    STUDIO_COMMAND_NEW_SCENE,
    STUDIO_COMMAND_NEW_PROTOTYPE,
    STUDIO_COMMAND_NEW_BEHAVIOR,
    STUDIO_COMMAND_NEW_EVENT,
    STUDIO_COMMAND_NEW_SYSTEM,
    STUDIO_COMMAND_NEW_LIBRARY,
    STUDIO_COMMAND_NEW_MATERIAL,
    STUDIO_COMMAND_NEW_SHAPE,
    STUDIO_COMMAND_NEW_FIELD,
    STUDIO_COMMAND_NEW_SHADER_LIBRARY,
    STUDIO_COMMAND_NEW_UI_WIDGET,
    STUDIO_COMMAND_NEW_UI_DOCUMENT,
    STUDIO_COMMAND_NEW_STYLESHEET,
    STUDIO_COMMAND_RENAME,
    STUDIO_COMMAND_ADD_COMPONENT,
    STUDIO_COMMAND_KEEP_CHANGES,
    STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT,
    STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT,
    STUDIO_COMMAND_TOGGLE_SIDE_PANE,
    /* Appended so existing shortcut preference ids stay stable. */
    STUDIO_COMMAND_COLLABORATION,
    STUDIO_COMMAND_COLLABORATION_HOST,
    STUDIO_COMMAND_COLLABORATION_JOIN,
    STUDIO_COMMAND_COLLABORATION_LEAVE,
    STUDIO_COMMAND_COLLABORATION_INVITE_VIEWER,
    STUDIO_COMMAND_COLLABORATION_INVITE_EDITOR,
    STUDIO_COMMAND_COLLABORATION_TOKEN,
    STUDIO_COMMAND_COLLABORATION_FOLLOW,
    STUDIO_COMMAND_COLLABORATION_UNFOLLOW,
    /* The Scene's tools (studio_scene.c), in scene_tool order. */
    STUDIO_COMMAND_SELECT_TOOL,
    STUDIO_COMMAND_MOVE_TOOL,
    STUDIO_COMMAND_ROTATE_TOOL,
    STUDIO_COMMAND_SCALE_TOOL,
    STUDIO_COMMAND_GRAB,
    STUDIO_COMMAND_TOGGLE_SNAP,
    STUDIO_COMMAND_TOGGLE_GRID,
    STUDIO_COMMAND_FINER_GRID,
    STUDIO_COMMAND_COARSER_GRID,
    /* The Scene view's Show toggles and its camera view. */
    STUDIO_COMMAND_SHOW_WORLD,
    STUDIO_COMMAND_SHOW_LIGHTING,
    STUDIO_COMMAND_SHOW_FOG,
    STUDIO_COMMAND_SHOW_CAMERAS,
    STUDIO_COMMAND_VIEW_THROUGH_CAMERA,
    /* Hide in Editor (studio_scene.c) */
    STUDIO_COMMAND_HIDE_SELECTED,
    STUDIO_COMMAND_HIDE_UNSELECTED,
    STUDIO_COMMAND_SHOW_ALL,
    STUDIO_COMMAND_SELECT_ALL,
    STUDIO_COMMAND_DESELECT,
    STUDIO_COMMAND_FRAME_ALL,
    STUDIO_COMMAND_FIND,
    STUDIO_COMMAND_KEYBOARD_SHORTCUTS,
    /* The object clipboard and extending the Outliner's selection */
    STUDIO_COMMAND_COPY,
    STUDIO_COMMAND_CUT,
    STUDIO_COMMAND_PASTE,
    STUDIO_COMMAND_EXTEND_PREVIOUS,
    STUDIO_COMMAND_EXTEND_NEXT,
    STUDIO_COMMAND_SHOW_LIVE_VALUES,
    STUDIO_COMMAND_COUNT,
} studio_command;

#define STUDIO_CHORD_SIZE 64

/* Workspace presets (Blender's workspaces): each arranges the areas for a
 * task, and switching one never touches documents, selection or playback. */
typedef enum
{
    STUDIO_WORKSPACE_VERTICAL_SPLIT,   /* Scene over Behavior */
    STUDIO_WORKSPACE_HORIZONTAL_SPLIT, /* Canvas beside the Text Editor */
    STUDIO_WORKSPACE_COUNT,
} studio_workspace;

/* A workspace's arrangement, as the user last left it. */
typedef struct
{
    bool side_open; /* the right column shows */
    float side_width;
    float main_split;
    float side_split;
    bool drawer_open;
    float drawer_height;
    bool canvas_actions_open;
    studio_editor editors[STUDIO_AREA_COUNT];
    char panels[STUDIO_AREA_COUNT][64]; /* the panel each STUDIO_EDITOR_PANEL area shows */
} studio_layout;

#define STUDIO_RECENT_COUNT 32

/* An item chosen in the palette: its key ("command:save") and how often. */
typedef struct
{
    char key[96];
    int uses;
} studio_recent;

/* Editor preferences (studio_prefs.c). The one store for view settings and the
 * area layout; everything else is authoring state owned by the C model. */
typedef struct
{
    char shortcuts[STUDIO_COMMAND_COUNT][STUDIO_CHORD_SIZE]; /* empty uses the default */
    studio_recent recent[STUDIO_RECENT_COUNT]; /* most recent first; an empty key ends them */
    studio_theme theme;
    bool comfortable;
    bool reduced_motion;
    float ui_scale; /* the editor's size, as Blender's Resolution Scale; games' UI keeps its own */
    studio_workspace workspace;
    studio_layout layouts[STUDIO_WORKSPACE_COUNT];
    kit_wire_style wires; /* graph wire routing */
    bool short_bindings; /* graph chips name "Aim › Manual", not "Self.GameAim.manual" */
    bool title_flow;    /* unlabeled execution pins sit in node titles */
    bool compact_nodes; /* operators as symbols, one-output reads as pills */
    bool live_values;   /* data outputs show the debug target's values */
    int standard_font_family; /* registered RmlUi family used by editor UI text */
    float standard_font_size; /* base UI text size, in dp */
    int code_font_family; /* registered RmlUi family used by code and monospace text */
    bool code_vim;        /* the Text Editor edits with Vim's keys */
    float code_font_size; /* the Text Editor's font size, in dp */
    bool code_apply_live; /* UI edits apply as typed, not when saved */
    bool code_wrap; /* Workstream I: long lines wrap; off, they scroll sideways */
    /* The Scene's tools: the active one, kept by hand (studio_prefs.c), and
       the grid and snapping, which are settings. */
    scene_tool tool;
    bool scene_grid;
    bool scene_snap;
    float scene_increment; /* the grid's spacing, in world units */
    /* What the Scene view shows while editing (app_scene_view_options). */
    bool scene_world;
    bool scene_lighting;
    bool scene_fog;
    bool scene_cameras;
    int assets_tile; /* Assets' thumbnail size in dp; STUDIO_ASSETS_LIST lists them */
} studio_prefs;

/* Assets' thumbnail slider: its smallest step shows the list. */
#define STUDIO_ASSETS_LIST 40
#define STUDIO_ASSETS_TILE 96
#define STUDIO_ASSETS_TILE_MAX 160

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
    float scene_gpu_ms; /* the scene's GPU time, or negative when not measured */
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

/* An error or warning in one of the project's graph assets (studio_output.c). */
typedef struct
{
    studio_node_ref at;
    char place[256]; /* "Asset › Graph › Node" */
    bool error;
    char message[CMPL_MESSAGE_MAX];
} studio_problem;

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
    int kind; /* reference_asset_kind of the referencing asset */
    char asset[128];
    uint64_t graph_id, node_id; /* 0 for an asset-level reference */
    char place[160];            /* graph or object */
    bool object;                /* place names a scene object */
    const char *use;            /* Call, Read, Handles... */
    /* Workstream I: a place in a UI document the Text Editor opens; its
       path is empty for other references. */
    ui_code_place ui;
} studio_reference;

/* What Output's Calls view traces (studio_calls.c): a function by its asset
   and graph, or an event by name. */
typedef struct
{
    char title[160]; /* the function's or event's name; empty for none */
    int kind;        /* REFERENCE_SYMBOL_GRAPH or REFERENCE_SYMBOL_EVENT */
    int asset_kind;  /* a function's asset */
    char asset[128];
    uint64_t graph_id;
} studio_call_root;

/* A row of the Calls view, copied from the call hierarchy so it outlives the
   index it came from. */
typedef struct
{
    char label[128];
    char meta[128]; /* the asset holding it */
    uint32_t icon;
    studio_reference at; /* the call, send or Event node joining it to its parent */
    studio_call_root from; /* what a trace from it roots at, when it has more */
    int depth;
    bool open;   /* its children show */
    bool parent; /* has children */
    bool cycle;  /* repeats an ancestor */
    bool more;   /* has children past the trace's depth or size */
} studio_call;

enum
{
    STUDIO_VALUE_NAMES = 8, /* outputs per node the Outputs panel can name */
};

/* The behavior graph canvas (studio_graph*.c): its own RmlUi context whose dp
 * ratio includes graph zoom and standard-font scale, so text stays crisp and
 * clipping works at every scale. The canvas logical space is graph units;
 * `x, y` is the graph point at the canvas top-left. */
typedef struct
{
    rml_backend *backend;
    kit *kit;
    studio_theme theme;
    bool theme_light, theme_initialized;
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
    uint64_t input_edit_pin; /* the input whose folded expression is being edited */
    uint64_t input_edit_node; /* its inline Behavior Expression node */
    kit *input_edit_kit; /* the canvas or Properties kit that opened the editor */
    bool input_edit_focus;
    char input_edit[512];
    char input_edit_source[512];
    uint64_t inline_edit_pin; /* consumer input whose folded source is being edited */
    uint64_t inline_edit_node;
    kit *inline_edit_kit;
    bool inline_popup_pending;
    kit_rect inline_anchor;
    uint64_t inline_anchor_node, inline_anchor_pin;
    kit *inline_anchor_kit;
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
    uint64_t link_pickup_node, link_pickup_pin; /* original input until drop commits */
    char link_error[160];
    uint64_t hover_node;
    int hover_kind, hover_pin; /* -1 without a socket */
    int hover_edge;            /* edge index under the pointer, or -1 */
    bool space;
    double value_edit_frame; /* a value gesture has an open undo snapshot */
    bool value_editing;
    uint64_t value_edit_node_id;
    uint64_t value_edit_pin_id;
    int value_edit_output;
    kit *value_edit_kit;
    uint64_t measured_key;
    arrange_routes routes;
    int *edge_route; /* per edge: its link in routes, or -1 */
    int *wire_edges; /* per drawn wire: its edge index */
    struct studio_node_layout *layouts; /* intrinsic node measurements */
    struct studio_graph_calculations *calculations; /* Persistent world measurement, routing and reachability cache. */
    int reroute_edge, reroute_point; /* the reroute being dragged */
    float reroute_x, reroute_y;      /* where it was before the drag */
    bool reroute_added;              /* the drag began by adding it */
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
    /* Workstream Q: the graph Back and Forward last saw shown (an array of
       one, or none), so a Text Editor place in the history leaves the
       graph shown alone. */
    studio_visit *shown;
    uint64_t menu_pin;  /* the menu's pin on menu_node, or 0 */
    bool box_subtract;  /* a Ctrl marquee removes what it touches */
    bool frame_pending; /* F frames on release unless it placed a node */
    struct
    {
        struct studio_live_value *values; /* shown pins' live values as read */
        double time;                      /* when they were read */
    } live;
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
        int toast; /* offers Undo while the delete is the latest edit */
        const behavior_asset *step; /* the delete's undo step in the graph editor */
    } undo;
    struct
    {
        bool open;       /* the type search is choosing for this target */
        uint64_t id;     /* the variable, or the function whose pin it is */
        uint64_t pin_id; /* the Entry or Return pin, or 0 for the variable */
    } pick;
    bool reveal; /* scroll the selected member into view */
    struct
    {
        bool counted;
        unsigned generation; /* the asset index build counted */
        int asset;           /* the document counted */
        struct studio_member_uses *items;
    } uses; /* each member's uses across the project */
} studio_members;

typedef enum
{
    STUDIO_REFACTOR_CLOSED,
    STUDIO_REFACTOR_RENAME,
    STUDIO_REFACTOR_DELETE,
} studio_refactor_mode;

typedef struct
{
    char text[192]; /* "Asset > Place" */
} studio_refactor_use;

/* The rename or safe-delete popover (studio_refactor.c). The symbol's asset
   is found again by kind and name, since the index may rebuild meanwhile. */
typedef struct
{
    studio_refactor_mode mode;
    bool opening; /* open the popover this frame */
    bool focus;   /* focus the name field on its first frame */
    bool shown;   /* the popover showed last frame */
    reference_symbol symbol;
    int asset_kind;
    char asset[128]; /* the asset holding the symbol, or the asset itself */
    char event[128]; /* an event's name, which symbol.name borrows */
    char title[128]; /* the symbol's name */
    char name[128];  /* the new name, as typed */
    char planned[128];
    struct app_refactor *plan; /* renames to `planned` */
    char error[192];
    studio_refactor_use *uses; /* what a delete leaves dangling */
    studio_member_kind member; /* a used member whose delete is confirmed */
    uint64_t member_id;
    struct app_refactor *undo; /* puts back the last applied change's files */
    int undo_toast;
} studio_refactor;

/* The open project's editor/Components.kdl, re-read when it changes. */
typedef struct
{
    component_presentation hints; /* the last valid file; empty while it is missing */
    file_watch watch;
} studio_presentation;

/* The editor's own UI project: its panels, widgets and theme. */
#define STUDIO_PROJECT resource_studio()

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

typedef struct
{
    bool open, focus, waiting, confirm_discard;
    char name[128];
    char error[192];
    char created_root[1024];
} studio_new_project;

/* Workstream I: the code editor of one open document part, which keeps its
 * cursor, scroll and Vim state while another part shows. */
typedef struct
{
    char path[1024];
    ui_part part;
    kit_code_editor *editor;
    /* Workstream Q: the text the editor and its part last shared, owned;
       typing records from it (ui_code_record). */
    char *synced;
} studio_code_editor;

/* UI documents (workstream D). The Text Editor (studio_code.c) shows the
 * active UI document and keeps the last one while a document of another kind
 * is active. */
typedef struct
{
    char path[1024];      /* the UI document shown, or empty */
    int reveal;           /* a 1-based line of the shown part to select, or 0 */
    int reveal_wait;      /* frames until that line is laid out */
    int overwrite_toast;  /* offers to save over a file changed on disk */
    char overwrite[1024]; /* that file */
    int close_toast;      /* confirms closing a document with unsaved edits */
    char close[1024];     /* that document's tab key */
    /* Workstream I: a code editor (kit_code) per open document part, and
       the one shown with the document and part it holds (the designer
       follows its cursor). */
    studio_code_editor *editors; /* array */
    asset_code **assets;         /* KDL source sessions, owned */
    kit_code_editor *editor;
    char editor_path[1024];
    ui_part editor_part;
    int reveal_offset; /* a byte of the part to reveal instead, or -1 */
    /* The shown part's text parsed (assets/ui_source.h), the spans that
       highlight it, and each part's problems as markers (ui_code_markers). */
    ui_source *source;
    kit_code_span *spans;                    /* array */
    kit_code_marker *markers[UI_PART_COUNT]; /* arrays */
    /* RmlUi's check of the shown document (ui_code_check) in a scratch
       context kept for it, made once its text has rested: the revision
       checked, and when the next is due. */
    rml_backend *checker;
    ui_check_message *checked; /* array */
    char check_failure[256];
    char checked_path[1024];
    unsigned checked_revision, check_seen;
    double check_due;
    bool markers_stale;
    /* The project around the shown document, for completion and navigation
       (ui_code_scan), and when it was scanned. */
    ui_source_project project;
    char project_root[1024];
    double project_time;
    /* Completion while it runs, at the cursor. */
    bool completing;
    ui_code_completions completions;
    /* The description of the byte the pointer rests on. */
    char *hover;
    int hover_at;
    const ui_source *hover_source;
    bool replayed; /* Undo or Redo from the editor changed the text */
    /* Find (Mod+F): the query, where its shown match starts and how many
       the part holds. */
    struct
    {
        bool open, focus, typing, refocus;
        char query[128], seen[128];
        int at, count, index;
    } find;
    /* Workstream Q: the shown document's problems for Output
       (studio_code_problems), and the revisions of the text and the check
       they describe. */
    struct
    {
        struct studio_ui_problem *rows; /* array */
        char path[1024];
        unsigned revision, checked;
    } problems;
} studio_code;

typedef struct
{
    char *client, *name, *presentation;
    uint32_t color;
    int pointer_area;
    float pointer_x, pointer_y;
    bool pointer_valid;
} studio_collab_peer;

typedef struct
{
    studio_collab_peer *peers;
    char follow_client[64];
    studio_prefs local_prefs;
    bool following, saved_prefs, restoring_prefs, admin_token_expanded, busy;
    double presence_sent;
} studio_collaboration;

/* Assets ▸ New (studio_assets.c): the kinds of asset Studio makes, each
   new_asset_kind and then each ui_new_kind, and the name of the new one. */
#define STUDIO_NEW_KIND_COUNT (NEW_ASSET_KIND_COUNT + UI_NEW_KIND_COUNT)
typedef struct
{
    int kind;     /* below STUDIO_NEW_KIND_COUNT */
    bool opening; /* open the name popover this frame */
    bool focus;   /* focus the name field on its first frame */
    bool shown;   /* the popover showed last frame, so an Enter is for it */
    char name[64];
    char error[160];
} studio_new_asset;

struct studio
{
    app_scene_authoring *scene;
    engine_viewport *viewport;
    const char *project_root; /* the open project, owned by the application */
    /* Its manifest as opened, with the name and start scene it defaults to;
       Settings ▸ Project shows and rewrites it. Empty when it would not open. */
    chibi_project project;
    bool own_project; /* the open project is the Studio project, whose settings are these preferences */
    uint64_t scene_prefs_seen, preview_prefs_seen;
    studio_project *projects;
    studio_new_project new_project;
    char *open_project; /* root chosen to open, until the application takes it */
    studio_disk_toast *disk_toasts; /* files that changed on disk under unsaved edits */
    int discard_toast;   /* toast confirming unsaved changes are discarded */
    int discard_project; /* the project that toast opens */
    rml_backend *backend;
    kit *kit;
    studio_ui *ui; /* NULL when the Studio project could not load */
    ui_page page; /* the Studio scene's root document, whose RCSS styles every Studio document */
    kit_frame_input in;
    struct
    {
        double next;
        uint64_t frames; /* host model/emission passes, independent of simulation frames */
        uint64_t state;
        float width, height, ratio;
        bool wake;
    } ui_schedule;
    studio_area areas[STUDIO_AREA_COUNT];
    int hover_area;
    int focus_area;
    int key_area; /* the area under the pointer while a key runs a command, else -1 */
    int find_request; /* editor + 1 whose search field Find focuses next, else 0 */
    int maximized; /* the area filling the workspace (Blender's Ctrl Space), or -1 */
    studio_prefs prefs;
    studio_collaboration collaboration;
    int shortcut_recording; /* command + 1 while Settings captures a chord */
    studio_prefs applied; /* last applied copy; changes apply and save by comparison */
    double prefs_changed_at;
    uint64_t scene_view_saved; /* the scene view store's revision last committed */
    bool system_light;
    bool mac; /* macOS conventions: Command shortcuts and ⌘ notation */
    bool light;
    int theme_switching; /* frames left with transitions suspended */
    bool drawer_dragging;
    bool side_dragging;
    SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];
    int cursor_shown; /* SDL_SystemCursor applied, -1 before the first frame */
    kit_rect main_rect;
    kit_rect side_rect;
    studio_tab tab;
    uint64_t shown_node; /* graph node the Properties tabs last followed */
    int shown_actor;     /* scene selection the Properties tabs last followed */
    studio_record_entry *records;
    studio_runtime_error *runtime_errors;
    studio_problem *project_problems; /* the last check of every graph asset */
    bool problems_checked;            /* no save or reload since that check */
    int problems_scope;               /* the open asset, or the project */
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
    studio_perf_sample *perf_published;
    double perf_publish_next;
    uint64_t perf_publications;
    bool perf_publish_playing;
    struct
    {
        bool available;
        float budget_ms;
        char text[128];
    } perf_lighting;
    double perf_readout_time;
    char perf_readout[96];
    struct studio_assets_cache *assets_cache;
    int assets_kind;           /* reference_asset_kind, or -1 for all */
    unsigned assets_collapsed; /* bit per reference_asset_kind */
    char assets_filter[128];
    int assets_menu;           /* the asset under the row menu */
    int assets_selected_kind;  /* the selected asset's reference_asset_kind */
    char assets_selected[128]; /* the selected asset's name, or "" */
    bool assets_reveal; /* scroll the selection into view on the next emission */
    int assets_rest;    /* the tile the pointer rests on, or -1 */
    double assets_rest_since;
    struct studio_thumbs *thumbs; /* asset thumbnails, NULL until asked for */
    char dropped_prototype[256];  /* placed by the viewport this frame */
    int output_view;
    char output_filter[128];
    char references_title[160]; /* what Output's Find view lists references to */
    studio_reference *references;
    struct
    {
        studio_call_root root;
        int callees; /* 0 lists callers, 1 callees */
        studio_call *rows;
    } calls; /* Output's Calls view */
    struct
    {
        bool request; /* open at x, y on the next frame */
        float x, y;
        char component[128];
    } peek; /* a component node's Go to Definition: its fields */
    char outliner_filter[128];
    bool outliner_collapsed;
    bool outliner_reveal; /* scroll the selection into view on the next frame */
    struct
    {
        uint64_t anchor; /* the object a Shift range extends from, by identity */
        uint64_t active; /* the active object the last range left, by identity */
    } range;
    char property_search[128];
    bool text_input;
    kit_surface_input surface;
    struct
    {
        float zoom; /* magnification of the Scene's frame, game UI included; 1 fits */
        float x, y; /* the center shown, as fractions of the frame */
        float u, v; /* the pointer last frame, as fractions of the Scene area */
        bool panning;
    } view;
    studio_graph graph;
    studio_members members;
    studio_refactor refactor;
    int assets_hover;      /* the asset row under the pointer, or -1 */
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
    float zoom_wheel; /* the wheel turned with Mod or Ctrl held this frame */
    float pinch;      /* the log of this frame's trackpad pinch scale, or 0 */
    int undo_toast;   /* toast offering Undo for undo_toast_group */
    uint64_t undo_toast_group;
    char status[256];
    char seen_message[256]; /* last authoring message reported */
    double status_time;
    double time;
    double active_until; /* frames run at full rate until then, after input */
    /* UI documents (workstream D). */
    studio_code code;
    studio_new_asset new_asset;
    struct studio_inspect *inspect; /* the UI Inspector, NULL until used */
    /* UI wiring (workstream K): the asset picker, the shown game UI's error
       and the Canvas preview. */
    struct
    {
        bool open;   /* the command surface is choosing an asset for `key` */
        bool chosen; /* `value` waits for the field keyed `key` */
        bool text;   /* a name no asset has may be chosen */
        int kind;    /* reference_asset_kind */
        char key[192];
        char none[128]; /* the label of choosing none; "" when a name is required */
        char current[128];
        char value[128];
    } pick;
    char ui_error[256]; /* the shown game UI's error, as last recorded */
    struct studio_canvas *canvas; /* NULL until a preview opens */
    char followed[128]; /* the ui asset previewed because its document is active */
    uint64_t revealed_document; /* tab identity last considered for a maximized area */
    struct
    {
        bool read; /* events holds them while the Add menu is open */
        struct ui_event *events; /* studio_behavior_ui_events */
    } add_menu;
    /* What the ui documents showing the open behavior's entities bind under
       self, for Set UI Data (studio_graph_ui_data). */
    struct
    {
        int behavior; /* the asset index entry they are for; -2 before any */
        struct ui_data_binding *bindings;
    } bound;
    /* Visual designer (workstream H): NULL until a UI document is active. */
    struct studio_designer *designer;
    /* Workstream Q: Problems ▸ Project's ui problems as of its last check
       (studio_code_project_problems). */
    struct studio_ui_problem *ui_project_problems; /* array */
    /* ---------- arena2d end to end (wave 3, workstream X) ---------- */
    rml_input *game_input; /* keys pressed and text typed this frame, for the Scene's game UI */
    /* ---------- Studio in Studio (studio_host.c) ---------- */
    struct studio *guest; /* the Studio Play runs in the Scene area, or NULL */
    int guest_request; /* 1 starts the guest, -1 stops it, at the next frame */
    bool hosted;      /* a guest: its host feeds it input and shows its frame */
    bool guest_hover; /* the pointer is over the guest */
    bool guest_keys;  /* a press in the guest gave it the keyboard */
    bool guest_capture; /* a button pressed in the guest is held */
    bool typing;        /* a field or the graph had the keyboard last frame */
    bool scene_status;  /* the status bar shows a Scene gesture's change */
    /* ---------- Rename objects and prototype components ---------- */
    struct
    {
        uint64_t entity; /* the object renamed in place, or 0 */
        bool outliner; /* in its Outliner row, else in Properties' name field */
        bool focus;    /* focus the field on its next frame */
        char name[256];
    } rename;
    struct
    {
        bool open; /* the command surface adds a component to `prototype` */
        char prototype[128];
        char added[128];      /* the component added last, whose panel opens */
        char menu[128];       /* the component whose panel menu is open */
        int toast;            /* offers Undo after Remove Component */
        const av_value *step; /* the removal's undo snapshot; never dereferenced */
    } components;
    struct
    {
        int toast; /* offers Undo after Keep Changes */
        int depth; /* the scene's Keep Changes records after it */
    } keep;
};

/* Whether frames run at full rate for input or a playing scene, rather than
 * only while the screen changes. */
bool studio_active(const studio *s);
/* Physical pixels per editor dp: the display's scale times UI Scale. */
float studio_pixel_ratio(const studio *s);

/* Whether `key` was pressed this frame. */
bool studio_key_pressed(const studio *s, SDL_Keycode key);
/* The Scene's tools for this frame (studio_scene.c): the active tool,
 * snapping and the grid from the preferences, the held modifiers and a
 * gesture's keys, and the gesture's change in the status bar. */
void studio_scene_tools(studio *s);
/* The grid increment preset `step` places from the current one, which it
 * keeps at either end. */
float studio_grid_increment(const studio *s, int step);
/* Platform shortcuts (studio_keys.c). */
bool studio_detect_mac();
bool studio_primary(const studio *s, SDL_Keymod mod);
const char *studio_chord(const studio *s, const char *chord);

/* Preferences. */
bool studio_theme_is_light(studio_theme theme, bool system_light);
/* Applies one palette to either the editor or its graph context. */
void studio_theme_apply(kit *k, studio_theme theme, bool system_light);
studio_prefs studio_prefs_default();
studio_layout studio_layout_default(studio_workspace workspace);
/* The arrangement the areas show: the active workspace's. */
studio_layout *studio_active_layout(studio *s);
studio_prefs studio_prefs_load();
bool studio_prefs_save(const studio_prefs *prefs);
/* Binds the scenes' editor view state to its user file, beside the
 * preferences' (localStorage on the web). */
bool studio_prefs_open_scene_view(game_save *store);
bool studio_prefs_equal(const studio_prefs *a, const studio_prefs *b);
/* Shares the preferences with authored panels as saved keys: a change a panel
 * made since `seen` applies first, then the preferences are written back. */
void studio_prefs_share(studio_prefs *prefs, game_save *save, uint64_t *seen);

/* A theme custom property a Theme panel row shows (studio_sources.c). */
typedef struct studio_token
{
    int file;       /* index in the page's imports */
    int start, end; /* the value's bytes in the file when read */
    char name[64];
    char *value; /* as read */
    bool color;
} studio_token;

/* Shortcut presentation belongs to one Settings model. Command defaults,
 * aliases and contexts are fixed by the command table; overrides and the
 * platform determine its keys and conflicts. */
typedef struct studio_settings_cache
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *rows;
    char shortcuts[STUDIO_COMMAND_COUNT][STUDIO_CHORD_SIZE];
    int commands[STUDIO_COMMAND_COUNT];
    int conflicts[STUDIO_COMMAND_COUNT];
    int command_count;
    bool valid, mac;
} studio_settings_cache;

/* What Keyboard Shortcuts last showed: its query and the bindings. */
typedef struct studio_keymap_cache
{
    yyjson_mut_doc *doc;
    char query[256];
    char shortcuts[STUDIO_COMMAND_COUNT][STUDIO_CHORD_SIZE];
    bool valid, mac;
} studio_keymap_cache;

/* Keyboard Shortcuts (studio_keymap.c): the `shortcuts` variable, with the
 * sheet's `query`, its `matches` and `groups`, each a `label` and `rows`
 * of a `label` and its `keys`. */
void studio_keymap_add(yyjson_mut_doc *doc);
/* Lists every binding `prefs` gives, and the keys editors take
 * themselves, that matches the query; true when the sheet changed. */
bool studio_keymap_show(yyjson_mut_doc *doc, const studio_prefs *prefs, bool mac, studio_keymap_cache *cache);
/* Whether the sheet lists the area. */
bool studio_keymap_area(const char *area);

/* What authored panels bind beyond their world's `save` (studio_sources.c):
 * `world`, the Scene world's saved keys, and `theme`, the custom properties
 * of the stylesheets the open scene's root document imports. */
typedef struct studio_sources
{
    game_save *save; /* the one `world` shows */
    uint64_t save_seen;
    char content_root[1024]; /* whose page `theme` shows */
    ui_page page;            /* the open scene's root document */
    studio_token *tokens;    /* in the order `theme` lists its rows */
    uint64_t writes_seen;
    int inspected; /* the Inspector model's revision `inspector` shows */
    studio_settings_cache settings_cache;
    studio_keymap_cache keymap_cache;
} studio_sources;

/* A data model's JSON with the variables authored panels bind. */
yyjson_mut_doc *studio_sources_new();
/* Applies what bindings changed, then shows the current values. */
void studio_sources_sync(studio *s, studio_sources *sources, ui_data *data);
/* Shows the current values in a preview's model, applying nothing its
 * bindings write (studio_canvas.c). */
void studio_sources_show(studio *s, studio_sources *sources, ui_data *data);
/* Sets the Settings page to search the Editor tab for `query`. */
void studio_sources_search_settings(ui_data *data, const char *query);
void studio_sources_free(studio_sources *sources);

/* Collaboration presentation, peers and status controls. */
void studio_collab_free(studio *s);
void studio_collab_emit(studio *s);
void studio_collab_after_frame(studio *s);
bool studio_collab_available(void);
const char *studio_collab_display_status(void);
bool studio_collab_status_error(void);
void studio_collab_web_status(const char *text, int error);
void studio_collab_control(const char *command, const char *argument);
bool studio_collab_busy(studio *s);
bool studio_collab_follow(studio *s, int peer);
void studio_collab_unfollow(studio *s);

/* Authored UI. Loads the Studio project's scene; NULL with an error when it
 * cannot. Free it after the kit, whose hosts hold its listener. */
/* The Studio scene's root ui document: its own, else the Studio project's
 * default, else "". */
const char *studio_ui_page(const studio_ui *ui);
studio_ui *studio_ui_new(rml_context *context, const char *project_root, char *error, size_t size);
void studio_ui_free(studio_ui *ui);
/* Tick authored behaviors every simulation frame after input; sync models only
 * when the host emission is due. */
bool studio_ui_tick(studio *s);
void studio_ui_frame(studio *s);
/* Shows the Studio scene entity `name`'s GameUi document in a kit host;
 * returns the host, for native slots, or NULL when it shows none. */
rml_element *studio_ui_panel(studio *s, const char *key, const char *name);
/* The Studio scene's panels, each an editor type an area can show. */
int studio_ui_panel_count(const studio *s);
const char *studio_ui_panel_name(const studio *s, int index);
/* Shows panel `name` filling area `area`'s body, whether or not its GameUi
 * is hidden. */
void studio_ui_area(studio *s, int area, const char *name);
/* Shows it over the editor while its GameUi is not hidden, as for a dialog
 * whose behaviors hide it again. */
void studio_ui_modal(studio *s, const char *name);
/* True when a focused authored chrome button can pass global shortcuts. */
bool studio_ui_shortcut_focus(studio *s);
/* True while a modal panel (Settings, Collaboration, Keyboard Shortcuts,
 * New Project) shows. */
bool studio_ui_modal_open(studio *s);
/* Clears its GameUi.hidden. */
void studio_ui_show(studio *s, const char *name);
/* Runs a collaboration action with transient values from the authored panel. */
void studio_ui_collaboration_action(studio *s, studio_command command);
/* Shows Settings searching for `query`. */
void studio_ui_show_settings(studio *s, const char *query);

/* The command palette (studio_palette.c), while open. */
#define STUDIO_PALETTE_PLACEHOLDER                                             \
    "Search, or type > for commands, @ for symbols, ? for help"
void studio_palette(studio *s);

/* Content changed on disk (studio_disk.c): reloads it and raises failures and
   unsaved conflicts as toasts. */
void studio_disk_frame(studio *s);
void studio_disk_free(studio *s);
void studio_reload(studio *s);

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
/* File > New > Project. */
void studio_new_project_menu(studio *s);
void studio_new_project_frame(studio *s);
void studio_new_project_open(studio *s);
void studio_new_project_action(studio *s, const char *action);
void studio_ui_new_project(studio *s);

/* Shell. */
void studio_emit(studio *s);
void studio_report(studio *s, const char *message);
/* Runs this frame's keys as commands, on the area under the pointer
 * (Blender). With `field`, a focused text field keeps every key but the
 * primary-modifier chords it edits no text with. */
void studio_shortcuts(studio *s, bool field);
/* Whether the key, under the primary modifier, edits a text field: the
 * clipboard, undo, select all and caret movement. */
bool studio_text_key(SDL_Keycode key);
/* The area a command acts on: the one under the pointer while a key runs
 * it, else the one clicked last, as menus act on the area opened over. */
int studio_command_area(const studio *s);
/* Focuses the search field just emitted when Find asked for `editor`'s. */
void studio_find_field(studio *s, studio_editor editor);
const char *studio_scene_title(const app_scene_authoring *scene, char *out, size_t size);

/* Some open document has edits that are not saved. */
bool studio_unsaved(studio *s);
/* Scene edits are allowed: the scene can move and shows no runtime snapshot. */
bool studio_editable(const studio *s);
/* The scene runs, playing or paused: Properties and the Scene edit its running
 * world, as Unity's Play Mode does, and Stop restores the authored scene. */
bool studio_live(const studio *s);
/* Case-insensitive substring filter; an empty query matches everything. */
bool studio_matches(const char *query, const char *text);

/* Editors emit into an area body; headers into the area header. */
void studio_scene_header(studio *s, int area);
void studio_scene_body(studio *s, int area);
/* Takes the view zoom's gestures from the Scene area's pointer in `input`,
 * given in area points, and leaves it in the frame's points. */
void studio_scene_view(studio *s, engine_viewport_host_input *input);
void studio_outliner_header(studio *s, int area);
void studio_outliner_body(studio *s, int area);
int *studio_outliner_rows(studio *s);
/* Hide in Editor (Blender's H, Shift+H and Alt+H): hides the selected objects,
 * or the others, from the Scene's edit view, or shows them all again. */
void studio_hide(studio *s, bool selected);
void studio_show_all(studio *s);
int studio_hidden_count(studio *s);
void studio_properties_header(studio *s, int area);
void studio_properties_body(studio *s, int area);
/* Properties shows the running scene's values, which edit live. */
bool studio_properties_live(studio *s);
void studio_code_header(studio *s, int area);
void studio_code_body(studio *s, int area);
/* Opens a UI asset's or stylesheet's source at a 1-based line, as Blender's
 * Edit Source does; false when it cannot be opened. */
bool studio_open_ui_source(studio *s, const char *path, int line);
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
/* Opens Find in graph. */
void studio_graph_find(studio *s);
/* Opens the Text Editor's find widget. */
void studio_code_find(studio *s);
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
/* Deletes a member at once, even while it is used. */
void studio_members_delete_now(studio *s, studio_member_kind kind, uint64_t id);
/* A rename of the open asset's function, macro or variable that other assets
   name goes through the refactor popover: true when it took it. */
bool studio_members_rename_across(
    studio *s,
    studio_member_kind kind,
    uint64_t id,
    const char *name,
    const char *new_name
);
/* The selected member as a key that changes with the selection, or 0. */
uint64_t studio_member_shown(studio *s);
void studio_member_properties(studio *s);
/* The type search: items for the command surface while it picks a type. */
bool studio_member_palette(studio *s);
void studio_assets_free(studio *s);
void studio_assets_header(studio *s, int area);
void studio_assets_body(studio *s, int area);
/* Keys over the Assets area: F2 renames the row under the pointer, X or
   Delete deletes it. True when one was taken. */
bool studio_assets_key(studio *s, SDL_Keycode key, SDL_Keymod mod);
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
/* Shows Output's Calls view the same way. */
void studio_output_calls(studio *s);
/* Shows an editor: in an area already showing it, else in the drawer. */
void studio_show_editor(studio *s, studio_editor editor);
/* Whether an area or the open drawer shows the editor. */
bool studio_editor_shown(const studio *s, studio_editor editor);

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
void studio_find_references(studio *s, const char *title, reference_symbol symbol);
void studio_find_view(studio *s, float row);
void studio_find_clear(studio *s);
/* What a referable node names, with a title for it; the index is refreshed
   first. An ASSET symbol at -1 for nothing. */
reference_symbol studio_node_symbol(studio *s, const node *n, const char **title);
/* The open graph document's position in the index as last built, or -1. */
int studio_document_asset(const studio *s);
/* Its position once the index is refreshed with the document's edits. */
int studio_refresh_document_asset(studio *s);
studio_reference studio_reference_of(const reference_index *index, const reference *use);
/* Opens a Find or Calls row's asset and reveals its node or object. */
void studio_open_reference(studio *s, const studio_reference *row);

/* Go to Definition (studio_definition.c): a call opens its graph, a Get or
   Set its variable in Members, an event node its event definition, Spawn
   Prototype its prototype, and a component node peeks its fields and their
   uses. */
bool studio_node_definable(const node *n);
void studio_go_to_definition(studio *s, node *n);
/* The component peek; call every frame with the Behavior area. */
void studio_definition_peek(studio *s);

/* Call Hierarchy (studio_calls.c): a function's or event's callers or
   callees as a tree in Output's Calls view. Calls and event nodes trace. */
bool studio_node_traceable(const node *n);
void studio_calls_node(studio *s, const node *n);
void studio_calls_graph(studio *s, const graph *gr);
void studio_calls_view(studio *s, float row);
void studio_calls_clear(studio *s);
/* Selects a member of the open behavior and scrolls Members to it. */
void studio_members_reveal(studio *s, studio_member_kind kind, uint64_t id);
/* Opens a graph of the open document and frames one of its nodes. */
void studio_reveal_node(studio *s, uint64_t graph_id, uint64_t node_id);
void studio_record(studio *s, kit_toast_type kind, const char *text);

/* Rename and safe delete across files (studio_refactor.c). A rename opens a
   popover with the new name and the assets it rewrites; a delete of what is
   still used lists the uses and asks. Applied changes offer Undo in a toast. */
/* Whether an asset renames and deletes as a file: scenes and systems are
   named elsewhere, a missing asset has no file, and nothing indexes the
   imports of a stylesheet. */
bool studio_asset_refactorable(const reference_asset *asset);
/* Renames an event, its definition when it has one, and what sends and
   handles it; host events keep their names. */
void studio_refactor_rename_event(studio *s, const char *event);
/* The event an Event, Call Event or Send Event node names, unless the host
   names it; NULL for other nodes. */
const char *studio_event_renameable(const node *n);
void studio_refactor_rename_asset(studio *s, int asset);
/* A member rename that reaches other assets goes through the popover: true
   when it took it. */
bool studio_refactor_member_rename(studio *s, reference_symbol symbol, const char *name, const char *new_name);
/* Asks before deleting a symbol still used: true when the popover took it;
   its Delete then deletes the member, or the asset when member is none. */
bool studio_refactor_confirm_delete(
    studio *s,
    reference_symbol symbol,
    const char *name,
    studio_member_kind member,
    uint64_t id
);
void studio_refactor_delete_asset(studio *s, int asset);
/* The popover and the Undo toast; call every frame. */
void studio_refactor_frame(studio *s);
void studio_refactor_free(studio *s);

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
/* Step Over, Into and Out in the transport while a run is paused. */
void studio_debug_steps(studio *s);
/* Resume and the steps as menu items, enabled while a run is paused. */
void studio_debug_menu(studio *s);
/* The paused run's call stack over the Behavior area. */
void studio_debug_call_stack(studio *s);

/* Commands (studio_commands.c): one table the menus, the command palette,
 * shortcuts and authored UI share, as VS Code's command registry. Authored
 * UI runs one with emit('studio.<id>'), or on a tab or an area with
 * emit('studio.<id>', index), and binds its state as `commands.<id>`
 * (studio_sources.c). */
typedef struct
{
    const char *id; /* stable, camelCase, as in emit('studio.toggleAssets') */
    const char *label;
    const char *menu;     /* the label in a menu */
    const char *shortcut; /* in the platform's notation, or NULL */
    const char *keywords; /* more words the palette matches */
    uint32_t icon;
    bool indexed; /* runs only on the tab its argument names; not in the palette */
} studio_command_info;

/* How the command reads now: Play reads Pause while a scene plays, and
 * Resume while a run is paused at a breakpoint. */
studio_command_info studio_command_describe(studio *s, studio_command command);
bool studio_command_enabled(studio *s, studio_command command);
/* 1 or 0 for a command that toggles something, else -1. */
int studio_command_checked(studio *s, studio_command command);
/* Runs the command when it is enabled. */
void studio_command_run(studio *s, studio_command command);
/* Runs the command on the tab or area `argument` names, as
 * emit('studio.closeTab', i) does; -1 acts on the active one, as
 * studio_command_run does. */
void studio_command_run_with(studio *s, studio_command command, int argument);
/* The command whose id is `id`, or -1. */
int studio_command_named(const char *id);
const char *studio_command_id(studio_command command);
/* A menu item for the command, run when chosen; true then. */
bool studio_command_item(studio *s, studio_command command);

/* Effective shortcuts and the contexts that can receive them. */
int studio_command_for_key(
    const studio_prefs *prefs,
    SDL_Keycode key,
    SDL_Keymod mod,
    studio_editor editor,
    bool mac
);
const char *studio_command_chord(const studio_prefs *prefs, studio_command command);
const char *studio_command_default_chord(studio_command command);
const char *studio_command_context(studio_command command);
/* Up to `max` chords that run the command: its binding, then the default's
 * aliases while it is not overridden. */
int studio_command_bindings(const studio_prefs *prefs, studio_command command, const char **out, int max);
const char *studio_command_label(studio_command command);
/* The area Keyboard Shortcuts lists the command under: Global, its areas
 * (as studio_command_context names them), or Debugger. */
const char *studio_command_group(studio_command command);
int studio_command_conflict(const studio_prefs *prefs, studio_command command, const char *chord, bool mac);
bool studio_chord_parse(const char *chord, bool mac, studio_key *out);
bool studio_chord_valid(const char *chord);
bool studio_chord_matches(const char *chord, SDL_Keycode key, SDL_Keymod mod, bool mac);
/* The key a chord names. Option on a Mac composes a character, which the
 * browser and SDL's web backend report as the key: ⌥G arrives as ©. */
SDL_Keycode studio_event_key(const SDL_KeyboardEvent *e);
void studio_chord_format(char *out, size_t size, const char *chord, bool mac);
bool studio_shortcut_record(studio_prefs *prefs, int *recording, studio_key key, bool mac);

/* What commands run (studio_shell.c). */
void studio_save(studio *s);
void studio_history(studio *s, bool redo);
void studio_runtime(studio *s, app_runtime_command command);
/* Record Gameplay toggles: it restarts the scene and records, then saves the
   play as a scenarios asset. */
void studio_record_gameplay(studio *s);
void studio_replay_scenario(studio *s, const char *name);
/* A scene run, a preview or a guest Studio is running. */
bool studio_session(const studio *s);
/* Play in the Studio project runs Studio itself, as the project's game: a
 * guest Studio in the Scene area, built from the project's working copy and
 * editing the same scene authoring. Both take effect at the next frame. */
void studio_guest_start(studio *s);
void studio_guest_stop(studio *s);
bool studio_can_replay(studio *s, bool redo);
/* The active document has unsaved edits. */
bool studio_active_dirty(studio *s);
/* Fills the workspace with one area, or restores the layout; -1 is no area. */
void studio_toggle_maximized(studio *s, int area);
/* The active workspace's default split sizes and area editors. */
void studio_reset_layout(studio *s);
/* Shows `workspace`'s arrangement as it was left. */
void studio_set_workspace(studio *s, studio_workspace workspace);
/* Hides or shows the right column, at its last width. */
void studio_toggle_side_pane(studio *s);
void studio_toggle_drawer(studio *s, studio_editor editor);
/* Steps UI Scale up (1) or down (-1), or restores it (0), within
 * [STUDIO_UI_SCALE_MIN, STUDIO_UI_SCALE_MAX]. */
#define STUDIO_UI_SCALE_MIN 0.5f
#define STUDIO_UI_SCALE_MAX 2.0f
void studio_zoom(studio *s, int step);
void studio_frame_selection(studio *s);
/* Fits every visible object in the Scene view (Blender's View All). */
void studio_frame_all(studio *s);
/* Marks every object, as a marquee around them all does. */
void studio_select_all(studio *s);
void studio_deselect(studio *s);
/* Duplicates the selection one grid increment down and right, as one Undo
   step, and selects the copies. */
void studio_duplicate(studio *s);
/* Deletes the selected and marked objects as one Undo step. */
void studio_delete(studio *s);
/* The object clipboard (Unity's, Godot's): Copy puts the selection on the
   system clipboard as scene text, Cut copies and deletes it, and Paste adds
   the clipboard's objects, centered under the pointer over the Scene, else
   one grid increment from where they were copied. */
void studio_copy(studio *s);
void studio_cut(studio *s);
void studio_paste(studio *s);
/* Up and Down in the Outliner step the active row; with Shift they extend
   the selection from its anchor (studio_commands.c). */
void studio_step_selection(studio *s, int direction, bool extend);
/* Selects the Outliner's rows from the anchor through `row`, a position in
   studio_outliner_rows, as Shift+click does. */
void studio_select_range(studio *s, const int *rows, int count, int row);
void studio_add_object(studio *s, const char *prototype);
uint32_t studio_prototype_icon(const studio *s, const char *prototype);

/* ---------- UI documents (workstream D, studio_code.c) ---------- */

/* Before the areas: applies UI edits live (the code_apply_live preference)
 * and shows the Text Editor when a UI document becomes active. */
void studio_code_frame(studio *s);
/* Opens the UI document `path` belongs to (ui_buffer_document) at a 1-based
 * line of its part, as a style origin names a line of a ui asset's .rcss
 * (UI_PART_RCSS); false when it cannot be opened. studio_open_ui_source opens
 * the part `path` is: a .rml's lines, which data-src names, or a .rcss's. */
bool studio_open_ui_part(studio *s, const char *path, ui_part part, int line);
/* The active UI document's text, or NULL when another kind is active. */
ui_buffer *studio_ui_buffer(studio *s);
/* Saves the active UI document, offering to save over a file that changed
 * on disk. */
void studio_code_save(studio *s);
app_disk_result studio_code_reload_path(studio *s, const char *path);
/* Closes a document's tab, asking first when it has unsaved edits, which
 * closing discards. */
void studio_close_tab(studio *s, int tab);
/* Workstream C: frees the Text Editor's code editor. */
void studio_code_free(studio *s);
/* Workstream I: opens a place in a UI document's part, as Go to Definition
 * and Find References rows do; false when it cannot be opened. */
bool studio_code_open_place(studio *s, const ui_code_place *place);
/* Assets ▸ New (studio_assets.c): the header's button opens a menu of every
   kind, grouped, with the Assets filter's kind first. A kind, from the menu
   or its New … command, asks for a name, then writes the asset and opens it
   as Assets opens it. The frame draws the menu and the name popover wherever
   they opened. */
void studio_new_asset_button(studio *s);
void studio_new_asset_start(studio *s, int kind);
void studio_new_asset_frame(studio *s);

/* ---------- UI Inspector (studio_inspect.c) ---------- */

/* The documents the Inspector shows: Studio's own, which its authored panels
 * share; the graph canvas's; and the game UI the Scene shows. */
typedef enum
{
    STUDIO_INSPECT_STUDIO,
    STUDIO_INSPECT_GRAPH,
    STUDIO_INSPECT_GAME,
    STUDIO_INSPECT_TARGET_COUNT,
} studio_inspect_target;

/* The pseudo classes the Inspector can force, as DevTools' Force state. */
typedef enum
{
    STUDIO_PSEUDO_HOVER,
    STUDIO_PSEUDO_ACTIVE,
    STUDIO_PSEUDO_FOCUS,
    STUDIO_PSEUDO_CHECKED,
    STUDIO_PSEUDO_COUNT,
} studio_pseudo;

/* A row of the Elements tree: an element, or a text node by its text. */
typedef struct studio_inspect_row
{
    int depth;
    bool node;   /* a text node */
    bool parent; /* has children to expand */
    bool open, selected, hovered;
    bool hidden;     /* not displayed */
    char indent[16]; /* its depth as padding, e.g. "28dp" */
    char tag[32];
    char id[64];       /* "#id", or "" */
    char classes[128]; /* ".a.b", or "" */
    char widget[64];   /* the ui asset or widget it shows (data-ui) */
    char text[96];     /* a text node's text, or an element's only text */
} studio_inspect_row;

/* A line of the Styles pane: a rule's heading, or a declaration under it. */
typedef struct studio_inspect_style
{
    bool heading;
    bool ancestor;      /* a heading naming the ancestor whose rules follow */
    bool inherited;     /* declared for an ancestor */
    bool linked;        /* the heading's origin opens in the Text Editor */
    char selector[128]; /* a heading's rule, "style" when inline */
    char origin[160];   /* a heading's file and line */
    char name[64], value[160];
    char file[256]; /* where a linked heading opens */
    int line;
} studio_inspect_style;

typedef struct studio_inspect_pair
{
    char name[64], value[160];
} studio_inspect_pair;

/* What the Inspector panel shows: the tree of the target's documents and
 * the selected element's box, styles, computed values and attributes. */
typedef struct studio_inspect_model
{
    studio_inspect_target target;
    bool targets[STUDIO_INSPECT_TARGET_COUNT]; /* which have a document now */
    bool picking;
    bool wide;         /* its area is wider than tall: panes side by side */
    char shortcut[32]; /* Inspect Element's, in the platform's notation */
    int revision;      /* counts the syncs that changed it */
    studio_inspect_row *rows;
    bool more; /* rows stopped at their limit */
    bool selection;
    bool detached;    /* the selected element left its document */
    bool native;      /* drawn by Studio's C code rather than from a ui asset */
    bool editable;    /* its source opens in the Text Editor */
    char title[160];  /* tag#id.class */
    char path[320];   /* its kit path, or the steps to it */
    char source[160]; /* "Asset:line", an asset, or "Native (C)" */
    char pseudo[96];  /* the pseudo classes set */
    bool force[STUDIO_PSEUDO_COUNT]; /* forced on the selection */
    char box[3][4][12]; /* margin, border and padding: top, right, bottom, left */
    char size[40];      /* the content box, "120 × 24" */
    studio_inspect_style *styles;
    studio_inspect_pair *computed, *attributes;
} studio_inspect_model;

/* What the panel asked for since the last sync: rows of the tree, a Styles
 * row whose origin to open, -1 for none; a target to show, or -1. */
typedef struct studio_inspect_input
{
    int select, toggle, hover, rule;
    int target;
    bool source; /* Edit Source */
    bool force[STUDIO_PSEUDO_COUNT];
} studio_inspect_input;

typedef struct studio_inspect studio_inspect;

studio_inspect *studio_inspect_new();
void studio_inspect_free(studio_inspect *in);
/* Selects `element` and reveals it in the tree. */
void studio_inspect_select(studio_inspect *in, rml_element *element);
/* Lists `context`'s documents as the tree and describes the selection; `k`
 * names its own elements by their key paths. */
const studio_inspect_model *studio_inspect_read(studio_inspect *in, rml_context *context, const kit *k);

/* Inspect Element: starts picking, showing the panel, or stops. */
void studio_inspect_toggle(studio *s);
bool studio_inspect_picking(const studio *s);
/* Takes Mod+Shift+C, and Escape while picking, before the documents do. */
bool studio_inspect_key(studio *s, const SDL_KeyboardEvent *key);
/* Applies what the panel asked and reads what it shows; NULL while neither
 * an area shows the panel nor a pick is on. */
const studio_inspect_model *studio_inspect_sync(studio *s, const studio_inspect_input *input);
/* The model the last sync read, whose strings last until its revision
 * changes; NULL before the Inspector opens. */
const studio_inspect_model *studio_inspect_shown(const studio *s);
/* After studio_emit: the pick overlay and the highlight. */
void studio_inspect_emit(studio *s);
/* ---------- UI wiring (workstream K) ---------- */

struct ui_event;
struct ui_data_binding;
/* The events the GameUi documents shown on whatever attaches the open
 * behavior send it (studio_graph_edit.c), which the Add menu offers as Event
 * nodes. Free them with ui_events_free. */
struct ui_event *studio_behavior_ui_events(studio *s);
/* Drops the events the open Add menu read once; it reads them again when it
 * next opens. */
void studio_add_menu_close(studio *s);
/* Reads what the open behavior's documents bind under self when the behavior
 * changes, or when `again`, and offers them to its graph editor. */
void studio_graph_ui_data(studio *s, bool again);

/* Asset pickers (studio_pick.c): a field naming an asset chooses one of its
 * kind on the command surface, as Unity's object field and Unreal's asset
 * picker do. */
typedef struct
{
    int kind;         /* reference_asset_kind */
    const char *none; /* the label of choosing none; NULL when a name is required */
    bool text;        /* a name no asset has yet may be typed */
} studio_pick_desc;
/* The key of the pick that previews the chosen ui asset. */
#define STUDIO_PICK_PREVIEW "@preview"
/* An asset of `kind` a field can name: not a name only a reference makes
 * up, nor a file of another format beside the assets, as a stylesheet under
 * ui/. */
bool studio_asset_nameable(const reference_asset *asset, int kind);
/* The names of the nameable assets of `kind`, sorted. The caller afrees the
 * array; the names belong to the index. */
const char **studio_asset_names(const reference_index *index, int kind);
/* An asset field showing `value`, keyed `key` across the project: the name
 * chosen for it this frame ("" for none), or NULL. */
const char *studio_asset_field(
    studio *s,
    const char *key,
    const char *value,
    const studio_pick_desc *desc,
    unsigned flags
);
/* Opens the command surface over desc's assets for `key`. */
void studio_pick_open(studio *s, const char *key, const char *current, const studio_pick_desc *desc);
/* Items for the command surface while it picks an asset. */
bool studio_pick_palette(studio *s);

/* Game UI errors (studio_canvas.c): the shown documents' error, the scene's
 * or the preview's, as "ui/Name.rml message". A change is recorded in
 * Output once; the Scene's notice and Output's Problems show it while it
 * lasts. */
void studio_ui_error(studio *s, const char *error);
/* The ui asset an error names, in `out`; false when it names none. */
bool studio_ui_error_asset(const char *error, char *out, size_t size);

/* The Canvas preview (studio_canvas.c; its header and body in
 * studio_scene.c): one ui asset alone over an empty world in the Scene area,
 * as Storybook shows a story, at a screen size (Unity UI Builder's canvas
 * size, UMG's screen size), on a light or dark backdrop, with a log of the
 * events it emits (Storybook's Actions). */
typedef enum
{
    STUDIO_CANVAS_FIT, /* the Scene area */
    STUDIO_CANVAS_PHONE,
    STUDIO_CANVAS_TABLET,
    STUDIO_CANVAS_DESKTOP,
    STUDIO_CANVAS_SIZE_COUNT,
} studio_canvas_size;

typedef struct
{
    const char *label;
    int width, height; /* logical pixels; zero for Fit */
} studio_canvas_size_info;
studio_canvas_size_info studio_canvas_size_of(studio_canvas_size size);

/* An event the previewed asset emitted, with its repeats in a row. */
typedef struct
{
    double time;
    int count;
    char event[128];
    char element[130]; /* "#id", or "" */
    char value[32];    /* an input's value, or "" */
} studio_canvas_action;

#define STUDIO_CANVAS_ACTIONS 200

typedef struct studio_canvas
{
    char asset[128]; /* "" while the Scene shows the scene */
    studio_canvas_size size;
    bool light;
    engine_viewport *viewport; /* its frame and documents */
    rml_element *frame; /* latest emitted pane, also the owner of Studio's surface input */
    ecs_world *world; /* empty: only the preview shows */
    game_save *save;  /* what its bindings set */
    studio_sources sources; /* Studio's data, which it shows with the Studio project open */
    studio_canvas_action *actions;
} studio_canvas;

/* Shows ui asset `asset` in the Scene area; NULL returns it to the scene. */
void studio_preview_ui(studio *s, const char *asset);
/* The Scene area follows the active document (Blender): while a ui asset's
 * UI document is active, it previews that asset, and when another kind of
 * document becomes active, it shows the scene again. Call every frame. */
void studio_canvas_follow(studio *s);
bool studio_ui_document_asset(const studio *s, const char *path, char *name, size_t size);
/* The previewed ui asset, or NULL while the Scene shows the scene. */
const char *studio_previewed_ui(const studio *s);
/* Draws the preview for the Scene's pointer `input` in place of the scene;
 * false while none shows. */
bool studio_canvas_draw(studio *s, engine_viewport_host_input *input);
void studio_canvas_free(studio *s);

/* ---------- Settings (workstream F) ---------- */

/* The Settings page's categories, in its sidebar's order. The Keyboard
 * category lists the command registry; Project edits the open project's
 * project.kdl. */
typedef enum
{
    STUDIO_SETTINGS_APPEARANCE,
    STUDIO_SETTINGS_SCENE,
    STUDIO_SETTINGS_GRAPH,
    STUDIO_SETTINGS_TEXT,
    STUDIO_SETTINGS_KEYBOARD,
    STUDIO_SETTINGS_PROJECT,
    STUDIO_SETTINGS_CATEGORY_COUNT,
} studio_settings_category;

typedef struct
{
    const char *id; /* what `settings.category` holds */
    const char *label;
    uint32_t icon;
    bool project; /* under the Project tab, else Editor */
} studio_settings_category_info;

extern const studio_settings_category_info studio_settings_categories[STUDIO_SETTINGS_CATEGORY_COUNT];

typedef enum
{
    STUDIO_SETTING_BOOL, /* a bool field; JSON true or false */
    STUDIO_SETTING_CHOICE, /* a choice's index in an int or bool field; JSON its value */
    STUDIO_SETTING_NUMBER, /* a float field within [min, max]; JSON a number */
} studio_setting_type;

typedef struct
{
    const char *value, *label;
} studio_setting_choice;

/* One preference (studio_settings.c): how the preferences file, the saved
 * keys authored panels bind and the Settings page read and write it. */
typedef struct
{
    const char *key;  /* its path in the preferences file: "code.vim" */
    const char *save; /* the saved key authored panels bind: "codeVim" */
    studio_settings_category category;
    const char *label;
    const char *description;
    const char *keywords; /* more words search matches */
    studio_setting_type type;
    size_t offset, size;                  /* the studio_prefs field */
    const studio_setting_choice *choices; /* a choice's values, or a number's presets */
    int choice_count;
    float min, max, step; /* a number's */
    float fallback; /* the default: 0 or 1, a choice's index, or a number */
} studio_setting;

extern const studio_setting studio_settings[];
extern const int studio_setting_count;

/* The value: 0 or 1, a choice's index, or a number. */
float studio_setting_get(const studio_prefs *prefs, const studio_setting *setting);
void studio_setting_set(studio_prefs *prefs, const studio_setting *setting, float value);
/* The value as text: "true" or "false", a choice's value, or a number. */
void studio_setting_text(const studio_prefs *prefs, const studio_setting *setting, char *out, size_t size);
/* Sets the value from text; false, leaving it, when the text is not valid. */
bool studio_setting_parse(studio_prefs *prefs, const studio_setting *setting, const char *text);
/* Whether each word of `query` is in the label, description, keywords or
 * category, ignoring case. */
bool studio_setting_matches(const studio_setting *setting, const char *query);
/* The same for any text: each word of `query` is somewhere in `text`. */
bool studio_settings_match(const char *query, const char *text);
const char *studio_standard_font(const studio_prefs *prefs);
const char *studio_code_font(const studio_prefs *prefs);
void studio_apply_font_settings(kit *k, const studio_prefs *prefs);
void studio_apply_graph_font_settings(kit *k, const studio_prefs *prefs);

/* The preferences in a preferences file's JSON, NULL for none: defaults
 * where a value is missing or invalid (studio_prefs.c). */
studio_prefs studio_prefs_read(const char *json);
/* Owned JSON text of the preferences file. Members of `saved`, the file's
 * text or NULL, that this build does not write stay, so builds of different
 * ages share the file without dropping each other's settings. */
char *studio_prefs_write(const studio_prefs *prefs, const char *saved);

/* The Project tab's rows: fields of the open project's project.kdl, in the
 * page's order. */
typedef enum
{
    STUDIO_PROJECT_NAME,        /* text */
    STUDIO_PROJECT_START_SCENE, /* a scene, by its path under content */
    STUDIO_PROJECT_UI,          /* the root document: a ui asset, or none */
    STUDIO_PROJECT_FIELD_COUNT,
} studio_project_field;

/* The Settings page's variable, `settings` (studio_sources.c): the Editor or
 * Project tab, the category, the search query, the categories and the rows
 * they show. Rows edit through the table; Keyboard rows list the commands. */
typedef struct studio_settings_view
{
    studio_prefs *prefs;          /* edited in place */
    studio_settings_cache *cache; /* retained by the model owner, optional */
    const studio_command_info *commands; /* the Keyboard category's */
    int command_count;
    int recording; /* command + 1, zero when idle */
    bool mac;
    const char *project; /* the open project's name */
    /* Each project.kdl field as the project reads it, "" for none; the
       page may set them. */
    char fields[STUDIO_PROJECT_FIELD_COUNT][256];
    /* The assets a field chooses from (support/array.h): Start Scene's
       scenes and Root Document's ui assets; none for Name. */
    studio_setting_choice *choices[STUDIO_PROJECT_FIELD_COUNT];
} studio_settings_view;

/* Adds the `settings` variable to a data model's JSON. */
void studio_settings_add(yyjson_mut_doc *doc);
/* Applies what the page's bindings changed to `view` when `wrote`, then shows
 * it; true when the page changed. */
bool studio_settings_sync(studio_settings_view *view, yyjson_mut_doc *doc, bool wrote);

/* ---------- Authored status bar (workstream J) ---------- */

/* A key hint, as Blender's status bar shows them: a mouse icon or a key, and
 * what it does. */
typedef struct studio_hint
{
    uint32_t icon;   /* a kit icon, or 0 for the key */
    const char *key; /* in the platform's notation */
    const char *label;
    int command; /* command + 1 for a rebindable hint, zero for editor gestures */
} studio_hint;

#define STUDIO_HINT_MAX 5

/* An open document as the top bar's tabs show it (workstream T). */
typedef struct studio_tab_view
{
    const char *label;
    const char *path; /* its file, the tab's tooltip */
    uint32_t icon;    /* a kit icon */
    bool dirty, active;
    bool closable; /* a UI document; others stay for the session */
} studio_tab_view;

/* What the editor's authored chrome shows: Studio's `editor` variable
 * (studio_sources.c), which projects/studio's StatusBar, TopBar and
 * AreaHeader bind. Commands' states are `commands`. */
typedef struct studio_status
{
    studio_hint hints[STUDIO_HINT_MAX]; /* the editor's under the pointer */
    int hint_count;
    const char *report;    /* the last operation */
    bool reporting;        /* while the report shows, three seconds */
    const char *scene;     /* the scene's title */
    const char *selection; /* the active object, "" for none */
    int selected;          /* how many objects are selected */
    int objects;
    int hidden; /* objects hidden in the Scene's edit view */
    bool playing;
    bool paused;                 /* the run is paused at a breakpoint */
    const studio_tab_view *tabs; /* the open documents */
    int tab_count;
    int maximized; /* the area filling the workspace, or -1 */
    unsigned live; /* a bit per area whose Properties edit the running scene */
} studio_status;

/* Adds the `editor` variable to a data model's JSON. */
void studio_status_add(yyjson_mut_doc *doc);
/* Shows `status` in it; true when anything changed. */
bool studio_status_show(yyjson_mut_doc *doc, const studio_status *status);

/* ---------- Visual designer (workstream H, studio_designer*.c) ---------- */

/* While a ui asset's UI document is active, the areas follow it, as Unity's
 * UI Builder and Unreal's UMG Designer arrange theirs: the Outliner shows its
 * Hierarchy, Properties its Details and Assets its Library, beside its Canvas
 * in the Scene area (studio_canvas.c) and its source in the Text Editor. Each
 * change is a text edit of the document (editor/documents/ui_design.h), and
 * selecting in the Hierarchy, in the Canvas or with the Text Editor's cursor
 * selects in all of them. */
typedef struct studio_designer studio_designer;

/* Whether the areas follow a UI document: the active one has markup. */
bool studio_designer_active(studio *s);
void studio_designer_hierarchy_header(studio *s, int area);
void studio_designer_hierarchy(studio *s, int area);
void studio_designer_details_header(studio *s, int area);
void studio_designer_details(studio *s, int area);
void studio_designer_library_header(studio *s, int area);
void studio_designer_library(studio *s, int area);
/* After the areas: follows the Text Editor's cursor, and outlines, picks and
 * takes Library drops in the Canvas. */
void studio_designer_frame(studio *s);
/* Keys over the Hierarchy or the Canvas: X or Delete deletes, Shift D
 * duplicates, and in the Hierarchy the arrows walk and fold; Space or Enter
 * picks the selected row up to move with the arrows, as dnd-kit's keyboard
 * sensor does (workstream Q). True when one was taken. */
bool studio_designer_key(studio *s, studio_editor editor, SDL_Keycode key, SDL_Keymod mod);
void studio_designer_free(studio *s);
/* The element of `rml`, the markup of ui asset `asset`, that the shown
 * `element` of `context`, or its nearest ancestor the asset wrote, was
 * written as, by the lines data-src names; -1 for none. */
int studio_designer_shown_source(
    rml_context *context,
    const ui_source *rml,
    const char *asset,
    rml_element *element
);
/* The first shown element of `context` written as element `source`, or NULL. */
rml_element *studio_designer_shown(rml_context *context, const ui_source *rml, const char *asset, int source);

/* ---------- Designer and editor polish (workstream Q) ---------- */

/* Back and Forward (studio_graph_nav.c) cover the Text Editor's jumps as
 * well as the graphs visited, as VS Code's Go Back crosses editors: Go to
 * Definition records the place it leaves and the one it lands on. */
void studio_nav_jump(studio *s, const ui_code_place *from, const ui_code_place *to);
bool studio_nav_can_step(const studio *s, int direction);
void studio_nav_step(studio *s, int direction);

/* A problem of a UI document in Output's Problems (studio_output.c): a
 * part's syntax error or RmlUi warning, where a row opens it. */
typedef struct studio_ui_problem
{
    ui_code_place at;
    bool error;
    char message[256];
} studio_ui_problem;

/* The problems of the UI document the Text Editor shows, as its markers
 * mark them: its syntax errors as it stands and RmlUi's warnings as last
 * checked. An array (support/array.h) valid until the next call. */
const studio_ui_problem *studio_code_problems(studio *s);
/* Every ui asset and stylesheet of the project, with the unsaved text of
 * open documents, checked as the Text Editor checks one; appended to
 * `out`. */
void studio_code_project_problems(studio *s, studio_ui_problem **out);

/* ---------- Authored top bar and area headers (workstream T) ---------- */

/* A command as authored UI binds it: `commands.<id>` (studio_sources.c),
 * with its label, menu label, shortcut, icon glyph, enabled and checked,
 * so a button reads data-attrif-data-disabled="!commands.save.enabled". */
typedef struct studio_command_state
{
    studio_command_info info;
    bool enabled;
    bool checked; /* a toggle that is on */
} studio_command_state;

/* Adds `commands`, a member per command id, to a data model's JSON. */
void studio_commands_add(yyjson_mut_doc *doc);
/* Shows the STUDIO_COMMAND_COUNT states in it, by command; true when
 * anything changed. */
bool studio_commands_show(yyjson_mut_doc *doc, const studio_command_state *states);
/* Whether document tab `tab` closes: UI documents do, and the others stay
 * for the session. */
bool studio_tab_closes(const studio *s, int tab);
/* The open documents as the top bar's tabs show them (support/array.h; the
 * caller frees it with afree). */
studio_tab_view *studio_document_tabs(studio *s);
/* Shows panel `name` as area `area`'s header in a host keyed "head": a
 * mount per area, whose root binds `area` to `editor.areas[area]`
 * (data-alias-area), as a prop. Returns the host, or NULL when the panel
 * has no document. */
rml_element *studio_ui_area_header(studio *s, int area, const char *name);
void studio_ui_area_detach(studio *s, int area);

/* ---------- Rename objects and prototype components ---------- */

/* Renames the selected object in place, as Blender's F2 and Unity's
 * Inspector name do: in its Outliner row while an area shows the Outliner,
 * else in Properties' name field. */
void studio_rename_object(studio *s);
/* Names the object `entity` authors, as one Undo step; a refused name
 * toasts why. */
void studio_rename_commit(studio *s, uint64_t entity, const char *name);
/* Add Component (Unity's Inspector): the command surface lists the
 * components the open prototype lacks, by the Properties tab each shows in. */
void studio_add_component_open(studio *s);
/* Its items while it is open. */
bool studio_component_palette(studio *s);
/* Runs the Undo a Remove Component toast offers; call every frame. */
void studio_component_toast(studio *s);
/* Keep Changes (Unreal's Keep Simulation Changes, K): the selected object's
 * running values, or those of its component `only`, apply to the scene when
 * it stops; a toast offers Undo. */
void studio_keep_changes(studio *s, const ecs_component_desc *only);
/* Runs the Undo a Keep Changes toast offers; call every frame. */
void studio_keep_toast(studio *s);

#endif
