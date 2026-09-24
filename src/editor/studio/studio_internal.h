#ifndef CHIBI_EDITOR_STUDIO_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_INTERNAL_H

#include "editor/rmlui/backend.h"
#include "editor/studio/studio.h"
#include "editor/ui/kit.h"
#include "editor/ui/kit_icons.h"
#include "editor/workspace/scene_internal.h"

#include <SDL3/SDL_events.h>
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
    STUDIO_TAB_COUNT,
} studio_tab;

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
    float side_width;
    float main_split;
    float side_split;
    bool drawer_open;
    float drawer_height;
    studio_editor editors[STUDIO_AREA_COUNT];
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

typedef struct
{
    SDL_Keycode key;
    SDL_Keymod mod; /* at press time: a quick chord is released before the frame runs */
} studio_key;

struct studio
{
    app_scene_authoring *scene;
    engine_viewport *viewport;
    editor_rml_backend *backend;
    kit *kit;
    kit_frame_input in;
    studio_area areas[STUDIO_AREA_COUNT];
    int hover_area;
    int focus_area;
    studio_prefs prefs;
    studio_prefs applied; /* last applied copy; changes apply and save by comparison */
    double prefs_changed_at;
    bool system_light;
    bool light;
    int theme_switching; /* frames left with transitions suspended */
    bool drawer_dragging;
    SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];
    int cursor_shown; /* SDL_SystemCursor applied, -1 before the first frame */
    kit_rect main_rect;
    kit_rect side_rect;
    studio_tab tab;
    studio_record_entry *records;
    studio_perf_sample *perf;
    double perf_readout_time;
    char perf_readout[96];
    int assets_kind;           /* asset_index_kind, or -1 for all */
    unsigned assets_collapsed; /* bit per asset_index_kind */
    char assets_filter[128];
    char dropped_prototype[256]; /* placed by the viewport this frame */
    int output_view;
    char output_filter[128];
    char outliner_filter[128];
    bool outliner_collapsed;
    bool outliner_reveal; /* scroll the selection into view on the next frame */
    char property_search[128];
    bool text_input;
    kit_surface_input surface;
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
    int undo_toast;    /* toast offering Undo for undo_toast_group */
    uint64_t undo_toast_group;
    char status[256];
    char seen_message[256]; /* last authoring message reported */
    double status_time;
    double time;
};

/* Preferences. */
studio_prefs studio_prefs_default();
studio_prefs studio_prefs_load();
bool studio_prefs_save(const studio_prefs *prefs);
bool studio_prefs_equal(const studio_prefs *a, const studio_prefs *b);

/* Shell. */
void studio_emit(studio *s);
void studio_report(studio *s, const char *message);
void studio_shortcuts(studio *s);
bool studio_key_pressed(const studio *s, SDL_Keycode key);
const char *studio_scene_title(const app_scene_authoring *scene, char *out, size_t size);

/* Editors emit into an area body; headers into the area header. */
void studio_scene_header(studio *s, int area);
void studio_scene_body(studio *s, int area);
void studio_outliner_header(studio *s, int area);
void studio_outliner_body(studio *s, int area);
int *studio_outliner_rows(studio *s);
void studio_properties_header(studio *s, int area);
void studio_properties_body(studio *s, int area);
void studio_behavior_body(studio *s, int area);
void studio_assets_header(studio *s, int area);
void studio_assets_body(studio *s, int area);
const char *studio_asset_kind_name(int kind);
uint32_t studio_asset_kind_icon(int kind);
void studio_performance_sample(studio *s, double delta_seconds);
void studio_performance_header(studio *s, int area);
void studio_performance_body(studio *s, int area);
void studio_output_header(studio *s, int area);
void studio_output_body(studio *s, int area);
void studio_record(studio *s, kit_toast_type kind, const char *text);

/* Commands shared by menus, shortcuts and the command palette. */
void studio_toggle_drawer(studio *s, studio_editor editor);
void studio_save(studio *s);
void studio_history(studio *s, bool redo);
void studio_runtime(studio *s, app_runtime_command command);
void studio_frame_selection(studio *s);
void studio_duplicate(studio *s);
void studio_delete(studio *s);
void studio_add_object(studio *s, const char *prototype);
uint32_t studio_prototype_icon(const studio *s, const char *prototype);

#endif
