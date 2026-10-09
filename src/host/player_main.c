#include "execution/backend.h"
#include "host/native_default.h"
#include "platform/cute.h"
#include "render/scene3d.h"
#include "render/scene_renderer.h"
#include "render/sdf_motion.h"
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
    return GAME_TYPE_PACK;
}
typedef struct
{
    game_player *player;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    bool render_failed;
} player_host;
#define BUNDLE_ARTIFACT "game.chibip"
#define BUNDLE_ENGINE "engine"
#define BUNDLE_MOUNT "/chibi-game"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
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
    int control = GAME_CONTROL_NONE;
    if (key >= CF_KEY_A && key <= CF_KEY_Z) {
        control = GAME_CONTROL_A + key - CF_KEY_A;
    } else {
        switch (key) {
            case CF_KEY_SPACE: control = GAME_CONTROL_SPACE; break;
            case CF_KEY_TAB: control = GAME_CONTROL_TAB; break;
            case CF_KEY_RETURN: control = GAME_CONTROL_ENTER; break;
            case CF_KEY_LSHIFT:
            case CF_KEY_RSHIFT: control = GAME_CONTROL_SHIFT; break;
            default: break;
        }
    }
    if (control) {
        pressed_controls |= UINT64_C(1) << control;
    }
}
static bool control_down(GameControl control)
{
    if (control >= GAME_CONTROL_A && control <= GAME_CONTROL_Z) {
        return cf_key_down(CF_KEY_A + control - GAME_CONTROL_A);
    }
    if (control >= GAME_CONTROL_PAD_A && control <= GAME_CONTROL_PAD_Y) {
        return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_A + control - GAME_CONTROL_PAD_A);
    }
    switch (control) {
        case GAME_CONTROL_MOUSE_LEFT:
            return cf_mouse_down(CF_MOUSE_BUTTON_LEFT) ||
                   cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT);
        case GAME_CONTROL_MOUSE_RIGHT:
            return cf_mouse_down(CF_MOUSE_BUTTON_RIGHT) ||
                   cf_mouse_just_pressed(CF_MOUSE_BUTTON_RIGHT);
        case GAME_CONTROL_SPACE: return cf_key_down(CF_KEY_SPACE);
        case GAME_CONTROL_SHIFT: return cf_key_shift();
        case GAME_CONTROL_TAB: return cf_key_down(CF_KEY_TAB);
        case GAME_CONTROL_ENTER: return cf_key_down(CF_KEY_RETURN);
        case GAME_CONTROL_PAD_LEFT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_LEFTSHOULDER);
        case GAME_CONTROL_PAD_RIGHT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_RIGHTSHOULDER);
        case GAME_CONTROL_PAD_LEFT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERLEFT) > 8192;
        case GAME_CONTROL_PAD_RIGHT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERRIGHT) > 8192;
        case GAME_CONTROL_AIM_STICK:
            return abs(cf_joypad_axis(0, CF_JOYPAD_AXIS_RIGHTX)) > 8192 ||
                   abs(cf_joypad_axis(0, CF_JOYPAD_AXIS_RIGHTY)) > 8192;
        default: return false;
    }
}
static CF_V2 stick(CF_JoypadAxis x, CF_JoypadAxis y)
{
    CF_V2 v = { cf_joypad_axis(0, x) / 32768.0f, cf_joypad_axis(0, y) / 32768.0f };
    float length = sqrtf(v.x * v.x + v.y * v.y);
    if (length <= .25f) {
        return (CF_V2){ 0 };
    }
    return (CF_V2){ v.x / length, v.y / length };
}
static int payload(void *context, const char *name, render_ui_event_field *fields, int capacity)
{
    player_host *host = context;
    game_source_registry *sources = game_world_sources(game_player_world(host->player));
    execution_module_event_info event;
    if (!game_source_event(sources, name, &event) || event.field_count > capacity) {
        return -1;
    }
    for (int i = 0; i < event.field_count; i++) {
        execution_module_event_field_info field;
        if (!game_source_event_field(sources, name, i, &field)) {
            return -1;
        }
        fields[i] = (render_ui_event_field){ field.name, field.initial };
    }
    return event.field_count;
}
static int dispatch(void *context, uint64_t owner, const char *name, execution_arguments args)
{
    player_host *host = context;
    game_world *world = game_player_world(host->player);
    const engine_frame_timing *timing = engine_last_timing(game_world_engine(world));
    return game_source_dispatch(
        game_world_sources(world),
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
    player_host *host = context;
    return game_entities_initializing(game_world_entities(game_player_world(host->player)));
}
static void render(void *context)
{
    player_host *host = context;
    game_world *world = game_player_world(host->player);
    const engine_modules *modules = game_world_modules(world);
    if (engine_viewport_perspective(host->viewport)) {
        render_scene3d_camera camera;
        engine_viewport_get_camera3d(host->viewport, &camera);
        host->render_failed = !app_scene_renderer_draw3d(
            host->renderer,
            game_world_ecs(world),
            modules->particles,
            &camera,
            host->viewport,
            game_world_engine(world),
            (render_skip){ 0 }
        );
        return;
    }
    host->render_failed = !app_scene_renderer_draw(
        host->renderer,
        game_world_ecs(world),
        modules->particles,
        host->viewport,
        game_world_engine(world),
        (render_skip){ 0 }
    );
}
static int sample_input(player_host *host)
{
    engine *runtime = game_world_engine(game_player_world(host->player));
    bool active = cf_app_has_focus() && engine_is_playing(runtime) &&
                  !engine_viewport_ui_capture(host->viewport) &&
                  !engine_viewport_ui_typing(host->viewport);
    uint64_t controls = 0;
    if (active) {
        controls = pressed_controls;
        for (int i = 1; i < GAME_CONTROL_COUNT; i++) {
            if (control_down(i)) {
                controls |= UINT64_C(1) << i;
            }
        }
    }
    bool firing = false;
    if (engine_sample_controls(runtime, controls, &firing) < 0) {
        return -1;
    }
    CF_V2 movement = active ? stick(CF_JOYPAD_AXIS_LEFTX, CF_JOYPAD_AXIS_LEFTY)
                            : (CF_V2){ 0 };
    if (active && !movement.x && !movement.y) {
        movement.x = cf_key_down(CF_KEY_D) - cf_key_down(CF_KEY_A);
        movement.y = cf_key_down(CF_KEY_S) - cf_key_down(CF_KEY_W);
        float length = sqrtf(movement.x * movement.x + movement.y * movement.y);
        if (length > 1) {
            movement.x /= length;
            movement.y /= length;
        }
    }
    engine_sample_movement(runtime, movement.x, movement.y);
    CF_V2 aim = active ? stick(CF_JOYPAD_AXIS_RIGHTX, CF_JOYPAD_AXIS_RIGHTY)
                       : (CF_V2){ 0 };
    bool point = false;
    if (active && !aim.x && !aim.y) {
        point = engine_viewport_mouse_world(host->viewport, &aim.x, &aim.y);
        aim.y = -aim.y;
    }
    return engine_sample_aim(runtime, aim, firing, point);
}
static bool text_input(player_host *host)
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
    game_player_application application = { 0 };
    native_capabilities *native = NULL;
    if (!artifact) {
        fprintf(stderr, "A prepared project artifact is required; use --help.\n");
        result = 2;
        goto release;
    }
    if (!game_player_application_read(artifact, &application, error, sizeof error)) {
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
    player_host host = { 0 };
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
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    game_player_host loader = { .components = { GAME_REGISTRY_COMPONENTS,
                                                GAME_REGISTRY_COMPONENT_COUNT },
                                .linker = &linker,
                                .log = { log_message, NULL },
                                .sdf_motion = game_sdf_motion_tick,
                                .resources = render_prepared_resources(root) };
    host.player = game_player_open(artifact, &loader, backend, error, sizeof error);
    if (!host.player) {
        fprintf(stderr, "%s\n", error);
        goto finish;
    }
    game_world *world = game_player_world(host.player);
    engine *runtime = game_world_engine(world);
    const game_scene *scene = game_scene_instance_scene(game_player_scene(host.player));
    engine_viewport_set_perspective(host.viewport, game_scene_perspective(scene));
    engine_viewport_set_runtime(host.viewport, runtime);
    engine_viewport_set_render_callback(host.viewport, render, &host);
    ui_world ui = { .ecs = game_world_ecs(world),
                    .page = game_scene_ui(scene),
                    .model = game_world_modules(world)->ui,
                    .save = game_world_modules(world)->save,
                    .host = { &host, payload, dispatch, initializing } };
    engine_viewport_set_ui(host.viewport, &ui, "");
    if (save_path && !ui.save) {
        fprintf(stderr, "Save service is unavailable\n");
        goto finish;
    }
    if (save_path && !game_save_open(ui.save, save_path)) {
        fprintf(stderr, "%s\n", game_save_error(ui.save));
        goto finish;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    cf_register_key_callback(key_changed);
    cf_set_target_framerate(60);
    engine_set_playing(runtime, true);
    double now = 0;
    int frame = 0;
    while (!stopped && cf_app_is_running() && (!frames || frame < frames)) {
        key_count = 0;
        pressed_controls = 0;
        input_overflow = false;
        if (engine_viewport_ui_typing(host.viewport)) {
            cf_input_enable_ime();
        } else {
            cf_input_disable_ime();
        }
        cf_app_update(NULL);
        if (!text_input(&host)) {
            fprintf(stderr, "Invalid or oversized text input\n");
            goto finish;
        }
        if (input_overflow) {
            fprintf(stderr, "Input event limit exceeded\n");
            goto finish;
        }
        bool typing = engine_viewport_ui_typing(host.viewport);
        if (!typing && cf_key_just_pressed(CF_KEY_ESCAPE)) {
            engine_set_playing(runtime, !engine_is_playing(runtime));
        }
        if (!typing && !engine_is_playing(runtime) && cf_key_just_pressed(CF_KEY_Q)) {
            break;
        }
        int width, height;
        cf_app_get_size(&width, &height);
        engine_viewport_request_resize(host.viewport, width, height);
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
        engine_viewport_set_host_input(host.viewport, &input);
        engine_set_focus(runtime, input.focused ? ENGINE_FOCUS_VIEWPORT : ENGINE_FOCUS_NONE);
        if (sample_input(&host) < 0 || engine_frame(runtime, now) < 0) {
            const char *message = engine_error(runtime);
            fprintf(stderr, "%s\n", message ? message : "Gameplay input rejected");
            goto finish;
        }
        engine_viewport_view view;
        int camera = game_camera_view(game_world_ecs(world), runtime, &view);
        if (camera < 0) {
            fprintf(stderr, "Invalid active camera\n");
            goto finish;
        }
        if (camera) {
            engine_viewport_set_view(host.viewport, &view);
        }
        engine_viewport_set_time(host.viewport, now);
        if (engine_viewport_draw(
                host.viewport,
                &(engine_viewport_controls){ .playing = engine_is_playing(runtime),
                                             .session = true }
            ) < 0 ||
            host.render_failed) {
            fprintf(stderr, "%s\n", app_scene_renderer_error(host.renderer));
            goto finish;
        }
        const char *ui_error = render_ui_error(engine_viewport_ui(host.viewport));
        if (ui_error[0]) {
            fprintf(stderr, "%s\n", ui_error);
            goto finish;
        }
        engine_viewport_present(host.viewport);
        cf_app_draw_onto_screen(true);
        now += fixed_dt ? fixed_dt : CF_DELTA_TIME;
        frame++;
    }
    printf(
        "frames=%d steps=%llu\n",
        frame,
        (unsigned long long)engine_last_timing(runtime)->simulation_steps
    );
    result = 0;
finish:
    cf_register_key_callback(NULL);
    if (host.viewport) {
        engine_viewport_free(host.viewport);
    }
    if (host.renderer) {
        app_scene_renderer_free(host.renderer);
    }
    if (host.player) {
        game_player_free(host.player);
    }
    rml_backend_release_prepared_fonts();
    render_images_clear();
    cf_destroy_app();
release:
    native_capabilities_free(native);
    game_player_application_free(&application);
    free(engine_dir);
    free(bundled);
    free(bundle);
    return result;
}
