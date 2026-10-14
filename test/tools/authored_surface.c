#include "assets/ui_source.h"
#include "assets/ui_texture.h"
#include "render/ui.h"
#include "runtime/component_catalog.h"
#include "runtime/ui/documents.h"
#include "runtime/ui_chord.h"
#include "runtime/ui_native.h"
#include "third_party/pico_headers/pico_unit.h"
#include "ui/controls/controls.h"
#include "ui/input_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(authored_surface_geometry_expires_and_updates)
{
    ui *model = ui_new();
    pin_value arguments[] = { { .u64 = 17 }, { .str = "#grid" } };
    pin_value results[3] = { 0 };
    REQUIRE(!UI_GET_VIEW_WIDTH_CALL.invoke(model, arguments, results));
    REQUIRE(!results[1].b && results[0].f == 0 && results[2].f == 0);
    ui_measure_width(model, 17, "#grid", 0);
    REQUIRE(!UI_GET_VIEW_WIDTH_CALL.invoke(model, arguments, results));
    REQUIRE(results[1].b && results[0].f == 0);
    ui_measure_width(model, 17, "#grid", 420);
    ui_measure_pixel_ratio(model, 17, "#grid", 2);
    ui_measure_width(model, 18, "#grid", 99);
    REQUIRE(!UI_GET_VIEW_WIDTH_CALL.invoke(model, arguments, results));
    REQUIRE(results[1].b && results[0].f == 420 && results[2].f == 2);
    ui_measure_pixel_ratio(model, 17, "#grid", 1.25f);
    REQUIRE(!UI_GET_VIEW_WIDTH_CALL.invoke(model, arguments, results));
    REQUIRE(results[2].f == 1.25f && results[0].f == 420);
    ui_unmeasure(model, 17);
    REQUIRE(!UI_GET_VIEW_WIDTH_CALL.invoke(model, arguments, results));
    REQUIRE(!results[1].b && results[0].f == 0 && results[2].f == 0);
    float width;
    REQUIRE(ui_view_width(model, 18, "#grid", &width) && width == 99);
    ui_measure_width(model, 17, "#grid", 700);
    ui_forget(model, 17);
    REQUIRE(!ui_view_width(model, 17, "#grid", &width) && width == 0);
    ui_free(model);
    return true;
}

typedef struct
{
    bool called, valid, canceled;
    bool unsupported, explicit_controls;
    const char *expected_text;
} surface_observer;

static int surface_payload(void *context, const char *event, render_ui_event_field *fields, int capacity)
{
    surface_observer *observer = context;
    (void)event;
    if (!strcmp(event, "OwnedText")) {
        fields[0] = (render_ui_event_field){ "Text", { .type = PIN_STR } };
        fields[1] = (render_ui_event_field){ "Extra", { .type = PIN_I32 } };
        return 2;
    }
    if (capacity < 11) {
        return -1;
    }
    const char *vectors[] = { "Pointer", "LocalPointer", "Delta", "Wheel" };
    for (int i = 0; i < 4; i++) {
        fields[i] = (render_ui_event_field){ vectors[i],
                                             { .type = PIN_STRUCT,
                                               .type_name = "CF_V2" } };
    }
    fields[4] = (render_ui_event_field){ "Button", { .type = PIN_I32 } };
    fields[5] = (render_ui_event_field){ "Canceled", { .type = PIN_BOOL } };
    fields[6] = (render_ui_event_field){ "Value", { .type = PIN_F32 } };
    fields[7] = (render_ui_event_field){ "Text", { .type = PIN_STR } };
    fields[8] = (render_ui_event_field){ "Shift", { .type = PIN_BOOL } };
    fields[9] = (render_ui_event_field){ "Extra", { .type = PIN_I32 } };
    if (observer->unsupported) {
        fields[0].type.container = PIN_CONTAINER_ARRAY;
    }
    fields[10] = (render_ui_event_field){ "Modifiers", { .type = PIN_I32 } };
    return 11;
}

static int surface_dispatch(void *context, uint64_t entity, const char *event, execution_arguments arguments)
{
    surface_observer *observer = context;
    observer->called = true;
    if (!strcmp(event, "OwnedText")) {
        observer->valid = arguments.count == 2 &&
                          !strcmp(arguments.values[0].str, observer->expected_text) &&
                          arguments.values[1].i == 73;
        return 0;
    }
    observer->valid = entity == 17 && !strcmp(event, "Surface") &&
                      arguments.count == 11;
    const float expected[][2] = { { 100, 200 }, { 10, 20 }, { -2, 3 }, { 0.5f, -1 } };
    const pin_value *values = arguments.values;
    for (int i = 0; i < 4; i++) {
        observer->valid &= !memcmp(values[i].ptr, expected[i], sizeof expected[i]);
        for (int j = 0; j < i; j++) {
            observer->valid &= values[i].ptr != values[j].ptr;
        }
    }
    observer->valid &= values[4].i == -1 && values[5].b == observer->canceled &&
                       values[6].f == (observer->explicit_controls ? 0.75f : 0.25f) &&
                       !strcmp(values[7].str, observer->explicit_controls ? "edited" : "") &&
                       values[8].b && values[9].i == 73 &&
                       values[10].i ==
                           (RML_KM_SHIFT | RML_KM_CTRL | RML_KM_META | RML_KM_ALT);
    return 0;
}

static int seven_payload(void *context, const char *event, render_ui_event_field *fields, int capacity)
{
    (void)context;
    (void)event;
    if (capacity < 7) {
        return -1;
    }
    const char *names[] = { "Key",  "Control", "Shift", "Alt",
                            "Meta", "Caret",   "Buffer" };
    for (int i = 0; i < 7; i++) {
        fields[i] = (render_ui_event_field){ names[i],
                                             { .type = i == 0 || i == 6 ? PIN_STR
                                                                        : PIN_I32 } };
    }
    return 7;
}
static int seven_dispatch(void *context, uint64_t entity, const char *event, execution_arguments arguments)
{
    surface_observer *observer = context;
    observer->called = true;
    const pin_value *v = arguments.values;
    observer->valid = entity == 17 && !strcmp(event, "OwnedSeven") &&
                      arguments.count == 7 && !strcmp(v[0].str, "F12") &&
                      v[1].i == 1 && v[2].i == 0 && v[3].i == 1 && v[4].i == 0 &&
                      v[5].i == 134 && !strcmp(v[6].str, observer->expected_text);
    return 0;
}
TEST_CASE(authored_surface_complete_scalar_key_tuple_owns_unicode_buffer)
{
    char buffer[301];
    memset(buffer, 'x', sizeof buffer - 1);
    memcpy(buffer + 127, "雪🎮", 7);
    buffer[sizeof buffer - 1] = 0;
    char *expected = strdup(buffer);
    REQUIRE(expected);
    ui_activation activation = { 0 };
    const char *args[] = { "F12", "1", "0", "1", "0", "134", buffer };
    REQUIRE(ui_activation_capture(&activation, 7, args, NULL));
    /* A control supplies an explicit dictionary; DOM Key must not shift it. */
    snprintf(activation.key, sizeof activation.key, "Escape");
    snprintf(activation.event, sizeof activation.event, "OwnedSeven");
    memset(buffer, 'y', sizeof buffer - 1);
    surface_observer observer = { .expected_text = expected };
    render_ui_host host = { .context = &observer,
                            .payload = seven_payload,
                            .dispatch = seven_dispatch };
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &activation, RML_KM_META));
    REQUIRE(observer.called && observer.valid);
    REQUIRE(!strcmp(ui_activation_argument(&activation, 6), expected));
    const char *boundary[17];
    for (int i = 0; i < 17; i++) {
        boundary[i] = "1";
    }
    REQUIRE(ui_activation_capture(&activation, 16, boundary, NULL));
    REQUIRE(activation.argc == 16);
    REQUIRE(!ui_activation_capture(&activation, 17, boundary, NULL));
    REQUIRE(activation.argc == 16);
    ui_activation_clear(&activation);
    free(expected);
    return true;
}

TEST_CASE(authored_surface_vectors_control_cancel_and_arguments_are_independent)
{
    surface_observer observer = { 0 };
    render_ui_host host = { .context = &observer,
                            .payload = surface_payload,
                            .dispatch = surface_dispatch };
    ui_activation activation = { .event = "Surface",
                                 .argc = 1,
                                 .args = { "73" },
                                 .x = 100,
                                 .y = 200,
                                 .local_x = 10,
                                 .local_y = 20,
                                 .delta_x = -2,
                                 .delta_y = 3,
                                 .wheel_x = 0.5f,
                                 .wheel_y = -1,
                                 .button = -1,
                                 .modifiers = RML_KM_SHIFT | RML_KM_CTRL |
                                              RML_KM_META | RML_KM_ALT,
                                 .has_value = true,
                                 .value = 0.25f,
                                 .has_text = true };
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &activation, RML_KM_CTRL));
    REQUIRE(observer.called && observer.valid);
    observer.called = false;
    observer.canceled = activation.canceled = true;
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &activation, RML_KM_CTRL));
    REQUIRE(observer.called && observer.valid);
    observer.called = false;
    observer.explicit_controls = true;
    activation.argc = 3;
    snprintf(activation.args[0], sizeof activation.args[0], "0.75");
    snprintf(activation.args[1], sizeof activation.args[1], "edited");
    snprintf(activation.args[2], sizeof activation.args[2], "73");
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &activation, RML_KM_CTRL));
    REQUIRE(observer.called && observer.valid);
    observer.called = false;
    observer.unsupported = true;
    REQUIRE(!ui_activation_send(&host, ecs_entity_from_u64(17), &activation, RML_KM_CTRL));
    REQUIRE(!observer.called);
    return true;
}

TEST_CASE(authored_surface_keyboard_chords_are_exact_and_not_truncated)
{
    char key[4];
    REQUIRE(!strcmp(ui_key_name(RML_KEY_F12, key), "F12"));
    REQUIRE(!strcmp(ui_key_name(RML_KEY_OEM_COMMA, key), "Comma"));
    REQUIRE(!strcmp(ui_key_name(RML_KEY_A, key), "A"));
    REQUIRE(ui_chord_valid("Mod+Shift+F12"));
    REQUIRE(ui_chord_valid("Mod+Comma"));
    REQUIRE(!ui_chord_valid("Mod+S:Injected"));
    REQUIRE(!ui_chord_valid("Mod+S F12"));
    REQUIRE(!ui_chord_valid("Shift+Shift+A"));
    REQUIRE(!ui_chord_valid("Mod+F13"));
    REQUIRE(!ui_chord_valid("Bogus+A"));
    char event[128];
    const char *bindings = "S:Plain Mod+S:Save Mod+Shift+Z:Redo F12:Definition "
                           "Shift+F12:References Mod+Comma:Settings";
    REQUIRE(ui_key_binding(bindings, "S", RML_KM_META, RML_KM_META, event, sizeof event));
    REQUIRE(!strcmp(event, "Save"));
    REQUIRE(
        !ui_key_binding(bindings, "S", RML_KM_META | RML_KM_ALT, RML_KM_META, event, sizeof event)
    );
    REQUIRE(ui_key_binding(bindings, "S", RML_KM_CAPSLOCK, RML_KM_META, event, sizeof event));
    REQUIRE(!strcmp(event, "Plain"));
    REQUIRE(ui_key_binding(bindings, "F12", RML_KM_SHIFT, RML_KM_CTRL, event, sizeof event));
    REQUIRE(!strcmp(event, "References"));
    REQUIRE(ui_key_binding(bindings, "Comma", RML_KM_CTRL, RML_KM_CTRL, event, sizeof event));
    REQUIRE(!strcmp(event, "Settings"));
    REQUIRE(!ui_key_binding("Bogus+S:No", "S", 0, RML_KM_CTRL, event, sizeof event));
    char long_bindings[1024];
    memset(long_bindings, ' ', sizeof long_bindings);
    memcpy(long_bindings + 900, "Mod+Alt+S:SaveAll", 17);
    long_bindings[917] = 0;
    REQUIRE(
        ui_key_binding(long_bindings, "S", RML_KM_CTRL | RML_KM_ALT, RML_KM_CTRL, event, sizeof event)
    );
    REQUIRE(!strcmp(event, "SaveAll"));
    REQUIRE(!ui_key_binding("S:LongEvent", "S", 0, RML_KM_CTRL, event, 3));
    return true;
}

static void surface_cursor(const char *name)
{
    (void)name;
}
static void surface_clipboard_set(const char *text)
{
    (void)text;
}
static void surface_clipboard_get(rml_string *text)
{
    rml_string_set(text, "");
}

static double surface_time()
{
    return 0;
}
static bool surface_log(rml_log_type type, const char *message)
{
    (void)type;
    (void)message;
    return true;
}
static uintptr_t surface_compile(void *user, rml_mesh mesh)
{
    (void)user;
    (void)mesh;
    return 1;
}
static void surface_draw(void *user, uintptr_t geometry, rml_vec2 position, uintptr_t texture)
{
    (void)user;
    (void)geometry;
    (void)position;
    (void)texture;
}
static void surface_release(void *user, uintptr_t handle)
{
    (void)user;
    (void)handle;
}
static uintptr_t surface_texture(void *user, const uint8_t *pixels, int width, int height)
{
    (void)user;
    (void)pixels;
    (void)width;
    (void)height;
    return 1;
}

static uintptr_t surface_load_texture(void *user, const char *source, int *width, int *height)
{
    (void)user;
    (void)source;
    *width = *height = 1;
    return 0;
}

static bool surface_managed_texture(rml_element *root, rml_context *context)
{
    rml_document_show(root, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_context_update(context);
    rml_element *view = rml_element_query_selector(root, "#preview");
    rml_element *image = rml_element_query_selector(view, ".texture-image");
    REQUIRE(image);
    char value[UI_TEXTURE_KEY_MAX + 8];
    rml_element_attribute(image, "src", value, sizeof value);
    REQUIRE(!strcmp(value, "kit:preview/17"));
    const char *invalid[] = { "file:///private/asset.png",
                              "https://example.test/image.png",
                              "kit:",
                              "kit:bad key",
                              "kit:bad\\key" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        rml_element_set_attribute(view, "src", invalid[i]);
        rml_context_update(context);
        REQUIRE(rml_element_has_attribute(view, "data-invalid"));
        REQUIRE(!rml_element_has_attribute(image, "src"));
    }
    char oversized[UI_TEXTURE_KEY_MAX + 6];
    memcpy(oversized, "kit:", 4);
    memset(oversized + 4, 'a', UI_TEXTURE_KEY_MAX + 1);
    oversized[sizeof oversized - 1] = 0;
    rml_element_set_attribute(view, "src", oversized);
    rml_context_update(context);
    REQUIRE(rml_element_has_attribute(view, "data-invalid"));
    REQUIRE(!rml_element_has_attribute(image, "src"));
    rml_element_set_attribute(view, "src", "kit:runtime:21");
    rml_context_update(context);
    rml_element_attribute(image, "src", value, sizeof value);
    REQUIRE(!strcmp(value, "kit:runtime:21"));
    REQUIRE(!rml_element_has_attribute(view, "data-invalid"));
    rml_element_remove_attribute(view, "src");
    rml_context_update(context);
    REQUIRE(!rml_element_has_attribute(image, "src"));
    REQUIRE(!rml_element_has_attribute(view, "data-invalid"));
    return true;
}

TEST_CASE(authored_surface_managed_texture_resource_contract)
{
    ui_source_resource *resources = NULL;
    char error[256];
    REQUIRE(
        ui_source_resources("<texture-view data-attr-src='self.preview'/>", "", &resources, error, sizeof error)
    );
    ui_source_resources_free(resources);
    resources = NULL;
    REQUIRE(
        ui_source_resources("<texture-view src='kit:preview/1'/>", "", &resources, error, sizeof error)
    );
    ui_source_resources_free(resources);
    resources = NULL;
    REQUIRE(
        !ui_source_resources("<texture-view src='https://example.test/a.png'/>", "", &resources, error, sizeof error)
    );
    REQUIRE(strstr(error, "bounded kit:"));
    REQUIRE(
        !ui_source_resources("<img data-attr-src='self.preview'/>", "", &resources, error, sizeof error)
    );
    REQUIRE(strstr(error, "Dynamic image URL"));
    return true;
}

static void *surface_open(const char *root, const char *name, bool page)
{
    (void)root;
    (void)name;
    (void)page;
    return malloc(1);
}
static int source_opens;
static void *surface_open_context(void *context, const char *root, const char *name, bool page)
{
    int *opens = context;
    (*opens)++;
    return surface_open(root, name, page);
}
static int surface_read(void *source, double time, ui_document *out, char *error, size_t size)
{
    (void)source;
    (void)time;
    (void)error;
    (void)size;
    out->rml = strdup("<button id='node' data-measure/>");
    out->rcss = strdup("");
    return 1;
}
static void surface_event(void *user, const rml_event *event)
{
    (void)user;
    (void)event;
}
static void surface_emit(
    void *user,
    const rml_event *event,
    const char *name,
    int argc,
    const char *const *args
)
{
    (void)user;
    (void)event;
    (void)name;
    (void)argc;
    (void)args;
}
static ui_mount *surface_mount(rml_element *root, rml_listener *listener)
{
    const render_ui_loader loader = { .read = surface_read,
                                      .close = free,
                                      .context = &source_opens,
                                      .open_context = surface_open_context,
                                      .resident_images = true };
    ui_mount *mount = ui_mount_new(".", "Surface");
    ui_mount_set_loader(mount, &loader);
    ui_mount_sync(mount, root, listener, 0);
    return mount;
}
typedef struct
{
    int count;
    bool valid;
} surface_text_observer;
static void surface_raw_text(void *user, const rml_event *event)
{
    surface_text_observer *observer = user;
    char text[128];
    size_t bytes = rml_event_value(event, text, sizeof text);
    observer->count++;
    observer->valid = event->id == RML_EVENT_TEXTINPUT && event->has_value &&
                      bytes == strlen("雪🎮") && !strcmp(text, "雪🎮");
}
static bool surface_raw_text_input(rml_element *root, rml_context *context)
{
    surface_text_observer observer = { 0 };
    rml_listener *listener = rml_listener_create(surface_raw_text, &observer);
    rml_element *target = rml_element_by_id(root, "child");
    rml_element_set_attribute(target, "tabindex", "0");
    rml_element_add_event_listener(target, RML_EVENT_TEXTINPUT, listener, false);
    REQUIRE(rml_element_focus(target, false));
    rml_context_process_text_input(context, "雪🎮", strlen("雪🎮"));
    REQUIRE(observer.count == 1 && observer.valid);
    rml_element_remove_event_listener(target, RML_EVENT_TEXTINPUT, listener, false);
    rml_listener_destroy(listener);
    return true;
}
static void surface_key_event(void *user, const rml_event *event)
{
    ui_mount_listen(*(ui_mount **)user, event);
}
static bool surface_key_ownership(rml_element *root)
{
    ui_mount *mount = NULL;
    rml_listener *listener = rml_listener_create(surface_key_event, &mount);
    mount = surface_mount(root, listener);
    rml_element *body = ui_mount_root(mount), *node = rml_element_by_id(body, "node");
    REQUIRE(node);
    rml_element_set_attribute(body, "data-keys", "W:EditorMove Shift+G:EditorGrid Escape:ShellCancel");
    rml_element_set_attribute(node, "data-own-keys", "true");
    rml_element_set_attribute(node, "data-keys", "Escape:LeaveCanvas");
    rml_event event = { .key = RML_KEY_W };
    rml_element_dispatch_event(node, "keydown", &event);
    REQUIRE(!ui_mount_pending(mount));
    event.key = RML_KEY_G;
    event.modifiers = RML_KM_SHIFT;
    rml_element_dispatch_event(node, "keydown", &event);
    REQUIRE(!ui_mount_pending(mount));
    event.key = RML_KEY_ESCAPE;
    event.modifiers = 0;
    rml_element_dispatch_event(node, "keydown", &event);
    ui_activation activation;
    REQUIRE(ui_mount_take(mount, &activation) && !strcmp(activation.event, "LeaveCanvas"));
    rml_element_remove_attribute(node, "data-keys");
    rml_element_dispatch_event(node, "keydown", &event);
    REQUIRE(ui_mount_take(mount, &activation) && !strcmp(activation.event, "ShellCancel"));
    rml_element_remove_attribute(node, "data-own-keys");
    event.key = RML_KEY_W;
    rml_element_dispatch_event(node, "keydown", &event);
    REQUIRE(ui_mount_take(mount, &activation) && !strcmp(activation.event, "EditorMove"));
    rml_element_remove_attribute(body, "data-keys");
    ui_mount_free(mount);
    rml_element_remove_event_listener(root, RML_EVENT_KEYDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEUP, listener, true);
    rml_listener_destroy(listener);
    return true;
}
static bool surface_queue_lifetimes(rml_element *root, rml_context *context)
{
    ui_data data;
    ui_data_init(&data, context, NULL, surface_emit, NULL);
    rml_listener *listener = rml_listener_create(surface_event, NULL);
    ui_mount *mount = surface_mount(root, listener);
    ui *model = ui_new();
    REQUIRE(ui_set_style(model, 17, "#node", "width", "31px"));
    REQUIRE(ui_focus(model, 17, "#node") && ui_click(model, 17, "#node"));
    ui_element *setting = ui_element_at(model, 0);
    REQUIRE(setting->focus && setting->click);
    ui_mount_apply_read_only(mount, model, 17);
    REQUIRE(setting->focus && setting->click);
    char width[32];
    rml_element_property(rml_element_by_id(ui_mount_root(mount), "node"), "width", width, sizeof width);
    REQUIRE(!strcmp(width, "31px"));
    rml_context_set_dp_ratio(context, 2);
    ui_mount_measure(mount, model, 17);
    float ratio;
    REQUIRE(ui_view_pixel_ratio(model, 17, "#node", &ratio) && ratio == 2);
    REQUIRE(ui_view_pixel_ratio(model, 17, "body", &ratio) && ratio == 2);
    ui_free(model);
    rml_event event = { .current = root, .target = root, .button = -1 };
    char *text = malloc(1024u * 1024u + 1u);
    REQUIRE(text);
    memset(text, 'x', 1024u * 1024u);
    text[1024u * 1024u] = 0;
    const char *args[] = { text, "73" };
    for (int i = 0; i < 20; i++) {
        ui_mount_emit(mount, &event, "OwnedText", 2, args);
    }
    REQUIRE(strstr(ui_mount_error(mount), "16 MiB"));
    int count = 0;
    size_t bytes = 0;
    ui_activation taken;
    while (ui_mount_take(mount, &taken)) {
        REQUIRE(strlen(ui_activation_argument(&taken, 0)) == 1024u * 1024u);
        REQUIRE(strlen(ui_activation_argument(&taken, 0)) == 1024u * 1024u);
        bytes += taken.payload_bytes;
        count++;
    }
    REQUIRE(count > 1 && count < 20 && bytes <= 16u * 1024u * 1024u);
    ui_mount_detach(mount);
    REQUIRE(!ui_mount_pending(mount));
    ui_mount_free(mount);
    rml_element_remove_event_listener(root, RML_EVENT_KEYDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEUP, listener, true);
    rml_listener_destroy(listener);
    listener = rml_listener_create(surface_event, NULL);
    mount = surface_mount(root, listener);
    const char *small[] = { "73" };
    for (int i = 0; i < 1025; i++) {
        ui_mount_emit(mount, &event, "Small", 1, small);
    }
    REQUIRE(strstr(ui_mount_error(mount), "1024"));
    count = 0;
    while (ui_mount_take(mount, &taken)) {
        count++;
    }
    REQUIRE(count == 1024);
    ui_mount_emit(mount, &event, "OwnedText", 2, args);
    ui_mount_emit(mount, &event, "OwnedText", 2, args);
    REQUIRE(ui_mount_take(mount, &taken));
    REQUIRE(ui_mount_pending(mount));
    ui_mount_detach(mount);
    REQUIRE(!ui_mount_pending(mount));
    ui_mount_free(mount);
    rml_element_remove_event_listener(root, RML_EVENT_KEYDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEDOWN, listener, true);
    rml_element_remove_event_listener(root, RML_EVENT_MOUSEUP, listener, true);
    rml_listener_destroy(listener);
    ui_data_free(&data);
    free(text);
    return true;
}

TEST_CASE(authored_surface_root_selector_applies_style_class_and_visibility)
{
    static const rml_system_interface system = { .elapsed_time = surface_time,
                                                 .set_mouse_cursor = surface_cursor,
                                                 .set_clipboard_text = surface_clipboard_set,
                                                 .get_clipboard_text = surface_clipboard_get,
                                                 .log_message = surface_log };
    static const rml_render_interface render = { .compile_geometry = surface_compile,
                                                 .render_geometry = surface_draw,
                                                 .release_geometry = surface_release,
                                                 .load_texture = surface_load_texture,
                                                 .generate_texture = surface_texture,
                                                 .release_texture = surface_release };
    rml_set_system_interface(&system);
    REQUIRE(rml_initialize());
    controls_register();
    rml_renderer *renderer = rml_renderer_create(&render, NULL);
    rml_context *context = rml_create_context("authored-surface", 320, 200, renderer);
    REQUIRE(context);
    rml_element *root = rml_context_load_document_from_memory(
        context,
        "<rml><head/><body><div id='child'/><texture-view id='preview' "
        "src='kit:preview/17'/></body></rml>",
        "surface.rml"
    );
    REQUIRE(root);
    REQUIRE(surface_managed_texture(root, context));
    REQUIRE(surface_raw_text_input(root, context));
    ui *model = ui_new();
    REQUIRE(ui_set_style(model, 1, "body", "font-size", "19px"));
    REQUIRE(ui_set_class(model, 1, "body", "dark", true));
    ui_apply_element(root, ui_element_at(model, 0));
    char value[128];
    rml_element_property(root, "font-size", value, sizeof value);
    REQUIRE(!strcmp(value, "19px"));
    REQUIRE(rml_element_is_class_set(root, "dark"));
    REQUIRE(!rml_element_is_class_set(rml_element_query_selector(root, "#child"), "dark"));
    REQUIRE(ui_set_visible(model, 1, ":scope", false));
    ui_apply_element(root, ui_element_at(model, 1));
    rml_element_property(root, "display", value, sizeof value);
    REQUIRE(!strcmp(value, "none"));
    ui_free(model);
    /* A deliberately empty resident catalog must not mask this explicit
     * authored-source provider. No GPU backend or filesystem is involved. */
    ui_documents *catalog = ui_documents_new();
    ui_documents_exchange(catalog);
    int before = source_opens;
    REQUIRE(surface_key_ownership(root));
    bool queued = surface_queue_lifetimes(root, context);
    ui_documents_exchange(catalog);
    ui_documents_free(catalog);
    REQUIRE(queued && source_opens > before);
    rml_document_close(root);
    REQUIRE(rml_remove_context("authored-surface"));
    rml_renderer_destroy(renderer);
    rml_shutdown();
    return true;
}

TEST_CASE(authored_surface_owned_text_keeps_unicode_and_document_boundaries)
{
    ui_activation a = { 0 };
    char text[512];
    memset(text, 'x', 127);
    memcpy(text + 127, "雪🎮", 7);
    text[134] = 0;
    const char *args[] = { text, "73" };
    REQUIRE(ui_activation_capture(&a, 2, args, text));
    REQUIRE(strlen(ui_activation_argument(&a, 0)) == 134);
    REQUIRE(!strcmp(ui_activation_argument(&a, 0) + 127, "雪🎮"));
    REQUIRE(!strcmp(ui_activation_text(&a), text));
    REQUIRE(a.full_text == a.full_args[0]);
    text[127] = 0;
    REQUIRE(strlen(ui_activation_text(&a)) == 134);
    REQUIRE(strlen(ui_activation_text(&a)) == 134);
    size_t length = 1024u * 1024u;
    char *large = malloc(UI_ACTIVATION_BYTES_MAX + 2u);
    REQUIRE(large);
    memset(large, 'a', UI_ACTIVATION_BYTES_MAX + 1u);
    large[length] = 0;
    const char *large_args[] = { large, "73" };
    REQUIRE(ui_activation_capture(&a, 2, large_args, large));
    REQUIRE(strlen(ui_activation_argument(&a, 0)) == length);
    REQUIRE(strlen(ui_activation_text(&a)) == length);
    REQUIRE(a.payload_bytes <= UI_ACTIVATION_BYTES_MAX);
    snprintf(a.event, sizeof a.event, "OwnedText");
    surface_observer observer = { .expected_text = large };
    render_ui_host host = { .context = &observer,
                            .payload = surface_payload,
                            .dispatch = surface_dispatch };
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &a, RML_KM_CTRL));
    REQUIRE(observer.called && observer.valid);
    const char *metadata[] = { "73" };
    REQUIRE(ui_activation_capture(&a, 1, metadata, large));
    observer.called = false;
    REQUIRE(ui_activation_send(&host, ecs_entity_from_u64(17), &a, RML_KM_CTRL));
    REQUIRE(observer.called && observer.valid);
    large[length] = 'a';
    large[UI_ACTIVATION_BYTES_MAX + 1u] = 0;
    REQUIRE(!ui_activation_capture(&a, 2, large_args, large));
    REQUIRE(strlen(ui_activation_text(&a)) == length);
    REQUIRE(!ui_activation_capture(&a, UI_ACTIVATION_ARGS + 1, large_args, NULL));
    ui_activation_clear(&a);
    REQUIRE(!a.full_args[0] && !a.full_text && !a.payload_bytes);
    ui_activation_clear(&a);
    free(large);
    return true;
}

/* Exercises the production mount manager, layout and pointer routing. Creating
 * and updating a backend is CPU-only: no composite/render call is made. */
typedef struct
{
    uint64_t owners[3];
    int counts[3], unexpected;
    ui *model;
} embedded_observer;

static int embedded_payload(void *context, const char *event, render_ui_event_field *fields, int capacity)
{
    (void)context;
    (void)event;
    (void)fields;
    (void)capacity;
    return 0;
}

static int embedded_dispatch(void *context, uint64_t owner, const char *event, execution_arguments arguments)
{
    embedded_observer *observer = context;
    for (int i = 0; i < 3; i++) {
        if (owner != observer->owners[i]) {
            continue;
        }
        if (strcmp(event, "Increment") || arguments.count) {
            observer->unexpected++;
            return 0;
        }
        observer->counts[i]++;
        av_doc *value = av_doc_new();
        ui_set_data(observer->model, owner, "count", av_sint(value, observer->counts[i]));
        av_doc_free(value);
        ui_focus(observer->model, owner, "#draft");
        return 0;
    }
    observer->unexpected++;
    return 0;
}

static bool embedded_document(
    ui_documents *catalog,
    const char *name,
    const char *markup,
    const char *style,
    bool page
)
{
    prepared_ui_document document = { .name = strdup(name),
                                      .markup = strdup(markup),
                                      .style = strdup(style),
                                      .body = page ? strdup("") : NULL };
    return ui_documents_add_owned(catalog, &document);
}

static void embedded_step(render_ui *renderer, const ui_world *world)
{
    render_ui_update(renderer, world, 320, 720, 0.016);
}

static void embedded_click(rml_context *context, int x, int y)
{
    rml_context_process_mouse_move(context, x, y, 0);
    rml_context_process_mouse_button_down(context, 0, 0);
    rml_context_process_mouse_button_up(context, 0, 0);
}

TEST_CASE(authored_surface_embedded_owner_pointer_focus_and_lifetime)
{
    ecs_world *ecs = ecs_world_new();
    REQUIRE(ecs_world_register_component(ecs, &COMPONENT_Ui, NULL) == ECS_OK);
    ecs_entity owners[3];
    embedded_observer observer = { .model = ui_new() };
    for (int i = 0; i < 3; i++) {
        owners[i] = ecs_world_create_entity(ecs);
        observer.owners[i] = ecs_entity_to_u64(owners[i]);
        Ui component = { .document = i == 2 ? "Leaf" : "Counter", .hidden = true };
        REQUIRE(ecs_world_record_add(ecs, owners[i], COMPONENT_Ui.id, &component, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    av_doc *value = av_doc_new();
    for (int i = 0; i < 3; i++) {
        REQUIRE(ui_set_data(observer.model, observer.owners[i], "count", av_sint(value, 0)));
    }
    av_doc_free(value);
    char page[2048], counter[2048];
    snprintf(
        page,
        sizeof page,
        "<section id='left' "
        "style='position:absolute;left:0px;top:0px;width:100px;height:160px' "
        "data-ui-entity='ecs.entity(%llu)'/>"
        "<section id='right' "
        "style='position:absolute;left:120px;top:0px;width:100px;height:160px' "
        "data-ui-entity='ecs.entity(%llu)' data-ui-document='Counter'/>",
        (unsigned long long)observer.owners[0],
        (unsigned long long)observer.owners[1]
    );
    snprintf(
        counter,
        sizeof counter,
        "<button id='increment' style='width:80px;height:30px' "
        "data-event-click=\"emit('Increment')\">+</button>"
        "<input id='draft' style='display:block;width:80px;height:20px'/>"
        "<div id='counter' data-measure "
        "style='width:80px;height:10px'>{{self.count}}</div>"
        "<section id='nested' style='width:80px;height:40px' "
        "data-ui-entity='ecs.entity(%llu)' "
        "data-ui-document='Leaf'/>",
        (unsigned long long)observer.owners[2]
    );
    ui_documents *catalog = ui_documents_new();
    REQUIRE(embedded_document(catalog, "EmbeddedPage", page, "", true));
    REQUIRE(
        embedded_document(catalog, "Counter", counter, "[data-ui=Counter] #increment { pointer-events:auto; }", false)
    );
    REQUIRE(embedded_document(
        catalog,
        "Leaf",
        "<button id='increment' style='width:70px;height:25px' "
        "data-event-click=\"emit('Increment')\">nested</button>"
        "<input id='draft' style='width:60px;height:10px'/>",
        "",
        false
    ));
    ui_documents_exchange(catalog);
    render_ui *renderer = render_ui_new_prepared(NULL);
    REQUIRE(renderer && render_ui_context(renderer));
    ui_world world = { .ecs = ecs,
                       .page = "EmbeddedPage",
                       .model = observer.model,
                       .host = { .context = &observer,
                                 .payload = embedded_payload,
                                 .dispatch = embedded_dispatch } };
    for (int i = 0; i < 3; i++) {
        embedded_step(renderer, &world);
    }
    REQUIRE(!render_ui_error(renderer)[0]);
    rml_context *context = render_ui_context(renderer);
    embedded_click(context, 10, 10);
    embedded_step(renderer, &world);
    REQUIRE(observer.counts[0] == 1 && observer.counts[1] == 0 && !observer.unexpected);
    rml_element *left_draft = rml_context_focus_element(context);
    REQUIRE(left_draft && !strcmp(rml_element_id(left_draft), "draft"));
    rml_element *left_root = rml_element_parent(left_draft);
    REQUIRE(rml_element_has_attribute(left_root, "data-ui-embedded-root"));
    rml_element *left = rml_element_parent(left_root);
    REQUIRE(!strcmp(rml_element_id(left), "left"));
    char text[64];
    rml_element *right_host = rml_element_query_selector(rml_element_parent(left), "#right");
    REQUIRE(right_host);
    rml_element_inner_rml(rml_element_query_selector(right_host, "#counter"), text, sizeof text);
    REQUIRE(!strcmp(text, "0"));
    rml_element_set_value(left_draft, "left draft");
    embedded_click(context, 130, 10);
    embedded_step(renderer, &world);
    REQUIRE(observer.counts[0] == 1 && observer.counts[1] == 1 && !observer.unexpected);
    rml_element *right_draft = rml_context_focus_element(context);
    REQUIRE(right_draft && right_draft != left_draft);
    rml_element *right = rml_element_parent(rml_element_parent(right_draft));
    REQUIRE(!strcmp(rml_element_id(right), "right"));
    rml_element_value(left_draft, text, sizeof text);
    REQUIRE(!strcmp(text, "left draft"));
    rml_element_inner_rml(rml_element_query_selector(left_root, "#counter"), text, sizeof text);
    REQUIRE(!strcmp(text, "1"));
    rml_element_inner_rml(rml_element_query_selector(right, "#counter"), text, sizeof text);
    REQUIRE(!strcmp(text, "1"));
    float width;
    REQUIRE(ui_view_width(observer.model, observer.owners[0], "#counter", &width) && width == 80);
    REQUIRE(ui_view_width(observer.model, observer.owners[1], "#counter", &width) && width == 80);
    /* An outer entity's selector must not write through the inner boundary. */
    REQUIRE(ui_set_class(observer.model, observer.owners[0], "#increment", "owner-local", true));
    embedded_step(renderer, &world);
    rml_element *nested = rml_element_query_selector(left_root, "#nested");
    rml_element *nested_button = rml_element_query_selector(nested, "#increment");
    REQUIRE(nested_button && !rml_element_is_class_set(nested_button, "owner-local"));
    rml_vec2 at = rml_element_absolute_offset(nested_button, RML_BOX_BORDER);
    embedded_click(context, (int)at.x + 2, (int)at.y + 2);
    embedded_step(renderer, &world);
    REQUIRE(observer.counts[2] == 1 && observer.counts[0] == 1 && observer.counts[1] == 1);
    /* Read-only mode discards queued pointer events without consuming focus. */
    embedded_click(context, 130, 10);
    world.read_only = true;
    REQUIRE(ui_focus(observer.model, observer.owners[1], "#draft"));
    embedded_step(renderer, &world);
    REQUIRE(observer.counts[1] == 1);
    world.read_only = false;
    embedded_step(renderer, &world);
    REQUIRE(rml_context_focus_element(context) == right_draft);
    /* Unmount clears geometry and expires all descendant references. */
    rml_element_ref *expired = rml_element_ref_create(left_draft);
    rml_element_set_attribute(left, "data-ui-entity", "");
    embedded_step(renderer, &world);
    REQUIRE(!rml_element_ref_get(expired));
    REQUIRE(!ui_view_width(observer.model, observer.owners[0], "#counter", &width));
    embedded_step(renderer, &world);
    REQUIRE(!ui_view_width(observer.model, observer.owners[0], "#counter", &width));
    rml_element_ref_destroy(expired);
    char owner[64];
    snprintf(
        owner,
        sizeof owner,
        "ecs.entity(%llu)",
        (unsigned long long)observer.owners[0]
    );
    rml_element_set_attribute(left, "data-ui-entity", owner);
    embedded_step(renderer, &world);
    REQUIRE(rml_element_query_selector(left, "#draft"));
    rml_element_inner_rml(rml_element_query_selector(left, "#counter"), text, sizeof text);
    REQUIRE(!strcmp(text, "1"));
    /* A retiring owner cannot receive an already queued event. */
    embedded_click(context, 130, 10);
    REQUIRE(ecs_world_record_destroy(ecs, owners[1], 0) == ECS_OK);
    embedded_step(renderer, &world);
    REQUIRE(observer.counts[1] == 1 && !rml_element_query_selector(right, "#draft"));
    REQUIRE(!ui_view_width(observer.model, observer.owners[1], "#counter", &width));
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    /* Page replacement removes surviving embeds and their queued events. */
    expired = rml_element_ref_create(rml_element_query_selector(left, "#draft"));
    world.page = "";
    embedded_step(renderer, &world);
    REQUIRE(!rml_element_ref_get(expired));
    REQUIRE(!ui_view_width(observer.model, observer.owners[0], "#counter", &width));
    rml_element_ref_destroy(expired);
    render_ui_free(renderer);
    ui_documents_exchange(catalog);
    ui_documents_free(catalog);
    ui_free(observer.model);
    ecs_world_free(ecs);
    return true;
}

TEST_CASE(authored_surface_embedded_cycles_depth_and_actual_world_identity)
{
    ecs_world *ecs = ecs_world_new(), *foreign = ecs_world_new();
    ecs_entity owner = ecs_world_create_entity(ecs);
    uint64_t id = ecs_entity_to_u64(owner);
    ui *model = ui_new(), *foreign_model = ui_new();
    av_doc *value = av_doc_new();
    REQUIRE(ui_set_data(model, id, "marker", av_sint(value, 42)));
    REQUIRE(ui_set_data(foreign_model, id, "marker", av_sint(value, 7)));
    av_doc_free(value);
    ui_documents *catalog = ui_documents_new();
    char page[1024], markup[1024];
    snprintf(
        page,
        sizeof page,
        "<section id='cycle' data-ui-entity='ecs.entity(%llu)' "
        "data-ui-document='Cycle'/>"
        "<section id='depth' data-ui-entity='ecs.entity(%llu)' "
        "data-ui-document='Depth0'/>"
        "<section id='invalid' "
        "data-ui-entity='ecs.entity(18446744073709551615)' "
        "data-ui-document='Cycle'/>",
        (unsigned long long)id,
        (unsigned long long)id
    );
    REQUIRE(embedded_document(catalog, "BoundsPage", page, "", true));
    snprintf(
        markup,
        sizeof markup,
        "<span id='world'>{{self.marker}}</span>"
        "<section id='recurse' data-ui-entity='ecs.entity(%llu)' "
        "data-ui-document='Cycle'/>",
        (unsigned long long)id
    );
    REQUIRE(embedded_document(catalog, "Cycle", markup, "", false));
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof name, "Depth%d", i);
        snprintf(
            markup,
            sizeof markup,
            "<section data-ui-entity='ecs.entity(%llu)' "
            "data-ui-document='Depth%d'/>",
            (unsigned long long)id,
            i + 1
        );
        REQUIRE(embedded_document(catalog, name, markup, "", false));
    }
    ui_documents_exchange(catalog);
    render_ui *renderer = render_ui_new_prepared(NULL);
    REQUIRE(renderer && render_ui_context(renderer));
    render_ui_set_model_entity(renderer, id);
    ui_world world = { .ecs = ecs, .page = "BoundsPage", .model = model };
    for (int i = 0; i < 12; i++) {
        embedded_step(renderer, &world);
    }
    REQUIRE(!render_ui_error(renderer)[0]);
    rml_element *document = rml_context_document(render_ui_context(renderer), 0);
    REQUIRE(document);
    rml_element *cycle = rml_element_query_selector(document, "#cycle");
    rml_element *depth = rml_element_query_selector(document, "#depth");
    rml_element *invalid = rml_element_query_selector(document, "#invalid");
    REQUIRE(cycle && depth && invalid);
    REQUIRE(rml_element_query_selector_all(cycle, "[data-ui-embedded-root]", NULL, 0) == 1);
    REQUIRE(rml_element_query_selector_all(depth, "[data-ui-embedded-root]", NULL, 0) == 8);
    REQUIRE(!rml_element_query_selector(invalid, "[data-ui-embedded-root]"));
    REQUIRE(
        !rml_element_query_selector(rml_element_query_selector(cycle, "#recurse"), "[data-ui-embedded-root]")
    );
    char text[32];
    rml_element *shown = rml_element_query_selector(cycle, "#world");
    REQUIRE(shown);
    rml_element_inner_rml(shown, text, sizeof text);
    REQUIRE(!strcmp(text, "42"));
    rml_element_ref *expired = rml_element_ref_create(shown);
    /* Neither the model/inspected entity nor a different world's live handle
     * can authorize a mount in the actual empty rendered ECS. */
    world.ecs = foreign;
    world.model = foreign_model;
    embedded_step(renderer, &world);
    REQUIRE(!rml_element_ref_get(expired));
    REQUIRE(!rml_element_query_selector(cycle, "[data-ui-embedded-root]"));
    REQUIRE(!rml_element_query_selector(depth, "[data-ui-embedded-root]"));
    rml_element_ref_destroy(expired);
    ecs_entity replacement = ecs_world_create_entity(foreign);
    REQUIRE(ecs_entity_to_u64(replacement) == id);
    embedded_step(renderer, &world);
    shown = rml_element_query_selector(cycle, "#world");
    REQUIRE(shown);
    rml_element_inner_rml(shown, text, sizeof text);
    REQUIRE(!strcmp(text, "7"));
    render_ui_free(renderer);
    ui_documents_exchange(catalog);
    ui_documents_free(catalog);
    ui_free(model);
    ui_free(foreign_model);
    ecs_world_free(ecs);
    ecs_world_free(foreign);
    return true;
}

void test_authored_surface()
{
    RUN_TEST_CASE(authored_surface_embedded_cycles_depth_and_actual_world_identity);
    RUN_TEST_CASE(authored_surface_embedded_owner_pointer_focus_and_lifetime);
    RUN_TEST_CASE(authored_surface_managed_texture_resource_contract);
    RUN_TEST_CASE(authored_surface_complete_scalar_key_tuple_owns_unicode_buffer);
    RUN_TEST_CASE(authored_surface_geometry_expires_and_updates);
    RUN_TEST_CASE(authored_surface_owned_text_keeps_unicode_and_document_boundaries);
    RUN_TEST_CASE(authored_surface_keyboard_chords_are_exact_and_not_truncated);
    RUN_TEST_CASE(authored_surface_root_selector_applies_style_class_and_visibility);
    RUN_TEST_CASE(authored_surface_vectors_control_cancel_and_arguments_are_independent);
}
