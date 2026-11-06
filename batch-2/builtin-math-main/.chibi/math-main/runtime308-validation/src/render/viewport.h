#ifndef CHIBI_RENDER_VIEWPORT_H
#define CHIBI_RENDER_VIEWPORT_H

#include "render/view.h"
#include "runtime/engine.h"

typedef struct engine_viewport engine_viewport;
typedef struct render_scene3d_camera render_scene3d_camera;
typedef struct render_ui render_ui;
typedef struct ui_world ui_world;
typedef struct rml_input rml_input;

/* Host viewport surface. The first requested size replaces the placeholder
 * target on the next draw. Later requests are coalesced, and the target is
 * recreated once a size has held for two draws. */
engine_viewport *engine_viewport_new(int width, int height);
void engine_viewport_free(engine_viewport *viewport);
int engine_viewport_request_resize(engine_viewport *viewport, int width, int height);

typedef struct
{
    bool playing;
    bool session;
    bool editing;
} engine_viewport_controls;

/* Render the canvas and interpret this frame's host input. Returns -1 when the
 * canvas cannot be resized. */
int engine_viewport_draw(engine_viewport *viewport, const engine_viewport_controls *controls);

/* Pointer state from a retained UI host, in logical pixels relative to the image. */
typedef struct engine_viewport_host_input
{
    float width, height;
    float resolution; /* canvas pixels per unit of width and height, at least 1 */
    float x, y;
    float press_x, press_y;
    bool press_position; /* optional left button-down coordinates */
    bool hovered;
    bool focused; /* keyboard belongs to the Scene area */
    bool blocked; /* an overlay or popup covers the image */
    bool down[3], pressed[3], released[3];
    float wheel;
    bool alt;
    /* The modifier keys held (RML_INPUT_MOD_*), traded as the keys' are, so
       Control is a game's Mod (Command on a Mac) in its events' payloads. */
    unsigned modifiers;
    bool escape;
    char dropped_prototype[256];
    /* Key presses and typed text the host passes on, in order, kept until
       its engine_viewport_draw: while `focused` and the game plays, its
       documents take them, so their data-keys bindings and text fields
       hear them. */
    const rml_input *keys;
    int key_count;
} engine_viewport_host_input;

/* Required before engine_viewport_draw. The host displays the canvas texture. */
void engine_viewport_set_host_input(engine_viewport *viewport, const engine_viewport_host_input *input);
/* CF texture id of the current canvas target; changes when the canvas resizes. */
uint64_t engine_viewport_texture(const engine_viewport *viewport);
typedef struct engine_viewport_pointer
{
    float world_x;
    float world_y;
    float world_per_pixel;
    /* world_x and world_y hold the pointer's point: always in a 2D view, and
       in perspective where its ray meets the ground plane. */
    bool grounded;
    float ndc_x, ndc_y;
    float press_ndc_x, press_ndc_y;
    bool hovered;
    bool pressed;
    bool down;
    bool released;
    bool dragging;
    bool cancel;
    char dropped_prototype[256];
} engine_viewport_pointer;

/* Copied from the most recent draw. A left gesture starts inside the image and
 * remains captured outside it through release, above a perspective view's
 * horizon too. Hidden panes or blocked input cancel the gesture. World
 * coordinates use the rendered Y-up projection. */
void engine_viewport_get_pointer(const engine_viewport *viewport, engine_viewport_pointer *pointer);
/* Sample this frame's mouse against the last displayed image rectangle. */
bool engine_viewport_mouse_world(const engine_viewport *viewport, float *x, float *y);
/* The pointer's world-space ray uses the camera and rectangle of the last
 * displayed 3D image, even when a resize or view change is pending. */
bool engine_viewport_mouse_ray(const engine_viewport *viewport, CF_Ray3 *ray);
/* The runtime whose audio plays while a session is shown. */
void engine_viewport_set_runtime(engine_viewport *viewport, const engine *runtime);
/* Shows `world`'s Ui documents from content_root/ui over the scene
 * (render/ui.h). */
void engine_viewport_set_ui(engine_viewport *viewport, const ui_world *world, const char *content_root);
/* The host's frame time in seconds, which the documents' transitions follow,
 * so a fixed timestep replays them as it does the scene. Hosts set it before
 * each draw; until then the documents hold still. */
void engine_viewport_set_time(engine_viewport *viewport, double seconds);
/* Presents `ui`, which the viewport takes, as the documents
 * engine_viewport_set_ui creates: for a host that gives them its own data
 * (render_ui_new). Call before the viewport has documents. */
void engine_viewport_adopt_ui(engine_viewport *viewport, render_ui *ui);
/* The documents engine_viewport_set_ui created, for a host's overlay. */
render_ui *engine_viewport_ui(const engine_viewport *viewport);
/* Whether the documents took the pointer in the last draw. */
bool engine_viewport_ui_capture(const engine_viewport *viewport);
/* Whether the documents had the keyboard in the last draw with a text field
 * focused: typed letters are the field's, not gameplay's or a shortcut's. */
bool engine_viewport_ui_typing(const engine_viewport *viewport);

/* Whether the scene area held the keyboard as of the last draw. */
int engine_viewport_focused(const engine_viewport *viewport);
void engine_viewport_pixel_size(const engine_viewport *viewport, int *width, int *height);
void engine_viewport_get_view(const engine_viewport *viewport, engine_viewport_view *view);
void engine_viewport_set_view(engine_viewport *viewport, const engine_viewport_view *view);

void engine_viewport_get_camera(const engine_viewport *viewport, engine_viewport_camera *camera);
/* The 3D camera is rebuilt from the current viewport frame, so panning and
 * zooming remain synchronized with picking. */
void engine_viewport_get_camera3d(const engine_viewport *viewport, render_scene3d_camera *camera);
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

/* Render to the canvas without a UI host or presentation call. */
int engine_viewport_render(engine_viewport *, float width, float height);
/* Queue the canvas across the host target. Its normal draw flush performs the blit. */
void engine_viewport_present(engine_viewport *);
/* Image hosts supply the displayed origin and materialized input events. */
void engine_viewport_set_image_origin(engine_viewport *, float x, float y);
void engine_viewport_image_pointer(
    engine_viewport *,
    float x,
    float y,
    bool pressed,
    bool released,
    float wheel,
    unsigned modifiers,
    bool active
);
void engine_viewport_ui_event(engine_viewport *, const rml_input *input);
void engine_viewport_ui_keyboard(engine_viewport *, bool active);

#endif
