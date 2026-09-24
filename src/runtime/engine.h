#ifndef CHIBI_ENGINE_H
#define CHIBI_ENGINE_H

#include "ecs/ecs.h"
#include "runtime/behaviors/live.h"
#include "runtime/components.h"
#include "runtime/systems/schedule.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct engine engine;
typedef struct engine_viewport engine_viewport;
typedef struct render_arena_camera render_arena_camera;

typedef enum
{
    ENGINE_FOCUS_NONE,
    ENGINE_FOCUS_VIEWPORT,
    ENGINE_FOCUS_TEXT,
    ENGINE_FOCUS_CANVAS,
} engine_focus;

#define ENGINE_FIXED_SECONDS (1.0 / 60.0)
#define ENGINE_STEPS_MAX 8
#define ENGINE_EVENT_BATCHES_MAX 8
#define ENGINE_FRAME_SECONDS_MAX 0.25

typedef struct
{
    uint64_t frame;
    uint64_t simulation_steps;
    double simulation_time;
    double dropped_seconds; /* Cumulative wall time omitted by overload/debug holds. */
    int event_backlog; /* Events deferred after the bounded reaction pump. */
    int steps; /* Fixed clock ticks completed during this display frame. */
    float alpha; /* Presentation interpolation; paused and explicit Step show solved state. */
    double behavior_ms; /* Includes event dispatch and spawn initialization. */
    double physics_ms;
    double events_ms; /* Contact reactions, included in behavior_ms. */
    double systems_ms;
    double rendering_ms;
    uint32_t command_apply_count;
} engine_frame_timing;

typedef void (*engine_render_fn)(void *user);

engine *engine_new(ecs_world *world, game_live_world *live);
void engine_free(engine *engine);
/* Reset time and input before recreating authored entities and their Begin runs. */
void engine_reset(engine *engine);
int engine_register_system(engine *engine, const engine_system_desc *desc);
const system_schedule *engine_system_schedule(const engine *engine);
void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user);
int engine_set_focus(engine *engine, engine_focus focus);
engine_focus engine_get_focus(const engine *engine);
int engine_route_escape(engine *engine);
int engine_set_playing(engine *engine, bool playing);
bool engine_is_playing(const engine *engine);
int engine_step(engine *engine);
int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, flow_error *error);
/* Host input ingress: hold the latest movement/aim sample across fixed steps. */
int engine_sample_movement(engine *engine, float x, float y);
int engine_sample_aim(engine *engine, CF_V2 aim, bool firing, bool point);
/* Held physical controls interpreted through authored GameActionBinding entities. */
int engine_sample_controls(engine *engine, uint64_t controls, bool *firing);
int engine_frame(engine *engine, double monotonic_time);
/* Borrowed diagnostic from the last frame failure, or NULL after success. */
const char *engine_error(const engine *engine);
const game_visibility *engine_visibility(const engine *engine);
game_audio *engine_audio(const engine *engine);
const game_materials *engine_materials(const engine *engine);
bool engine_entity_visible(const engine *engine, ecs_world *world, ecs_entity entity);
const engine_frame_timing *engine_last_timing(const engine *engine);
const game_behavior_profile_frame *engine_behavior_profile(const engine *engine);
/* Detailed per-behavior timers are opt-in; aggregate frame timing stays active. */
void engine_set_behavior_profiling(engine *engine, bool enabled);
bool engine_render_position(const engine *engine, ecs_entity entity, GameTransform *out);

/* Host viewport surface. Resize requests are coalesced; the target is
 * recreated only when engine_viewport_commit_resize is called. */
engine_viewport *engine_viewport_new(int width, int height);
void engine_viewport_free(engine_viewport *viewport);
int engine_viewport_request_resize(engine_viewport *viewport, int width, int height);
int engine_viewport_commit_resize(engine_viewport *viewport);
typedef enum
{
    ENGINE_VIEWPORT_ERROR = -1,
    ENGINE_VIEWPORT_NONE = 0,
    ENGINE_VIEWPORT_PLAY,
    ENGINE_VIEWPORT_PAUSE,
    ENGINE_VIEWPORT_STEP,
    ENGINE_VIEWPORT_RESTART,
    ENGINE_VIEWPORT_STOP,
    ENGINE_VIEWPORT_FIT,
} engine_viewport_action;

typedef struct
{
    bool playing;
    bool session;
    bool editing;
    const char *error; /* Borrowed renderer diagnostic, read after drawing. */
} engine_viewport_controls;

/* Controls are borrowed for this draw. NULL draws only the viewport. */
/* Draw a framed, noninteractive canvas inside the current ImGui region. */
int engine_viewport_image(engine_viewport *viewport, float width, float height);
engine_viewport_action engine_viewport_draw(
    engine_viewport *viewport,
    const engine_viewport_controls *controls
);

/* Pointer state from a retained UI host, in logical pixels relative to the image. */
typedef struct engine_viewport_host_input
{
    float width, height;
    float x, y;
    bool hovered;
    bool focused; /* keyboard belongs to the Scene area */
    bool blocked; /* an overlay or popup covers the image */
    bool down[3], pressed[3], released[3];
    float wheel;
    bool alt;
    bool escape;
    char dropped_prototype[256];
} engine_viewport_host_input;

/* While set, engine_viewport_draw renders the canvas and interprets this input
 * instead of drawing an ImGui pane. The host displays the canvas texture. */
void engine_viewport_set_host_input(engine_viewport *viewport, const engine_viewport_host_input *input);
/* CF texture id of the current canvas target; changes when the canvas resizes. */
uint64_t engine_viewport_texture(const engine_viewport *viewport);
typedef struct engine_viewport_pointer
{
    float world_x;
    float world_y;
    float world_per_pixel;
    float ndc_x, ndc_y;
    bool hovered;
    bool pressed;
    bool down;
    bool released;
    bool dragging;
    bool cancel;
    bool delete_pressed;
    char dropped_prototype[256];
} engine_viewport_pointer;

/* Copied from the most recent draw. A left gesture starts inside the image and
 * remains captured outside it through release. Hidden panes or blocked input
 * cancel the gesture. World coordinates use the rendered Y-up projection. */
void engine_viewport_get_pointer(const engine_viewport *viewport, engine_viewport_pointer *pointer);
/* Sample this frame's mouse against the last displayed image rectangle. */
bool engine_viewport_mouse_world(const engine_viewport *viewport, float *x, float *y);
void engine_viewport_set_widgets(
    engine_viewport *viewport,
    ecs_world *world,
    game_entities *entities,
    const engine *runtime
);
bool engine_viewport_widgets_capture(const engine_viewport *viewport);

/* ENG-3: whether the scene panel held the keyboard as of the last draw, and a
 * way to hand focus back so Escape can release the viewport. */
int engine_viewport_focused(const engine_viewport *viewport);
void engine_viewport_release_focus(void);
uint64_t engine_viewport_binding(const engine_viewport *viewport);
uint32_t engine_viewport_recreate_count(const engine_viewport *viewport);
void engine_viewport_pixel_size(const engine_viewport *viewport, int *width, int *height);
typedef struct engine_viewport_camera
{
    float center_x;
    float center_y;
    float span_width;
    float span_height;
} engine_viewport_camera;

/* Logical camera framing, independent of viewport aspect ratio. */
typedef struct engine_viewport_view
{
    float center_x;
    float center_y;
    float width;
    float height;
} engine_viewport_view;

void engine_viewport_get_view(const engine_viewport *viewport, engine_viewport_view *view);
void engine_viewport_set_view(engine_viewport *viewport, const engine_viewport_view *view);

void engine_viewport_get_camera(const engine_viewport *viewport, engine_viewport_camera *camera);
/* The optional arena presentation camera is rebuilt from the current viewport
 * frame, so panning and zooming remain synchronized with picking. */
void engine_viewport_set_arena_showcase(engine_viewport *viewport, float time);
void engine_viewport_clear_arena_camera(engine_viewport *viewport);
void engine_viewport_get_arena_camera(const engine_viewport *viewport, render_arena_camera *camera);
void engine_viewport_set_perspective(engine_viewport *viewport, bool perspective);
bool engine_viewport_perspective(const engine_viewport *viewport);
void engine_viewport_fit(engine_viewport *viewport, float min_x, float min_y, float max_x, float max_y);
void engine_viewport_reset_camera(engine_viewport *viewport);

typedef void (*engine_viewport_render_fn)(void *user);
void engine_viewport_set_render_callback(
    engine_viewport *viewport,
    engine_viewport_render_fn fn,
    void *user
);

#endif
