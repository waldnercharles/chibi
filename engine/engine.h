#ifndef CBLUEPRINT_ENGINE_H
#define CBLUEPRINT_ENGINE_H

#include "../ecs/ecs.h"
#include "../game/live.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct engine engine;
typedef struct engine_viewport engine_viewport;

typedef enum
{
    ENGINE_FOCUS_NONE,
    ENGINE_FOCUS_VIEWPORT,
    ENGINE_FOCUS_TEXT,
    ENGINE_FOCUS_CANVAS,
} engine_focus;

typedef struct
{
    uint64_t frame;
    double behavior_ms;
    double systems_ms;
    double rendering_ms;
    uint32_t command_apply_count;
} engine_frame_timing;

typedef void (*engine_system_fn)(ecs_world *world, double now, void *user);
typedef void (*engine_render_fn)(void *user);

engine *engine_new(ecs_world *world, game_live_world *live);
void engine_free(engine *engine);
int engine_add_system(engine *engine, engine_system_fn fn, void *user);
void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user);
int engine_set_focus(engine *engine, engine_focus focus);
engine_focus engine_get_focus(const engine *engine);
int engine_route_escape(engine *engine);
int engine_set_playing(engine *engine, bool playing);
bool engine_is_playing(const engine *engine);
int engine_step(engine *engine);
int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, flow_error *error);
int engine_frame(engine *engine, double monotonic_time);
const engine_frame_timing *engine_last_timing(const engine *engine);

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
    const char *title;
    bool playing;
    bool session;
    bool editing;
} engine_viewport_controls;

/* Controls are borrowed for this draw. NULL draws only the viewport. */
engine_viewport_action engine_viewport_draw(
    engine_viewport *viewport,
    const engine_viewport_controls *controls
);
typedef struct engine_viewport_pointer
{
    float world_x;
    float world_y;
    float world_per_pixel;
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
void engine_viewport_fit(engine_viewport *viewport, float min_x, float min_y, float max_x, float max_y);
void engine_viewport_reset_camera(engine_viewport *viewport);

typedef void (*engine_viewport_render_fn)(void *user);
void engine_viewport_set_render_callback(
    engine_viewport *viewport,
    engine_viewport_render_fn fn,
    void *user
);

#endif
