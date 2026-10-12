#include "execution/backend.h"
#include "host/native_default.h"
#include "host/player_device.h"
#include "host/player_url.h"
#include "platform/cute.h"
#include "render/device_controls.h"
#include "render/scene3d.h"
#include "render/scene_renderer.h"
#include "render/shader_images.h"
#include "render/shader_parameters.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "runtime/player_prepared.h"
#include "support/file_io.h"
#include "support/resources.h"
#include "support/xalloc.h"
#include "types/catalog.h"

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
int type_catalog_pack_count()
{
    return 1;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    if (index != 0) {
        abort();
    }
    return TYPE_PACK;
}
typedef struct
{
    player *player;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    bool render_failed;
    /* A browser keeps calling frame() after main returns, so everything a
     * frame reads lives here rather than on main's stack. */
    native_prepared_context context;
    execution_prepared_linker linker;
    ui_world ui;
    double now;
    double fixed_dt;
} desktop_player;
#define BUNDLE_ARTIFACT "game.chibip"
#define BUNDLE_ENGINE "engine"
#define BUNDLE_MOUNT "/chibi-game"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define PROJECT_DECODED_BUDGET (128u * 1024u * 1024u)
static volatile sig_atomic_t stopped;
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
    switch (key) {
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
static int sample_input(desktop_player *host, CF_V2 pointer_position)
{
    engine *runtime = world_engine(player_world(host->player));
    int width, height;
    cf_app_get_size(&width, &height);
    if (engine_sample_device_state(runtime, player_device_sample(width, height, cf_app_has_focus())) <
        0) {
        return -1;
    }
    bool active = cf_app_has_focus() && engine_is_playing(runtime) &&
                  !engine_viewport_ui_capture(host->viewport) &&
                  !engine_viewport_ui_typing(host->viewport);
    if (!active) {
        controls_pointer_consumed();
    }
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
    if (active && engine_viewport_ray_at(host->viewport, pointer_position, &ray)) {
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
static void log_message(void *, const execution_log_message *message)
{
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
static int run_frame(desktop_player *host)
{
    world *world = player_world(host->player);
    engine *runtime = world_engine(world);
    key_count = 0;
    pressed_controls = 0;
    input_overflow = false;
    if (engine_viewport_ui_typing(host->viewport)) {
        cf_input_enable_ime();
    } else {
        cf_input_disable_ime();
    }
    cf_app_update(NULL);
    device_pointer_sample pointer = controls_pointer_take((CF_V2){ cf_mouse_x(),
                                                                   cf_mouse_y() });
    for (int i = 0; i < 3; i++) {
        if (pointer.pressed[i]) {
            pressed_controls |= UINT64_C(1) << control_for_mouse(i);
        }
    }
    if (!text_input(host)) {
        fprintf(stderr, "Invalid or oversized text input\n");
        return -1;
    }
    if (input_overflow) {
        fprintf(stderr, "Input event limit exceeded\n");
        return -1;
    }
    bool typing = engine_viewport_ui_typing(host->viewport);
    if (!typing && cf_key_just_pressed(CF_KEY_ESCAPE)) {
        engine_set_playing(runtime, !engine_is_playing(runtime));
    }
    if (!typing && !engine_is_playing(runtime) && cf_key_just_pressed(CF_KEY_Q)) {
        return 0;
    }
    if (!engine_is_playing(runtime)) {
        url_requests_cancel(world_urls(world));
        player_url_submit(NULL);
    }
    int width, height;
    cf_app_get_size(&width, &height);
    engine_viewport_host_input input = { .width = width,
                                         .height = height,
                                         .resolution = cf_app_get_pixel_scale(),
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
    engine_viewport_set_host_input(host->viewport, &input);
    engine_set_focus(runtime, input.focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_NONE);
    uint64_t steps = engine_last_timing(runtime)->simulation_steps;
    if (sample_input(host, pointer.position) < 0 ||
        engine_frame(runtime, host->now) < 0) {
        const char *message = engine_error(runtime);
        fprintf(stderr, "%s\n", message ? message : "Gameplay input rejected");
        return -1;
    }
    if (engine_last_timing(runtime)->simulation_steps != steps ||
        !engine_is_playing(runtime)) {
        controls_pointer_consumed();
    }
    player_device_submit(engine_take_device_requests(runtime), cf_app_has_focus());
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
    if (engine_viewport_present(host->viewport)) {
        cf_app_draw_onto_screen(true);
    }
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
        player_device_stop();
        controls_pointer_stop();
        web_stopped();
    }
}
#endif
int main(int argc, char **argv)
{
    const char *artifact = NULL, *root = NULL, *shader_dir = NULL, *save_path = NULL;
    bool hidden = false;
    int frames = 0;
    double fixed_dt = 0;
    behavior_backend backend = behavior_default_backend();
    for (int i = 1; i < argc; i++) {
        const char *option = argv[i];
        if (!strcmp(option, "--help")) {
            puts("chibi-player [ARTIFACT] [--resource-root DIR] [--shader-dir DIR] [--save PATH] [--backend vm|sljit] [--hidden] [--frames N] [--fixed-dt SECONDS]");
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
        } else if (!strcmp(option, "--shader-dir")) {
            shader_dir = value;
        } else if (!strcmp(option, "--save")) {
            save_path = value;
        } else if (!strcmp(option, "--backend") && !strcmp(value, "vm")) {
            backend = BEHAVIOR_BYTECODE;
        } else if (!strcmp(option, "--backend") && !strcmp(value, "sljit")) {
            backend = BEHAVIOR_SLJIT;
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
    int initial_width = application.width ? application.width : DEFAULT_WIDTH;
    int initial_height = application.height ? application.height : DEFAULT_HEIGHT;
    native = host_native_default_catalog_new(
        NULL,
        0,
        &RENDER_SHADER_PARAMETER_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
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
            (application.fullscreen ? CF_APP_OPTIONS_FULLSCREEN_BIT : 0) |
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
                           .log = { log_message, NULL },
                           .resources = render_prepared_resources(root),
                           .decoded_budget = PROJECT_DECODED_BUDGET };
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
    player_device_start();
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
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    cf_register_key_callback(key_changed);
    if (!controls_pointer_start()) {
        fprintf(stderr, "Cannot watch pointer input\n");
        goto finish;
    }
    cf_set_target_framerate(60);
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
    printf(
        "frames=%d steps=%llu\n",
        frame,
        (unsigned long long)engine_last_timing(runtime)->simulation_steps
    );
    result = 0;
finish:
    player_url_stop();
    player_device_stop();
    controls_pointer_stop();
    cf_register_key_callback(NULL);
    if (host.viewport) {
        engine_viewport_free(host.viewport);
    }
    if (host.renderer) {
        app_scene_renderer_free(host.renderer);
    }
    if (host.player) {
        player_free(host.player);
    }
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
