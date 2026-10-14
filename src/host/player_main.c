#include "execution/backend.h"
#include "execution/preparation_profile.h"
#include "host/native_default.h"
#include "host/player_clock.h"
#include "host/player_url.h"
#include "host/ui_replay_runtime.h"
#include "platform/cute.h"
#include "render/device_controls.h"
#include "render/scene3d.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "render/shader_images.h"
#include "render/shader_parameters.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "runtime/player_prepared.h"
#include "support/array.h"
#include "support/file_io.h"
#include "support/resources.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#ifdef CHIBI_AUTHORING_HOST
#include "assets/project.h"
#include "authoring/graph_catalog.h"
#include "authoring/project_migration.h"
#include "authoring/resource_previews_definition.h"
#include "authoring/ui_dom_definition.h"
#include "authoring/workspace_tools_definition.h"
#include "authoring/visual_authoring_preview_definition.h"
#include "authoring/workspace_catalog.h"
#include "authoring/workspace_runtime_definition.h"
#include "authoring/workspace_simulation_definition.h"
#include "graph/model/registry.h"
#include "host/authoring_platform_definition.h"
#include "host/workspace_files_web.h"
#include "host/workspace_host.h"
#include "runtime/declared_components.h"
#include "support/rooted_files.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <stdatomic.h>
#endif

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* This final application supplies one explicit native type composition. */
#ifdef CHIBI_AUTHORING_HOST
static type_catalog_pack *host_type_packs;

static void initialize_type_packs()
{
    assert(!host_type_packs);
    apush(host_type_packs, TYPE_PACK);
    int workspace_count = authoring_workspace_type_pack_count();
    for (int i = 0; i < workspace_count; i++) {
        apush(host_type_packs, authoring_workspace_type_pack_at(i));
    }
    int registry_count = registry_pack_count();
    for (int i = 0; i < registry_count; i++) {
        const registry_pack *pack = registry_pack_at(i);
        if ((pack->type_count || pack->enum_count) && pack->types != TYPE_PACK.types) {
            apush(
                host_type_packs,
                ((type_catalog_pack){ pack->types, pack->type_count, pack->enums, pack->enum_count })
            );
        }
    }
}
#endif
int type_catalog_pack_count()
{
#ifdef CHIBI_AUTHORING_HOST
    return host_type_packs ? acount(host_type_packs)
                           : 1 + authoring_workspace_type_pack_count();
#else
    return 1;
#endif
}
type_catalog_pack type_catalog_pack_at(int index)
{
#ifdef CHIBI_AUTHORING_HOST
    assert(index >= 0 && index < type_catalog_pack_count());
    if (!host_type_packs) {
        return index ? authoring_workspace_type_pack_at(index - 1) : TYPE_PACK;
    }
    return host_type_packs[index];
#else
    if (index != 0) {
        abort();
    }
    return TYPE_PACK;
#endif
}
typedef struct
{
    player *player;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    bool render_failed;
    ui_replay_session *replay;
    int replay_frame;
    /* A browser keeps calling frame() after main returns, so everything a
     * frame reads lives here rather than on main's stack. */
    native_prepared_context context;
    execution_prepared_linker linker;
    ui_world ui;
    double now;
    double fixed_dt;
    int width, height; /* Explicit canvas dimensions, independent of display DPI. */
#ifdef CHIBI_AUTHORING_HOST
    workspace_host *workspace;
    authoring_graph_catalog *graphs;
    workspace_host_config workspace_config;
    char *artifact, *workspace_root, *compiler, *launcher;
    char *project_name, *save_path;
    behavior_backend backend;
    SDL_EventFilter previous_filter;
    void *previous_filter_context;
    bool filtering;
    SDL_WindowID window_id;
    atomic_bool close_requested;
#endif
} desktop_player;
#define BUNDLE_ARTIFACT "game.chibip"
#define BUNDLE_ENGINE "engine"
#define BUNDLE_MOUNT "/chibi-game"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define CAPTURE_MAX_DIMENSION 640
#define CAPTURE_MAX_PIXELS 230400
static volatile sig_atomic_t stopped;
#ifdef CHIBI_AUTHORING_HOST
static bool authoring_event_filter(void *context, SDL_Event *event)
{
    desktop_player *host = context;
    if (event->type == SDL_EVENT_QUIT ||
        (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
         event->window.windowID == host->window_id)) {
        atomic_store(&host->close_requested, true);
        return false;
    }
    return !host->previous_filter ||
           host->previous_filter(host->previous_filter_context, event);
}
#endif
static rml_input keys[128];
static int key_count;
static bool input_overflow;
static uint64_t pressed_controls;
static void stop(int)
{
    stopped = 1;
}
static unsigned modifiers()
{
    return (cf_key_shift() ? RML_INPUT_MOD_SHIFT : 0) |
           (cf_key_ctrl() ? RML_INPUT_MOD_CONTROL : 0) |
           (cf_key_alt() ? RML_INPUT_MOD_ALT : 0) |
           (cf_key_gui() ? RML_INPUT_MOD_META : 0);
}
static rml_input_key ui_key(CF_KeyButton key)
{
    if (key >= CF_KEY_A && key <= CF_KEY_Z) {
        return RML_INPUT_KEY_A + key - CF_KEY_A;
    }
    if (key >= CF_KEY_0 && key <= CF_KEY_9) {
        return RML_INPUT_KEY_0 + key - CF_KEY_0;
    }
    if (key >= CF_KEY_F1 && key <= CF_KEY_F12) {
        return RML_INPUT_KEY_F1 + key - CF_KEY_F1;
    }
    if (key >= CF_KEY_KP_1 && key <= CF_KEY_KP_9) {
        return RML_INPUT_KEY_NUMPAD1 + key - CF_KEY_KP_1;
    }
    switch (key) {
        case CF_KEY_EQUALS: return RML_INPUT_KEY_OEM_PLUS;
        case CF_KEY_SEMICOLON: return RML_INPUT_KEY_OEM_1;
        case CF_KEY_COMMA: return RML_INPUT_KEY_OEM_COMMA;
        case CF_KEY_MINUS: return RML_INPUT_KEY_OEM_MINUS;
        case CF_KEY_PERIOD: return RML_INPUT_KEY_OEM_PERIOD;
        case CF_KEY_SLASH: return RML_INPUT_KEY_OEM_2;
        case CF_KEY_BACKQUOTE: return RML_INPUT_KEY_OEM_3;
        case CF_KEY_LEFTBRACKET: return RML_INPUT_KEY_OEM_4;
        case CF_KEY_BACKSLASH: return RML_INPUT_KEY_OEM_5;
        case CF_KEY_RIGHTBRACKET: return RML_INPUT_KEY_OEM_6;
        case CF_KEY_QUOTE: return RML_INPUT_KEY_OEM_7;
        case CF_KEY_KP_0: return RML_INPUT_KEY_NUMPAD0;
        case CF_KEY_KP_ENTER: return RML_INPUT_KEY_NUMPAD_ENTER;
        case CF_KEY_KP_MULTIPLY: return RML_INPUT_KEY_MULTIPLY;
        case CF_KEY_KP_PLUS: return RML_INPUT_KEY_ADD;
        case CF_KEY_KP_MINUS: return RML_INPUT_KEY_SUBTRACT;
        case CF_KEY_KP_PERIOD: return RML_INPUT_KEY_DECIMAL;
        case CF_KEY_KP_DIVIDE: return RML_INPUT_KEY_DIVIDE;
        case CF_KEY_SPACE: return RML_INPUT_KEY_SPACE;
        case CF_KEY_TAB: return RML_INPUT_KEY_TAB;
        case CF_KEY_RETURN: return RML_INPUT_KEY_ENTER;
        case CF_KEY_ESCAPE: return RML_INPUT_KEY_ESCAPE;
        case CF_KEY_BACKSPACE: return RML_INPUT_KEY_BACKSPACE;
        case CF_KEY_DELETE: return RML_INPUT_KEY_DELETE;
        case CF_KEY_LEFT: return RML_INPUT_KEY_LEFT;
        case CF_KEY_RIGHT: return RML_INPUT_KEY_RIGHT;
        case CF_KEY_UP: return RML_INPUT_KEY_UP;
        case CF_KEY_DOWN: return RML_INPUT_KEY_DOWN;
        case CF_KEY_HOME: return RML_INPUT_KEY_HOME;
        case CF_KEY_END: return RML_INPUT_KEY_END;
        case CF_KEY_PAGEUP: return RML_INPUT_KEY_PAGE_UP;
        case CF_KEY_PAGEDOWN: return RML_INPUT_KEY_PAGE_DOWN;
        case CF_KEY_INSERT: return RML_INPUT_KEY_INSERT;
        default: return RML_INPUT_KEY_UNKNOWN;
    }
}
static void key_changed(CF_KeyButton key, bool down)
{
    rml_input_key mapped = ui_key(key);
    if (mapped) {
        if (key_count == 128) {
            input_overflow = true;
        } else {
            keys[key_count++] = (rml_input){ .type = down ? RML_INPUT_KEY_PRESS
                                                          : RML_INPUT_KEY_RELEASE,
                                             .key = mapped };
        }
    }
    if (!down) {
        return;
    }
    Control control = control_for_key(key);
    if (control) {
        pressed_controls |= UINT64_C(1) << control;
    }
}
static int payload(void *context, const char *name, render_ui_event_field *fields, int capacity)
{
    desktop_player *host = context;
    source_registry *sources = world_sources(player_world(host->player));
    execution_module_event_info event;
    if (!source_event(sources, name, &event) || event.field_count > capacity) {
        return -1;
    }
    for (int i = 0; i < event.field_count; i++) {
        execution_module_event_field_info field;
        if (!source_event_field(sources, name, i, &field)) {
            return -1;
        }
        fields[i] = (render_ui_event_field){ field.name, field.initial };
    }
    return event.field_count;
}
static int dispatch(void *context, uint64_t owner, const char *name, execution_arguments args)
{
    desktop_player *host = context;
    world *world = player_world(host->player);
    const engine_frame_timing *timing = engine_last_timing(world_engine(world));
    return source_dispatch(
        world_sources(world),
        ecs_entity_from_u64(owner),
        name,
        args.values,
        args.count,
        timing->simulation_time,
        ENGINE_FIXED_SECONDS
    );
}
static bool initializing(void *context)
{
    desktop_player *host = context;
    return entities_initializing(world_entities(player_world(host->player)));
}
static void render(void *context)
{
    desktop_player *host = context;
    world *world = player_world(host->player);
    const engine_modules *modules = world_modules(world);
    app_scene_renderer_set_time(
        host->renderer,
        (float)engine_last_timing(world_engine(world))->simulation_time
    );
    if (engine_viewport_perspective(host->viewport)) {
        render_scene3d_camera camera;
        engine_viewport_get_camera3d(host->viewport, &camera);
        host->render_failed = !app_scene_renderer_draw3d(
            host->renderer,
            world_ecs(world),
            modules->particles,
            &camera,
            host->viewport,
            world_engine(world),
            (render_skip){ 0 }
        );
        return;
    }
    host->render_failed = !app_scene_renderer_draw(
        host->renderer,
        world_ecs(world),
        modules->particles,
        host->viewport,
        world_engine(world),
        (render_skip){ 0 }
    );
}
static int sample_input(desktop_player *host)
{
    engine *runtime = world_engine(player_world(host->player));
    bool active = cf_app_has_focus() && engine_is_playing(runtime) &&
                  !engine_viewport_ui_capture(host->viewport) &&
                  !engine_viewport_ui_typing(host->viewport);
    uint64_t controls = active ? pressed_controls | controls_down() : 0;
    bool firing = false;
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        controllers[i] = controller_state(i);
        if (!active) {
            controllers[i].buttons = 0;
        }
    }
    if (engine_sample_devices(runtime, controls, controllers, &firing) < 0) {
        return -1;
    }
    CF_V2 movement = active ? controls_movement() : (CF_V2){ 0 };
    engine_sample_movement(runtime, movement.x, movement.y);
    CF_V2 aim = active ? controls_aim() : (CF_V2){ 0 };
    bool point = false;
    if (active && !aim.x && !aim.y) {
        point = engine_viewport_mouse_world(host->viewport, &aim.x, &aim.y);
        aim.y = -aim.y;
    }
    CF_Ray3 ray;
    input_pointer_ray pointer = { 0 };
    if (active && engine_viewport_mouse_ray(host->viewport, &ray)) {
        pointer = (input_pointer_ray){ .origin = ray.p, .direction = ray.d, .active = true };
    }
    if (engine_sample_pointer_ray(runtime, pointer) < 0) {
        return -1;
    }
    return engine_sample_aim(runtime, aim, firing, point);
}
static bool text_input(desktop_player *host)
{
    CF_InputTextBuffer buffer;
    if (!cf_input_text_get_buffer(&buffer)) {
        return true;
    }
    if (buffer.len > 4096) {
        return false;
    }
    for (int i = 0; i < buffer.len; i++) {
        unsigned value = (unsigned)buffer.codepoints[i];
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
        char bytes[5] = { 0 };
        int count = 1;
        if (value < 0x80) {
            bytes[0] = (char)value;
        } else if (value < 0x800) {
            count = 2;
            bytes[0] = (char)(0xc0 | (value >> 6));
            bytes[1] = (char)(0x80 | (value & 63));
        } else if (value < 0x10000) {
            count = 3;
            bytes[0] = (char)(0xe0 | (value >> 12));
            bytes[1] = (char)(0x80 | ((value >> 6) & 63));
            bytes[2] = (char)(0x80 | (value & 63));
        } else {
            count = 4;
            bytes[0] = (char)(0xf0 | (value >> 18));
            bytes[1] = (char)(0x80 | ((value >> 12) & 63));
            bytes[2] = (char)(0x80 | ((value >> 6) & 63));
            bytes[3] = (char)(0x80 | (value & 63));
        }
        rml_input input = { .type = RML_INPUT_TEXT_INPUT,
                            .text = bytes,
                            .text_size = (size_t)count };
        engine_viewport_ui_event(host->viewport, &input);
    }
    cf_input_text_clear();
    return true;
}
static bool number(const char *text, double *out)
{
    char *end;
    errno = 0;
    *out = strtod(text, &end);
    return !errno && end != text && !*end && isfinite(*out) && *out > 0;
}
static bool dimensions(const char *text, int *width, int *height)
{
    char *end;
    errno = 0;
    long x = strtol(text, &end, 10);
    if (errno || end == text || *end != 'x' || x < 1 || x > CAPTURE_MAX_DIMENSION) {
        return false;
    }
    const char *second = end + 1;
    long y = strtol(second, &end, 10);
    if (errno || end == second || *end || y < 1 || y > CAPTURE_MAX_DIMENSION ||
        x * y > CAPTURE_MAX_PIXELS) {
        return false;
    }
    *width = (int)x;
    *height = (int)y;
    return true;
}
/* An exported game keeps its artifact and resources in data/ beside the
 * executable, or in Contents/Resources of a macOS app bundle, with the engine's
 * shaders, licenses and identity in engine/ under it. Returns the
 * owned directory, or NULL when this player was not exported with a game. */
static char *bundle_directory()
{
#ifdef __EMSCRIPTEN__
    /* The page fetches the exported data/ folder here before main runs. */
    return xstrdup("/data");
#else
    static const char *const candidates[] = {
        "/data",
#ifdef __APPLE__
        "/../Resources",
#endif
    };
    char *executable = file_executable_dir();
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
        size_t size = strlen(executable) + strlen(candidates[i]) +
                      sizeof "/" BUNDLE_ARTIFACT;
        char *directory = xmalloc(size);
        snprintf(directory, size, "%s%s/" BUNDLE_ARTIFACT, executable, candidates[i]);
        FILE *file = fopen(directory, "rb");
        bool found = file != NULL;
        if (file) {
            fclose(file);
        }
        directory[strlen(executable) + strlen(candidates[i])] = 0;
        if (found) {
            free(executable);
            return directory;
        }
        free(directory);
    }
    free(executable);
    return NULL;
#endif
}
static char *join(const char *directory, const char *name)
{
    size_t size = strlen(directory) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", directory, name);
    return path;
}
/* The icon lives under the resource root, which CF reads through a mount. */
static void set_icon(const char *root, const char *icon)
{
    if (!icon) {
        return;
    }
    if (!root || cf_is_error(cf_fs_mount(root, BUNDLE_MOUNT, true))) {
        fprintf(stderr, "Application icon needs a readable --resource-root: %s\n", icon);
        return;
    }
    char *path = join(BUNDLE_MOUNT, icon);
    cf_app_set_icon(path);
    free(path);
}
static void log_message(void *context, const execution_log_message *message)
{
#ifdef CHIBI_AUTHORING_HOST
    desktop_player *host = context;
    if (host) {
        workspace_host_record_log(
            host->workspace,
            message->now,
            message->program_name,
            message->text
        );
    }
#else
    (void)context;
#endif
    printf(
        "[%s %.6f] %s\n",
        message->program_name ? message->program_name : "source",
        message->now,
        message->text
    );
}
/* Starts the scene scripts asked for during the frame that just drew, or
 * prints why it could not. */
static bool follow_scene(desktop_player *host)
{
    char *next = scene_flow_take(world_scenes(player_world(host->player)));
    if (!next) {
        return true;
    }
    url_requests_cancel(world_urls(player_world(host->player)));
    player_url_submit(NULL);
    char error[512];
    bool loaded = player_load_scene(host->player, next, error, sizeof error);
    free(next);
    if (!loaded) {
        fprintf(stderr, "%s\n", error);
        return false;
    }
    const scene *scene = scene_instance_scene(player_scene(host->player));
    engine_viewport_reset_camera(host->viewport);
    app_scene_renderer_reset(host->renderer);
    engine_viewport_set_perspective(host->viewport, scene_perspective(scene));
    host->ui.page = scene_ui(scene);
    engine_viewport_set_ui(host->viewport, &host->ui, "");
    return true;
}
/* Runs one frame: 1 to continue, 0 when the player quits, and -1 after
 * printing why the game failed. */
static int run_frame_inner(desktop_player *host)
{
    world *world = player_world(host->player);
    engine *runtime = world_engine(world);
    key_count = 0;
    pressed_controls = 0;
    input_overflow = false;
    bool typing = engine_viewport_ui_typing(host->viewport);
#ifdef CHIBI_AUTHORING_HOST
    typing |= workspace_host_text_input_requested(host->workspace);
#endif
    if (typing) {
        cf_input_enable_ime();
    } else {
        cf_input_disable_ime();
    }
    cf_app_update(NULL);
#ifdef CHIBI_AUTHORING_HOST
    if (atomic_exchange(&host->close_requested, false)) {
        workspace_host_close_request(host->workspace);
    }
#endif
    if (host->width && (cf_app_get_canvas_width() != host->width ||
                        cf_app_get_canvas_height() != host->height)) {
        cf_app_set_canvas_size(host->width, host->height);
    }
    if (host->replay &&
        !ui_replay_session_input(host->replay, host->replay_frame, host->viewport)) {
        fprintf(stderr, "%s\n", ui_replay_session_error(host->replay));
        return -1;
    }
    if (!text_input(host)) {
        fprintf(stderr, "Invalid or oversized text input\n");
        return -1;
    }
    if (input_overflow) {
        fprintf(stderr, "Input event limit exceeded\n");
        return -1;
    }
    typing = engine_viewport_ui_typing(host->viewport);
#ifndef CHIBI_AUTHORING_HOST
    if (!typing && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_set_playing(runtime, !engine_is_playing(runtime));
    }
    if (!typing && !engine_is_playing(runtime) && cf_key_just_pressed(CF_KEY_Q)) {
        return 0;
    }
#else
    (void)typing;
#endif
    if (!engine_is_playing(runtime)) {
        url_requests_cancel(world_urls(world));
        player_url_submit(NULL);
    }
    int width = host->width, height = host->height;
    if (!width) {
        cf_app_get_size(&width, &height);
    }
    engine_viewport_request_resize(host->viewport, width, height);
    engine_viewport_host_input input = { .width = width,
                                         .height = height,
                                         .resolution = 1,
                                         .x = cf_mouse_x(),
                                         .y = cf_mouse_y(),
                                         .hovered = cf_app_has_focus(),
                                         .focused = cf_app_has_focus(),
                                         .wheel = cf_mouse_wheel_motion(),
                                         .modifiers = modifiers(),
                                         .keys = keys,
                                         .key_count = key_count };
    for (int i = 0; i < key_count; i++) {
        keys[i].modifiers = input.modifiers;
    }
    for (int i = 0; i < 3; i++) {
        input.down[i] = cf_mouse_down(i);
        input.pressed[i] = cf_mouse_just_pressed(i);
        input.released[i] = cf_mouse_just_released(i);
    }
    if (host->replay) {
        input.focused = true;
        input.hovered = true;
    }
    engine_viewport_set_host_input(host->viewport, &input);
    engine_set_focus(runtime, input.focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_NONE);
#ifdef CHIBI_AUTHORING_HOST
    workspace_host_set_frame_focus(host->workspace, input.focused);
#endif
    if (sample_input(host) < 0 || engine_frame(runtime, host->now) < 0) {
        const char *message = engine_error(runtime);
        fprintf(stderr, "%s\n", message ? message : "Gameplay input rejected");
        return -1;
    }
#ifdef CHIBI_AUTHORING_HOST
    const engine_frame_timing *host_timing = engine_last_timing(runtime);
    workspace_host_record_timing(
        host->workspace,
        (authoring_timing){ .available = true,
                            .time = host->now,
                            .frame_ms = CF_DELTA_TIME * 1000,
                            .behavior_ms = host_timing->behavior_ms,
                            .physics_ms = host_timing->physics_ms,
                            .systems_ms = host_timing->systems_ms,
                            .rendering_ms = host_timing->rendering_ms,
                            .gpu_ms = -1,
                            .steps = host_timing->steps,
                            .backlog = host_timing->event_backlog,
                            .playing = engine_is_playing(runtime) }
    );
    workspace_host_pump(host->workspace, engine_viewport_ui(host->viewport));
    render_ui_set_reference_height(
        engine_viewport_ui(host->viewport),
        height / (float)workspace_host_ui_scale(host->workspace)
    );
    if (workspace_host_quit_requested(host->workspace)) {
        return 0;
    }
#endif
    engine_viewport_view view;
    int camera = camera_view(world_ecs(world), runtime, &view);
    if (camera < 0) {
        fprintf(stderr, "Invalid active camera\n");
        return -1;
    }
    if (camera) {
        engine_viewport_set_view(host->viewport, &view);
    }
    engine_viewport_set_time(host->viewport, host->now);
    if (engine_viewport_draw(
            host->viewport,
            &(engine_viewport_controls){ .playing = engine_is_playing(runtime),
                                         .session = true }
        ) < 0 ||
        host->render_failed) {
        fprintf(stderr, "%s\n", app_scene_renderer_error(host->renderer));
        return -1;
    }
    const char *ui_error = render_ui_error(engine_viewport_ui(host->viewport));
    if (ui_error[0]) {
        fprintf(stderr, "%s\n", ui_error);
        return -1;
    }
    if (host->replay &&
        !ui_replay_session_check(host->replay, host->replay_frame, &host->ui, player_scene(host->player), host->viewport)) {
        fprintf(stderr, "%s\n", ui_replay_session_error(host->replay));
        return -1;
    }
    host->replay_frame++;
    engine_viewport_present(host->viewport);
    cf_app_draw_onto_screen(true);
    if (!follow_scene(host)) {
        return -1;
    }
    char *url;
    if (url_requests_take(world_urls(world), &url)) {
        player_url_submit(url);
        free(url);
    }
    host->now += host->fixed_dt ? host->fixed_dt : CF_DELTA_TIME;
    return 1;
}
#ifdef CHIBI_AUTHORING_HOST
static bool bind_workspace_services(void *context, world *world, char *error, size_t size)
{
    return workspace_host_bind(context, world_sources(world), error, size);
}
/* A project switch starts a fresh authored application against fresh service
 * handles. Preflight both before retiring anything in the current application;
 * no old queued event or UI context can address the replacement sessions. */
static void activate_project(desktop_player *host)
{
    const char *pending = workspace_host_pending_project(host->workspace);
    if (!pending) {
        return;
    }
    char *root = xstrdup(pending);
    char error[1024] = { 0 };
    workspace_host_config config = host->workspace_config;
    config.root = root;
    size_t document_state_bytes = 0;
    unsigned char *document_state = workspace_files_web_document_state(root, &document_state_bytes);
    config.document_state = document_state;
    config.document_state_bytes = document_state_bytes;
    workspace_host *candidate = workspace_host_new(&config, error, sizeof error);
    free(document_state);
    /* Constructor imports owned text; keep no borrowed snapshot in the host's
     * retained config or a later project-switch candidate. */
    config.document_state = NULL;
    config.document_state_bytes = 0;
    player_host loader = config.player;
    loader.services = (player_service_host){ candidate, bind_workspace_services };
    loader.detached_types = true;
    loader.resources = render_prepared_resources_detached(
        config.player.resources.context
    );
    player *application = candidate
                            ? player_open(host->artifact, &loader, host->backend, error, sizeof error)
                            : NULL;
    if (!application) {
        workspace_host_free(candidate);
        workspace_host_project_complete(host->workspace, false, error);
        free(root);
        return;
    }
    world *world = player_world(application);
    declared_components_scope types = { 0 };
    bool types_entered = declared_components_scope_enter(player_declared_components(application), &types);
    assert(types_entered);
    render_resource_scope scope = { 0 };
    bool entered = render_resource_scope_enter(
        render_resource_context_for_world(world),
        &scope,
        error,
        sizeof error
    );
    assert(entered);
    int width = host->width, height = host->height;
    if (!width) {
        cf_app_get_size(&width, &height);
    }
    engine_viewport *viewport = engine_viewport_new(width, height);
    app_scene_renderer *renderer = viewport ? app_scene_renderer_new() : NULL;
    ui_world ui = { .ecs = world_ecs(world),
                    .page = scene_ui(scene_instance_scene(player_scene(application))),
                    .model = world_modules(world)->ui,
                    .save = world_modules(world)->save,
                    .host = { host, payload, dispatch, initializing } };
    bool ready = viewport != NULL;
    if (ready && ui.save && (host->save_path || host->project_name)) {
        const char *directory = NULL;
#ifndef __EMSCRIPTEN__
        if (!host->save_path) {
            directory = cf_fs_get_user_directory("Chibi", "Saves");
        }
#endif
        ready = save_open_project(ui.save, host->project_name, directory, host->save_path);
        if (!ready) {
            snprintf(error, sizeof error, "%s", save_error(ui.save));
        }
    }
    if (!ready) {
        if (viewport) {
            engine_viewport_free(viewport);
        }
        if (renderer) {
            app_scene_renderer_free(renderer);
        }
    }
    bool left = render_resource_scope_leave(&scope, error, sizeof error);
    assert(left);
    bool types_left = declared_components_scope_leave(&types);
    assert(types_left);
    if (!ready) {
        if (!error[0]) {
            snprintf(error, sizeof error, "Cannot create the replacement application viewport");
        }
        player_free(application);
        workspace_host_free(candidate);
        workspace_host_project_complete(host->workspace, false, error);
        free(root);
        return;
    }
    workspace_files_web_adopt(root);
    url_requests_cancel(world_urls(player_world(host->player)));
    player_url_submit(NULL);
    workspace_host_detach_ui(host->workspace);
    engine_viewport_free(host->viewport);
    app_scene_renderer_free(host->renderer);
    player_free(host->player);
    workspace_host_free(host->workspace);
    workspace_files_web_release(host->workspace_root, root);
    free(host->workspace_root);
    host->workspace_root = root;
    host->workspace_config = config;
    host->workspace = candidate;
    host->player = application;
    host->viewport = viewport;
    host->renderer = renderer;
    host->ui = ui;
    host->now = 0;
    host->render_failed = false;
    workspace_host_project_complete(candidate, true, "");
    engine_viewport_adopt_ui(viewport, render_ui_new_prepared(NULL));
    engine_viewport_set_perspective(
        viewport,
        scene_perspective(scene_instance_scene(player_scene(application)))
    );
    engine_viewport_set_runtime(viewport, world_engine(world));
    engine_viewport_set_render_callback(viewport, render, host);
    engine_viewport_set_ui(viewport, &host->ui, "");
    engine_set_playing(world_engine(world), true);
}
#endif
static int run_frame(desktop_player *host)
{
#ifdef CHIBI_AUTHORING_HOST
    char error[512];
    declared_components_scope types = { 0 };
    if (!declared_components_scope_enter(player_declared_components(host->player), &types)) {
        fprintf(stderr, "Cannot enter authored application type scope\n");
        return -1;
    }
    render_resource_scope scope = { 0 };
    if (!render_resource_scope_enter(
            render_resource_context_for_world(player_world(host->player)),
            &scope,
            error,
            sizeof error
        )) {
        fprintf(stderr, "%s\n", error);
        bool types_left = declared_components_scope_leave(&types);
        assert(types_left);
        return -1;
    }
#endif
    int result = run_frame_inner(host);
#ifdef CHIBI_AUTHORING_HOST
    bool left = render_resource_scope_leave(&scope, error, sizeof error);
    assert(left);
    bool types_left = declared_components_scope_leave(&types);
    assert(types_left);
    if (result > 0) {
        activate_project(host);
    }
#endif
    return result;
}
#ifdef __EMSCRIPTEN__
/* Shows the last error the player printed on the page. */
// clang-format off
EM_JS(void, web_stopped, (), {
    Module['chibiStopped']?.();
})
// clang-format on

/* The browser calls this every animation frame. A stopped game stays on its
 * last frame, with the reason shown on the page. */
static void web_frame(void *host)
{
    if (run_frame(host) <= 0) {
        emscripten_cancel_main_loop();
        player_url_stop();
        web_stopped();
    }
}
#endif
int main(int argc, char **argv)
{
    const char *artifact = NULL, *root = NULL, *shader_dir = NULL, *save_path = NULL;
    const char *capture = NULL, *replay_path = NULL, *replay_result = NULL;
#ifdef CHIBI_AUTHORING_HOST
    const char *workspace_root = NULL, *sdk_root = NULL;
#endif
    int width = 0, height = 0;
    bool hidden = false;
    int frames = 0;
    double fixed_dt = 0;
    behavior_backend backend = behavior_default_backend();
    for (int i = 1; i < argc; i++) {
        const char *option = argv[i];
        if (!strcmp(option, "--help")) {
            puts("chibi-player [ARTIFACT] [--resource-root DIR] [--shader-dir DIR] [--save PATH] [--backend vm|sljit|wasm] [--hidden] [--frames N] [--fixed-dt SECONDS] [--size WIDTHxHEIGHT] [--capture PNG]");
            puts("--ui-replay JSON --ui-replay-result JSON requires matching --frames <=60, --fixed-dt and --size <=640x360.");
            puts("--size allows dimensions up to 640 and 230400 pixels; --capture requires --frames and --size.");
#ifdef CHIBI_AUTHORING_HOST
            puts("chibi-author opens without a project; --workspace-root DIR binds authoring services before Begin.");
#endif
            return 0;
        }
        if (!strcmp(option, "--hidden")) {
            hidden = true;
            continue;
        }
        if (option[0] != '-' && !artifact) {
            artifact = option;
            continue;
        }
        if (++i == argc) {
            fprintf(stderr, "Missing value: %s\n", option);
            return 2;
        }
        const char *value = argv[i];
        if (!strcmp(option, "--resource-root")) {
            root = value;
#ifdef CHIBI_AUTHORING_HOST
        } else if (!strcmp(option, "--workspace-root")) {
            if (!*value) {
                fprintf(stderr, "Workspace root is empty\n");
                return 2;
            }
            workspace_root = value;
        } else if (!strcmp(option, "--sdk-resource-root")) {
            sdk_root = value;
#endif
        } else if (!strcmp(option, "--shader-dir")) {
            shader_dir = value;
        } else if (!strcmp(option, "--size")) {
            if (!dimensions(value, &width, &height)) {
                fprintf(stderr, "Invalid canvas size\n");
                return 2;
            }
        } else if (!strcmp(option, "--ui-replay")) {
            replay_path = value;
        } else if (!strcmp(option, "--ui-replay-result")) {
            replay_result = value;
        } else if (!strcmp(option, "--capture")) {
            if (!*value) {
                fprintf(stderr, "Capture path is empty\n");
                return 2;
            }
            capture = value;
        } else if (!strcmp(option, "--save")) {
            save_path = value;
        } else if (!strcmp(option, "--backend") && !strcmp(value, "vm")) {
            backend = BEHAVIOR_BYTECODE;
        } else if (!strcmp(option, "--backend") && !strcmp(value, "sljit")) {
            backend = BEHAVIOR_SLJIT;
        } else if (!strcmp(option, "--backend") && !strcmp(value, "wasm")) {
            backend = BEHAVIOR_WASM;
        } else if (!strcmp(option, "--fixed-dt") && number(value, &fixed_dt) && fixed_dt <= ENGINE_FRAME_SECONDS_MAX) {
        } else if (!strcmp(option, "--frames")) {
            double count;
            if (!number(value, &count) || count > 1000000 || floor(count) != count) {
                fprintf(stderr, "Invalid frame count\n");
                return 2;
            }
            frames = (int)count;
        } else {
            fprintf(stderr, "Invalid option: %s %s\n", option, value);
            return 2;
        }
    }
#ifdef CHIBI_AUTHORING_HOST
    if (sdk_root) {
        resource_configure(sdk_root, sdk_root, sdk_root);
    }
#endif
    if ((replay_path || replay_result) &&
        (!replay_path || !*replay_path || !replay_result || !*replay_result ||
         !frames || frames > UI_REPLAY_FRAMES_MAX || !fixed_dt || !width ||
         width > 640 || height > 360)) {
        fprintf(stderr, "UI replay requires input/result paths, matching <=60frames, fixed dt and <=640x360canvas\n");
        return 2;
    }
#ifdef __EMSCRIPTEN__
    if (replay_path) {
        fprintf(stderr, "CLI UI replay requires the native host\n");
        return 2;
    }
#endif
    if (capture && (!frames || !width)) {
        fprintf(stderr, "Capture requires --frames and --size\n");
        return 2;
    }
#ifdef __EMSCRIPTEN__
    if (capture) {
        fprintf(stderr, "Capture requires the native player\n");
        return 2;
    }
#endif
    char *bundle = artifact ? NULL : bundle_directory();
    char *bundled = bundle ? join(bundle, BUNDLE_ARTIFACT) : NULL;
    char *engine_dir = bundle ? join(bundle, BUNDLE_ENGINE) : NULL;
    if (bundle) {
        resource_configure(engine_dir, engine_dir, engine_dir);
        artifact = bundled;
        root = root ? root : bundle;
    }
    int result = 1;
    char error[1024] = { 0 };
    player_application application = { 0 };
    native_capabilities *native = NULL;
    if (!artifact) {
        fprintf(stderr, "A prepared project artifact is required; use --help.\n");
        result = 2;
        goto release;
    }
    if (!player_application_read(artifact, &application, error, sizeof error)) {
        fprintf(stderr, "%s: %s\n", artifact, error);
        goto release;
    }
    int initial_width = width             ? width
                      : application.width ? application.width
                                          : DEFAULT_WIDTH;
    int initial_height = height             ? height
                       : application.height ? application.height
                                            : DEFAULT_HEIGHT;
#ifdef CHIBI_AUTHORING_HOST
    const native_definition_pack authoring_packs[] = {
        VISUAL_PREVIEW_DEFINITION_PACK,
        AUTHORING_PLATFORM_DEFINITION_PACK,
        UI_DOM_DEFINITION_PACK,
        WORKSPACE_TOOLS_DEFINITION_PACK,
        WORKSPACE_RUNTIME_DEFINITION_PACK,
        WORKSPACE_SIMULATION_DEFINITION_PACK,
        RESOURCE_PREVIEWS_DEFINITION_PACK
    };
    native = authoring_workspace_catalog_new(
        authoring_packs,
        sizeof authoring_packs / sizeof *authoring_packs,
        error,
        sizeof error
    );
#else
    native = host_native_default_catalog_new(
        NULL,
        0,
        &RENDER_SHADER_PARAMETER_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
#endif
    if (!native) {
        fprintf(stderr, "%s\n", error);
        goto release;
    }
    CF_Result made = cf_make_app(
        application.title ? application.title : "Chibi Player",
        0,
        0,
        0,
        initial_width,
        initial_height,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT |
            (application.fullscreen && !width ? CF_APP_OPTIONS_FULLSCREEN_BIT : 0) |
            (hidden ? CF_APP_OPTIONS_HIDDEN_BIT : 0),
        argv[0]
    );
    if (cf_is_error(made)) {
        fprintf(stderr, "%s\n", made.details);
        goto release;
    }
    set_icon(root, application.icon);
    static desktop_player host;
    host.fixed_dt = fixed_dt;
    host.width = width;
    host.height = height;
    if (!shader_dir) {
        shader_dir = resource_shaders();
    }
    CF_Result mounted = cf_fs_mount(shader_dir, "/chibi-render", true);
    if (cf_is_error(mounted)) {
        fprintf(stderr, "%s\n", mounted.details);
        goto finish;
    }
    cf_shader_directory("/chibi-render");
    host.renderer = app_scene_renderer_new();
    host.viewport = engine_viewport_new(initial_width, initial_height);
    if (!host.viewport) {
        fprintf(stderr, "Cannot create a %dx%d window canvas\n", initial_width, initial_height);
        goto finish;
    }
    engine_viewport_adopt_ui(host.viewport, render_ui_new_prepared(NULL));
    host.context = (native_prepared_context){ .native = native };
    host.linker = native_capabilities_prepared_linker(&host.context);
    player_host loader = { .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
                           .linker = &host.linker,
                           .log = { log_message, &host },
                           .resources = render_prepared_resources(root) };
#ifdef CHIBI_AUTHORING_HOST
    loader.decoded_budget = execution_preparation_budget(EXECUTION_PREPARATION_AUTHORED_TOOL);
    host.graphs = authoring_graph_catalog_new(
        (authoring_graph_catalog_config){
            .native = native,
            .types = TYPE_PACK,
            .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT } },
        error,
        sizeof error
    );
    if (!host.graphs) {
        fprintf(stderr, "%s\n", error);
        goto finish;
    }
    int graph_pack_count;
    const registry_pack *const *graph_packs = authoring_graph_catalog_packs(host.graphs, &graph_pack_count);
    registry_set_packs(graph_packs, graph_pack_count);
    initialize_type_packs();
#ifndef __EMSCRIPTEN__
    char *bin = file_executable_dir();
#ifdef _WIN32
    host.compiler = join(bin, "chibi-compile.exe");
    host.launcher = join(bin, "chibi.exe");
#else
    host.compiler = join(bin, "chibi-compile");
    host.launcher = join(bin, "chibi");
#endif
    free(bin);
#endif
    host.artifact = xstrdup(artifact);
    char *requested_root = NULL;
    int requested_version = 0, selected_version = 0;
    if (workspace_root &&
        project_migration_version(workspace_root, &selected_version, error, sizeof error) &&
        selected_version >= CHIBI_PROJECT_OLDEST_VERSION &&
        selected_version < CHIBI_PROJECT_VERSION) {
        rooted_files *selected = rooted_files_open(workspace_root);
        requested_root = selected ? rooted_files_host_path(selected) : NULL;
        if (selected) {
            rooted_files_close(selected);
        }
        if (!requested_root) {
            fprintf(stderr, "Cannot retain the selected older project root\n");
            goto finish;
        }
        requested_version = selected_version;
        workspace_root = NULL;
    }
    host.workspace_root = workspace_root ? xstrdup(workspace_root) : NULL;
    host.backend = backend;
    host.project_name = application.project_name ? xstrdup(application.project_name)
                                                 : NULL;
    host.save_path = save_path ? xstrdup(save_path) : NULL;
    host.workspace_config = (workspace_host_config){ .root = host.workspace_root,
                                                     .requested_root = requested_root,
                                                     .requested_version = requested_version,
                                                     .native = native,
                                                     .compiler = host.compiler,
                                                     .launcher = host.launcher,
                                                     .window = cf_app_get_window(),
                                                     .player = loader,
                                                     .backend = backend };
    host.workspace = workspace_host_new(&host.workspace_config, error, sizeof error);
    free(requested_root);
    host.workspace_config.requested_root = NULL;
    host.workspace_config.requested_version = 0;
    if (!host.workspace) {
        fprintf(stderr, "%s\n", error);
        goto finish;
    }
    /* Keep workspace_config.player capability-clean for target games. Only
     * this application loader receives the workspace service binder. */
    loader.services = (player_service_host){ host.workspace, bind_workspace_services };
#endif
    host.player = player_open(artifact, &loader, backend, error, sizeof error);
    if (!host.player) {
        fprintf(stderr, "%s\n", error);
        goto finish;
    }
    world *world = player_world(host.player);
    if (!hidden && !frames) {
        url_requests_enable(world_urls(world), true);
        player_url_start();
    }
    engine *runtime = world_engine(world);
    const scene *scene = scene_instance_scene(player_scene(host.player));
    engine_viewport_set_perspective(host.viewport, scene_perspective(scene));
    engine_viewport_set_runtime(host.viewport, runtime);
    engine_viewport_set_render_callback(host.viewport, render, &host);
    host.ui = (ui_world){ .ecs = world_ecs(world),
                          .page = scene_ui(scene),
                          .model = world_modules(world)->ui,
                          .save = world_modules(world)->save,
                          .host = { &host, payload, dispatch, initializing } };
    engine_viewport_set_ui(host.viewport, &host.ui, "");
    if (save_path && !host.ui.save) {
        fprintf(stderr, "Save service is unavailable\n");
        goto finish;
    }
    if (host.ui.save && (save_path || application.project_name)) {
        const char *directory = NULL;
#ifndef __EMSCRIPTEN__
        if (!save_path) {
            directory = cf_fs_get_user_directory("Chibi", "Saves");
        }
#endif
        /* Initial Begin handlers are queued until the first engine frame. */
        if (!save_open_project(host.ui.save, application.project_name, directory, save_path)) {
            fprintf(stderr, "%s\n", save_error(host.ui.save));
            goto finish;
        }
    }
    if (replay_path) {
        size_t bytes;
        char *text = ui_replay_read_file(replay_path, UI_REPLAY_BYTES_MAX, &bytes);
        ui_replay *replay = text ? ui_replay_parse(text, bytes, error, sizeof error)
                                 : NULL;
        free(text);
        if (!replay || replay->frames != frames) {
            fprintf(
                stderr,
                "%s\n",
                replay     ? "Replay frames differ from --frames"
                : error[0] ? error
                           : "Cannot read bounded replay input"
            );
            ui_replay_free(replay);
            goto finish;
        }
        host.replay = ui_replay_session_new(replay, artifact, error, sizeof error);
        if (!host.replay) {
            ui_replay_free(replay);
            fprintf(stderr, "%s\n", error);
            goto finish;
        }
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    cf_register_key_callback(key_changed);
#ifdef CHIBI_AUTHORING_HOST
    SDL_GetEventFilter(&host.previous_filter, &host.previous_filter_context);
    host.window_id = SDL_GetWindowID(cf_app_get_window());
    SDL_SetEventFilter(authoring_event_filter, &host);
    host.filtering = true;
#endif
    player_clock_start();
    engine_set_playing(runtime, true);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(web_frame, &host, 0, true);
#endif
    int status = 1, frame = 0;
    while (status > 0 && !stopped && cf_app_is_running() &&
           (!frames || frame < frames)) {
        status = run_frame(&host);
        frame += status > 0;
    }
    if (status < 0) {
        goto finish;
    }
    if (capture && (frame != frames || app_save_screenshot(capture))) {
        fprintf(stderr, "Final frame capture failed\n");
        goto finish;
    }
    if (capture) {
        printf(
            "capture backend=%s size=%dx%d hidden=%s\n",
            cf_backend_type_to_string(cf_query_backend()),
            cf_app_get_canvas_width(),
            cf_app_get_canvas_height(),
            hidden ? "true" : "false"
        );
    }
    runtime = world_engine(player_world(host.player));
    printf(
        "frames=%d steps=%llu\n",
        frame,
        (unsigned long long)engine_last_timing(runtime)->simulation_steps
    );
    result = 0;
finish:
    if (host.replay) {
        if (!ui_replay_session_finished(host.replay)) {
            result = 1;
        }
        if (!ui_replay_session_write(
                host.replay,
                replay_result,
                result == 0 ? capture : NULL,
                result == 0,
                cf_backend_type_to_string(cf_query_backend()),
                behavior_backend_name(backend),
                width,
                height,
                fixed_dt
            )) {
            fprintf(stderr, "Could not write bounded UI replay result\n");
            result = 1;
        }
        ui_replay_session_free(host.replay);
        host.replay = NULL;
    }
    player_url_stop();
    cf_register_key_callback(NULL);
#ifdef CHIBI_AUTHORING_HOST
    if (host.filtering) {
        SDL_SetEventFilter(host.previous_filter, host.previous_filter_context);
    }
    workspace_host_detach_ui(host.workspace);
#endif
    if (host.viewport) {
        engine_viewport_free(host.viewport);
    }
    if (host.renderer) {
        app_scene_renderer_free(host.renderer);
    }
    if (host.player) {
        player_free(host.player);
    }
#ifdef CHIBI_AUTHORING_HOST
    workspace_host_free(host.workspace);
    workspace_files_web_release(host.workspace_root, NULL);
    if (host.graphs) {
        afree(host_type_packs);
        registry_set_packs(NULL, 0);
        authoring_graph_catalog_free(host.graphs);
    }
    free(host.artifact);
    free(host.workspace_root);
    free(host.compiler);
    free(host.launcher);
    free(host.project_name);
    free(host.save_path);
#endif
    rml_backend_release_prepared_fonts();
    render_images_clear();
    cf_destroy_app();
release:
    native_capabilities_free(native);
    player_application_free(&application);
    free(engine_dir);
    free(bundled);
    free(bundle);
#ifdef __EMSCRIPTEN__
    if (result) {
        web_stopped();
    }
#endif
    return result;
}
