#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void shader_image_files_ui();

static void capture(const char *name)
{
    const char *directory = getenv("CHIBI_SHADER_CAPTURE_DIR");
    if (!directory || !*directory) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(app_save_screenshot(path) == 0);
}

static behavior_asset *read_library()
{
    FILE *f = fopen("test/fixtures/content/shader-libraries/ShaderMath.json", "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *json = malloc((size_t)size + 1);
    assert(json);
    assert(fread(json, 1, (size_t)size, f) == (size_t)size);
    json[size] = 0;
    fclose(f);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    return asset;
}

static void shader_scene_frame(app_scene_authoring *scene, engine_viewport *viewport)
{
    editor *e = scene->graph_editor;
    cf_app_update(NULL);
    app_input_frame();
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    editor_tick(e, e->monotonic_time + 1.0 / 60);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, true);
    editor_prepare_docking();
    app_scene_authoring_draw_viewport(scene, viewport);
    app_scene_authoring_draw(scene);
    editor_draw(e);
    cf_app_draw_onto_screen(true);
}

static void shader_files_inspector(app_scene_authoring *scene, engine_viewport *viewport)
{
    const char *paths[] = { "test/fixtures/content/materials/PulseRing.json",
                            "test/fixtures/content/shapes/SmoothUnion.json" };
    editor *e = scene->graph_editor;
    for (int i = 0; i < 2; i++) {
        select_instance(scene, 0);
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        shader_scene_frame(scene, viewport);
        editor_ui_rect rect;
        assert(editor_ui_item_rect("inspector.scene", &rect));
        assert(!editor_ui_item_rect("shader.preview", &rect));
        ecs_entity selected = selected_scene_entity(scene);
        const scene_content_entry *file = NULL;
        for (int j = 0; j < acount(scene->content); j++) {
            if (!strcmp(scene->content[j].path, paths[i])) {
                file = &scene->content[j];
                break;
            }
        }
        assert(file && !file->directory);
        select_content_entry(scene, file);
        open_content_entry(scene, file); /* The Files Open/double-click command. */
        shader_scene_frame(scene, viewport);
        assert(scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR);
        assert(ecs_entity_to_u64(selected_scene_entity(scene)) == ecs_entity_to_u64(selected));
        assert(!editor_has_inspection_selection(e));
        assert(e->inspector_active);
        assert(!editor_ui_item_rect("inspector.scene", &rect));
        assert(editor_ui_item_rect("shader.preview", &rect));
        assert(!editor_ui_item_rect("shader.error", &rect));
        assert(!editor_ui_item_rect("shader.backend_error", &rect));
    }
    capture("shader-files-inspector");
}

static void shader_scene_ui()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new(
        "Before shader",
        behavior_asset_new("Before shader")
    );
    assert(game_live_add_definition(live, definition) == 0);
    editor *graph = editor_new(behavior_asset_new("Before shader"));
    engine *runtime = engine_new(world, live);
    engine_viewport *viewport = engine_viewport_new(800, 600);
    app_scene_authoring *scene = app_scene_authoring_new(
        world,
        live,
        definition,
        editor_behavior_asset(graph),
        graph,
        NULL,
        NULL,
        runtime
    );
    assert(app_scene_authoring_set_content_root(scene, "test/fixtures/content") == 0);
    assert(app_scene_authoring_load_content(scene) == 0);
    assert(app_scene_authoring_load(scene, "test/fixtures/content/scenes/ShaderDemo.json") == 0);
    engine_viewport_set_render_callback(viewport, app_scene_authoring_render, scene);
    editor_ui_reset_layout();
    for (int frame = 0; frame < 30; frame++) {
        shader_scene_frame(scene, viewport);
    }
    capture("shader-scene-native");
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    for (int i = 0; i < acount(scene->actors); i++) {
        const GameTransform
            *position = GAME_GameTransform_get_const(world, scene->actors[i].entity);
        assert(fabsf(position->x - camera.center_x) < camera.span_width * .5f);
        assert(fabsf(-position->y - camera.center_y) < camera.span_height * .5f);
    }
    shader_files_inspector(scene, viewport);
    app_scene_authoring_free(scene);
    engine_viewport_free(viewport);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(graph);
    ecs_world_free(world);
}

static void shader_frame(editor *e)
{
    cf_app_update(NULL);
    app_input_frame();
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    editor_tick(e, e->monotonic_time + 1.0 / 60);
    editor_prepare_docking();
    editor_draw(e);
    cf_app_draw_onto_screen(true);
}

static editor_ui_rect item(const char *id)
{
    editor_ui_rect rect;
    if (!editor_ui_item_rect(id, &rect)) {
        fprintf(stderr, "Shader UI item not visible: %s\n", id);
        assert(!"Required shader control is not visible");
    }
    return rect;
}

static void click_at(editor *e, float x, float y)
{
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    shader_frame(e);
}

static void click(editor *e, const char *id)
{
    editor_ui_rect rect = item(id);
    click_at(e, rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f);
}

static void select_node(editor *e, uint64_t id)
{
    editor_reveal_node(e, graph_node_by_id(editor_active_graph(e), id));
    for (int i = 0; i < 12; i++) {
        shader_frame(e);
    }
}

static void choose_type(editor *e, int type)
{
    click(e, "shader.parameter.type");
    shader_frame(e); /* Popup sizing precedes its first visible frame. */
    char id[64];
    snprintf(id, sizeof id, "shader.parameter.type.%d", type);
    click(e, id);
    shader_frame(e);
}

static void enter_component(editor *e, const char *id, int width, int component, const char *text)
{
    assert(component >= 0 && component < width);
    char field[128];
    snprintf(field, sizeof field, "%s.axis.%d", id, component);
    editor_ui_rect rect = item(width == 1 ? id : field);
    click_at(e, rect.x + rect.width * .5f, rect.y + rect.height * .5f);
    click_at(e, rect.x + rect.width * .5f, rect.y + rect.height * .5f);
    ImGuiKey modifier = ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super
                                                             : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, true);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, false);
    shader_frame(e);
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), text);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    shader_frame(e);
}

static void enter_color_component(editor *e, int component, const char *text)
{
    editor_ui_rect row = item("color.hsv");
    float x = row.x + row.width * (component + 0.5f) / 4.0f;
    float y = row.y + row.height * 0.5f;
    ImGuiKey activation_modifier = ImGui_GetIO()->ConfigMacOSXBehaviors
                                     ? ImGuiMod_Super
                                     : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), activation_modifier, true);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), x, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), activation_modifier, false);
    shader_frame(e);
    assert(ImGui_GetIO()->WantTextInput);
    ImGuiKey modifier = ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super
                                                             : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, true);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, false);
    shader_frame(e);
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), text);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    shader_frame(e);
}

static const pin_spec *output(editor *e, uint64_t id)
{
    const node *n = graph_node_by_id(editor_active_graph(e), id);
    assert(n && n->output_count == 1);
    return &n->output_pins[0];
}

static void expect_vector(editor *e, uint64_t id, const float *expected, int width, bool color)
{
    const pin_spec *pin = output(e, id);
    assert(ed_vector_width(pin) == width && !!(pin->flags & PIN_FLAG_COLOR) == color);
    assert(pin->default_value.ptr);
    const float *value = pin->default_value.ptr;
    for (int i = 0; i < width; i++) {
        if (fabsf(value[i] - expected[i]) >= .00001f) {
            fprintf(
                stderr,
                "vector %llu axis %d: got %g expected %g\n",
                (unsigned long long)id,
                i,
                value[i],
                expected[i]
            );
            capture("vector-value-failure");
        }
        assert(fabsf(value[i] - expected[i]) < 0.00001f);
    }
}

static void expect_same_type(editor *e, uint64_t id, int choice, const float *value, int width, bool color)
{
    int history = acount(e->undo);
    choose_type(e, choice);
    expect_vector(e, id, value, width, color);
    assert(acount(e->undo) == history);
}

static void editable_shader_values(editor *e, uint64_t parameter, uint64_t constant)
{
    select_node(e, parameter);
    choose_type(e, 2);
    char control[96];
    snprintf(control, sizeof control, "vector.details.%llu.1.0", (unsigned long long)parameter);
    const float vector3[] = { 0.125f, 0.5f, 1.75f };
    enter_component(e, control, 3, 0, "0.125");
    enter_component(e, control, 3, 1, "0.5");
    enter_component(e, control, 3, 2, "1.75");
    expect_vector(e, parameter, vector3, 3, false);
    expect_same_type(e, parameter, 2, vector3, 3, false);
    editor_undo(e);
    shader_frame(e);
    expect_vector(e, parameter, (float[]){ 0.125f, 0.5f, 0 }, 3, false);
    editor_redo(e);
    shader_frame(e);
    expect_vector(e, parameter, vector3, 3, false);

    select_node(e, constant);
    choose_type(e, 3);
    snprintf(control, sizeof control, "vector.%llu.1.0", (unsigned long long)constant);
    const float vector4[] = { 0.125f, 0.375f, 1.75f, 0.625f };
    const char *text[] = { "0.125", "0.375", "1.75", "0.625" };
    for (int i = 0; i < 4; i++) {
        enter_component(e, control, 4, i, text[i]);
    }
    expect_vector(e, constant, vector4, 4, false);
    expect_same_type(e, constant, 3, vector4, 4, false);
    choose_type(e, 4);
    expect_vector(e, constant, vector4, 3, true);
    expect_same_type(e, constant, 4, vector4, 3, true);
    snprintf(control, sizeof control, "vector.details.%llu.1.0", (unsigned long long)constant);
    click(e, control);
    shader_frame(e);
    item("color.picker");
    capture("shader-values-color-picker");
    click(e, "color.done");
    expect_vector(e, constant, vector4, 3, true);
    click(e, control);
    shader_frame(e);
    editor_ui_rect picker = item("color.picker");
    click_at(e, picker.x + picker.width * 0.28f, picker.y + picker.height * 0.25f);
    click(e, "color.done");
    float changed[4] = { 0 };
    memcpy(changed, output(e, constant)->default_value.ptr, 3 * sizeof(float));
    assert(
        fabsf(changed[0] - vector4[0]) + fabsf(changed[1] - vector4[1]) +
            fabsf(changed[2] - vector4[2]) >
        0.01f
    );
    editor_undo(e);
    shader_frame(e);
    expect_vector(e, constant, vector4, 3, true);
    editor_redo(e);
    shader_frame(e);
    expect_vector(e, constant, changed, 3, true);
    select_node(e, constant);
    choose_type(e, 3);
    expect_vector(e, constant, changed, 4, false);
    choose_type(e, 4);
    expect_vector(e, constant, changed, 3, true);

    char path[] = "/tmp/shader-ui-values-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0 && close(descriptor) == 0);
    assert(editor_save(e, path) == 0);
    assert(editor_load(e, path) == 0);
    shader_frame(e);
    expect_vector(e, parameter, vector3, 3, false);
    expect_vector(e, constant, changed, 3, true);
    assert(unlink(path) == 0);
}

static int preview_pixel(const CF_Pixel *pixels, editor_ui_rect rect, float u, float v)
{
    int width = cf_app_get_canvas_width(), height = cf_app_get_canvas_height();
    ImVec2 screen = ImGui_GetIO()->DisplaySize;
    int x = (int)((rect.x + rect.width * u) * width / screen.x);
    int y = (int)((rect.y + rect.height * v) * height / screen.y);
    assert(x >= 0 && x < width && y >= 0 && y < height);
    CF_Pixel pixel = pixels[y * width + x];
    return (pixel.colors.r + pixel.colors.g + pixel.colors.b) / 3;
}

static CF_Pixel *canvas_pixels()
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    assert(readback.id);
    int size = cf_readback_size(readback);
    assert(size == cf_app_get_canvas_width() * cf_app_get_canvas_height() * (int)sizeof(CF_Pixel));
    CF_Pixel *pixels = malloc((size_t)size);
    assert(pixels);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    assert(cf_readback_data(readback, pixels, size) == size);
    cf_destroy_readback(readback);
    return pixels;
}

static void surface_preview_pixels()
{
    editor_ui_rect rect = item("shader.preview");
    CF_Pixel *pixels = canvas_pixels();
    int center = preview_pixel(pixels, rect, 0.5f, 0.5f);
    int background = preview_pixel(pixels, rect, 0.08f, 0.08f);
    int min = 255, max = 0;
    const float positions[][2] = { { 0.32f, 0.5f },
                                   { 0.68f, 0.5f },
                                   { 0.5f, 0.32f },
                                   { 0.5f, 0.68f } };
    for (int i = 0; i < 4; i++) {
        int light = preview_pixel(pixels, rect, positions[i][0], positions[i][1]);
        min = light < min ? light : min;
        max = light > max ? light : max;
    }
    assert(center > background + 30 && max > min + 10);
    free(pixels);
}

static int preview_center()
{
    CF_Pixel *pixels = canvas_pixels();
    int value = preview_pixel(pixels, item("shader.preview"), 0.5f, 0.5f);
    free(pixels);
    return value;
}

static int ramp_stops(editor *e, uint64_t id, shader_ramp_stop stops[SHADER_RAMP_MAX])
{
    node *n = graph_node_by_id(editor_active_graph(e), id);
    int count = shader_node_ramp_get(n, stops);
    assert(shader_ramp_valid(stops, count));
    return count;
}

static void select_node(editor *e, uint64_t id);

static double monotonic_ms()
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static int compare_double(const void *a, const void *b)
{
    const double left = *(const double *)a, right = *(const double *)b;
    return left < right ? -1 : left > right;
}

static double percentile(double values[60], double fraction)
{
    double sorted[60];
    memcpy(sorted, values, sizeof sorted);
    qsort(sorted, 60, sizeof *sorted, compare_double);
    int index = (int)(fraction * 59.0 + 0.5);
    return sorted[index];
}

static void ramp_gesture_baseline(editor *e, uint64_t ramp_id)
{
    shader_ramp_stop before[SHADER_RAMP_MAX];
    assert(ramp_stops(e, ramp_id, before) == 3);
    editor_ui_rect strip = item("shader.ramp.strip");
    unsigned compile_before = app_scene_renderer_shader_compile_count();
    for (int frame = 0; frame < 30; frame++) {
        shader_frame(e);
    }
    unsigned compile_after_warmup = app_scene_renderer_shader_compile_count();
    int before_pixel = preview_center();
    double samples[60];
    float y = strip.y + strip.height - 5.0f;
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * .7f, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    for (int frame = 0; frame < 60; frame++) {
        float progress = (float)(frame + 1) / 60.0f;
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * (.7f + .2f * progress), y);
        double start = monotonic_ms();
        shader_frame(e);
        samples[frame] = monotonic_ms() - start;
    }
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    shader_frame(e);
    int after_pixel = preview_center();
    unsigned compile_after_gesture = app_scene_renderer_shader_compile_count();
    assert(before_pixel != after_pixel);
    assert(compile_after_gesture == compile_after_warmup);
    printf(
        "shader ramp gesture: compiles=%u warmup_delta=%u gesture_delta=%u "
        "warmup=30 gesture=60 pixel_delta=%d p50=%.3fms p95=%.3fms\n",
        compile_after_gesture - compile_before,
        compile_after_warmup - compile_before,
        compile_after_gesture - compile_after_warmup,
        after_pixel - before_pixel,
        percentile(samples, .50),
        percentile(samples, .95)
    );
    editor_undo(e);
    select_node(e, ramp_id);
    shader_ramp_stop after[SHADER_RAMP_MAX];
    assert(ramp_stops(e, ramp_id, after) == 3);
    assert(!memcmp(before, after, 3 * sizeof *before));
}

static void inline_ramp_is_editable(editor *e, uint64_t ramp_id)
{
    shader_ramp_stop before[SHADER_RAMP_MAX], after[SHADER_RAMP_MAX];
    assert(ramp_stops(e, ramp_id, before) == 3);
    char strip_id[96];
    snprintf(strip_id, sizeof strip_id, "shader.node.%llu.shader.ramp.strip", (unsigned long long)ramp_id);
    assert(editor_ui_item_rect(strip_id, &(editor_ui_rect){ 0 }));
    editor_ui_rect strip = item(strip_id);
    char control[128];
    snprintf(control, sizeof control, "shader.node.%llu.shader.ramp.position", (unsigned long long)ramp_id);
    item(control);
    snprintf(control, sizeof control, "shader.node.%llu.shader.ramp.color", (unsigned long long)ramp_id);
    item(control);
    float y = strip.y + strip.height - 5.0f;
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * .5f, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    shader_frame(e);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * .62f, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, after) == 3);
    assert(fabsf(after[1].position - .62f) < .02f);
    editor_undo(e);
    select_node(e, ramp_id);
    assert(ramp_stops(e, ramp_id, after) == 3);
    assert(!memcmp(before, after, 3 * sizeof *before));
}

static void inline_ramp_selection_is_per_node(editor *e, uint64_t first_id)
{
    graph *g = e->active_graph;
    node *first = graph_node_by_id(g, first_id);
    node *second = graph_add_node(g, registry_node_by_name("Shader Color Ramp"));
    assert(first && second);
    second->x = first->x + 360.0f;
    second->y = first->y;
    const shader_ramp_stop stops[] = { { 0, { 0, 0, 0, 1 } }, { 1, { 1, 1, 1, 1 } } };
    assert(shader_node_ramp_set(g, second, stops, 2));

    uint64_t second_id = second->id;
    select_node(e, second_id);
    char strip_id[96];
    snprintf(strip_id, sizeof strip_id, "shader.node.%llu.shader.ramp.strip", (unsigned long long)second_id);
    editor_ui_rect strip = item(strip_id);
    click_at(e, strip.x + strip.width * .98f, strip.y + strip.height - 5.0f);
    assert(e->shader_ramp_node == second_id && e->shader_ramp_stop == 1);
    shader_frame(e);
    assert(e->shader_ramp_node == second_id && e->shader_ramp_stop == 1);

    select_node(e, first_id);
    shader_frame(e);
    assert(e->shader_ramp_node == second_id && e->shader_ramp_stop == 1);

    assert(graph_remove_node(g, second) == 0);
    e->shader_ramp_node = first_id;
    e->shader_ramp_stop = 0;
    select_node(e, first_id);
    shader_frame(e);
}

static void choose_interpolation(editor *e, int choice)
{
    click(e, "shader.setting.2");
    shader_frame(e);
    char id[64];
    snprintf(id, sizeof id, "shader.setting.2.%d", choice);
    click(e, id);
    shader_frame(e);
}

static void ramp_settings_are_private(editor *e, uint64_t ramp, uint64_t source)
{
    graph *g = e->active_graph;
    node *n = graph_node_by_id(g, ramp), *from = graph_node_by_id(g, source);
    editor_pin_ui pin;
    assert(editor_query_pin_ui(e, n->input_pins[0].id, &pin));
    assert(!editor_query_pin_ui(e, n->input_pins[1].id, &pin));
    assert(!editor_query_pin_ui(e, n->input_pins[2].id, &pin));
    assert(pin_types_compatible(&from->output_pins[0], &n->input_pins[2]));
    int edges = acount(g->edges);
    assert(graph_connect_data(g, from, 0, n, 2) != 0);
    assert(acount(g->edges) == edges);
}

static void ramp_action(editor *e, const char *id)
{
    click(e, "shader.ramp.tools");
    shader_frame(e);
    click(e, id);
    shader_frame(e);
}

static void ramp_stop_controls(editor *e, uint64_t id)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    editor_ui_rect strip = item("shader.ramp.strip");
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiMod_Ctrl, true);
    shader_frame(e);
    click_at(e, strip.x + 5 + (strip.width - 10) * .25f, strip.y + 4);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiMod_Ctrl, false);
    shader_frame(e);
    assert(
        ramp_stops(e, id, stops) == 4 &&
        fabsf(stops[1].position - .25f) <= 1.0f / (strip.width - 10)
    );
    assert(fabsf(stops[1].color[0] - stops[1].position * 2.5f) < .001f);
    editor_undo(e);
    shader_frame(e);
    assert(ramp_stops(e, id, stops) == 3);
    editor_redo(e);
    shader_frame(e);
    assert(ramp_stops(e, id, stops) == 4);
    editor_undo(e);
    select_node(e, id);

    int history = acount(e->undo);
    enter_component(e, "shader.ramp.stop", 1, 0, "2");
    assert(e->shader_ramp_stop == 2 && acount(e->undo) == history);
    click(e, "shader.ramp.add");
    assert(ramp_stops(e, id, stops) == 4 && stops[2].position == .75f);
    editor_undo(e);
    select_node(e, id);
    enter_component(e, "shader.ramp.stop", 1, 0, "0");
    ramp_action(e, "shader.ramp.flip");
    assert(ramp_stops(e, id, stops) == 3);
    assert(stops[0].color[0] == 2.5f && stops[2].color[0] == 0 && e->shader_ramp_stop == 2);
    editor_undo(e);
    select_node(e, id);
    assert(ramp_stops(e, id, stops) == 3 && stops[0].color[0] == 0);
    enter_component(e, "shader.ramp.stop", 1, 0, "1");
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, false);
    select_node(e, id);
    ed_clear_selection(e);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), 20, 10);
    for (int i = 0; i < 18; i++) {
        shader_frame(e);
    }
    capture("shader-ramp-blender");
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
    select_node(e, id);
}

static void ramp_numeric_position(editor *e, uint64_t id)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    int count = ramp_stops(e, id, stops);
    float before = stops[1].position;
    ImGuiKey modifier = ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super
                                                             : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, true);
    shader_frame(e);
    click(e, "shader.ramp.position");
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, false);
    shader_frame(e);
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "2");
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, true);
    shader_frame(e);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Enter, false);
    shader_frame(e);
    assert(
        ramp_stops(e, id, stops) == count && stops[1].position > .99f &&
        stops[1].position < 1
    );
    editor_undo(e);
    select_node(e, id);
    assert(ramp_stops(e, id, stops) == count && stops[1].position == before);
}

static void ramp_tools_history(editor *e, uint64_t id)
{
    shader_ramp_stop before[SHADER_RAMP_MAX], stops[SHADER_RAMP_MAX];
    int count = ramp_stops(e, id, before);
    ramp_action(e, "shader.ramp.even");
    assert(ramp_stops(e, id, stops) == count);
    for (int i = 0; i < count; i++) {
        assert(stops[i].position == (float)i / (count - 1));
        assert(!memcmp(stops[i].color, before[i].color, sizeof stops[i].color));
    }
    editor_undo(e);
    select_node(e, id);
    assert(
        ramp_stops(e, id, stops) == count &&
        !memcmp(stops, before, (size_t)count * sizeof *stops)
    );
    ramp_action(e, "shader.ramp.left");
    assert(
        ramp_stops(e, id, stops) == count &&
        stops[count - 1].position == (float)(count - 1) / count
    );
    editor_undo(e);
    select_node(e, id);
    ramp_action(e, "shader.ramp.reset");
    assert(ramp_stops(e, id, stops) == 2 && stops[1].color[0] == 1);
    assert(graph_node_by_id(e->active_graph, id)->input_pins[2].default_value.i == 0);
    editor_undo(e);
    select_node(e, id);
    assert(
        ramp_stops(e, id, stops) == count &&
        !memcmp(stops, before, (size_t)count * sizeof *stops)
    );
    assert(graph_node_by_id(e->active_graph, id)->input_pins[2].default_value.i == 2);
    editor_redo(e);
    select_node(e, id);
    assert(ramp_stops(e, id, stops) == 2 && stops[1].color[0] == 1);
    editor_undo(e);
    select_node(e, id);
}

static void shader_ramp_ui(editor *e)
{
    behavior_asset *asset = behavior_asset_new("Ramp authoring");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *surface = shader_graph_add_output(g, false);
    node *ramp = graph_add_node(g, registry_node_by_name("Shader Color Ramp"));
    node *source = graph_add_node(g, registry_node_by_name("Shader Constant"));
    assert(ramp && source);
    uint64_t ramp_id = ramp->id, source_id = source->id;
    const shader_ramp_stop initial[] = { { 0, { 0, 0, 0, 1 } },
                                         { 1, { 2.5f, 0.5f, 1.25f, 0.75f } } };
    assert(shader_node_ramp_set(g, ramp, initial, 2));
    assert(graph_set_pin_default(g, ramp, 0, 0, &(pin_value){ .type = PIN_F32, .f = 0.25f }) == 0);
    assert(
        graph_set_pin_default(g, surface, 0, 6, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0
    );
    assert(graph_set_pin_type(g, source, 1, 0, PIN_I32, NULL) == 0);
    assert(graph_connect_data(g, ramp, 0, surface, 0) == 0);
    ramp->show_advanced = true;
    assert(editor_open_behavior_asset(e, asset, g->graph_id) == 0);
    behavior_asset_free(asset);
    editor_set_graph_visible(e, true);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    select_node(e, ramp_id);
    ramp_settings_are_private(e, ramp_id, source_id);

    shader_ramp_stop stops[SHADER_RAMP_MAX];
    assert(ramp_stops(e, ramp_id, stops) == 2);
    unsigned compile_before_add = app_scene_renderer_shader_compile_count();
    click(e, "shader.ramp.add");
    unsigned compile_after_add = app_scene_renderer_shader_compile_count();
    assert(compile_after_add > compile_before_add);
    assert(ramp_stops(e, ramp_id, stops) == 3);
    assert(stops[1].position == 0.5f && stops[1].color[0] == 1.25f);
    ramp_stop_controls(e, ramp_id);
    ramp_gesture_baseline(e, ramp_id);
    inline_ramp_is_editable(e, ramp_id);
    inline_ramp_selection_is_per_node(e, ramp_id);
    editor_ui_rect strip = item("shader.ramp.strip");
    float y = strip.y + strip.height - 5;
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * 0.5f, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    shader_frame(e);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), strip.x + strip.width * 0.7f, y);
    shader_frame(e);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 3);
    assert(fabsf(stops[1].position - 0.7f) < 0.015f);
    float moved = stops[1].position;
    editor_undo(e);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].position == 0.5f);
    editor_redo(e);
    select_node(e, ramp_id);
    assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].position == moved);

    for (int right = 0; right < 2; right++) {
        editor_ui_rect position = item("shader.ramp.position");
        float y = position.y + position.height * .5f;
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), position.x + position.width * .3f, y);
        shader_frame(e);
        ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
        shader_frame(e);
        ImGuiIO_AddMousePosEvent(ImGui_GetIO(), position.x + (right ? position.width + 50 : -50), y);
        shader_frame(e);
        ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
        shader_frame(e);
        assert(ramp_stops(e, ramp_id, stops) == 3);
        assert(stops[1].position > 0 && stops[1].position < 1);
        assert(right ? stops[1].position > .99f : stops[1].position < .01f);
        editor_undo(e);
        select_node(e, ramp_id);
        assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].position == moved);
    }

    float hdr[4];
    memcpy(hdr, stops[1].color, sizeof hdr);
    click(e, "shader.ramp.color");
    shader_frame(e);
    item("color.picker");
    click(e, "color.done");
    assert(ramp_stops(e, ramp_id, stops) == 3);
    assert(!memcmp(hdr, stops[1].color, sizeof hdr));
    click(e, "shader.ramp.color");
    shader_frame(e);
    editor_ui_rect picker = item("color.picker");
    capture("shader-ramp-color-picker");
    enter_color_component(e, 2, "2.5");
    enter_color_component(e, 3, "0.25");
    click(e, "color.done");
    assert(ramp_stops(e, ramp_id, stops) == 3);
    assert(stops[1].color[3] == 0.25f);
    assert(stops[1].color[0] > 1 || stops[1].color[1] > 1 || stops[1].color[2] > 1);
    editor_undo(e);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 3 && !memcmp(hdr, stops[1].color, sizeof hdr));
    editor_redo(e);
    select_node(e, ramp_id);
    assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].color[3] == 0.25f);
    float edited[4];
    memcpy(edited, stops[1].color, sizeof edited);
    click(e, "shader.ramp.color");
    shader_frame(e);
    picker = item("color.picker");
    click_at(e, picker.x + picker.width * 0.28f, picker.y + picker.height * 0.25f);
    click(e, "color.done");
    assert(ramp_stops(e, ramp_id, stops) == 3);
    float changed[4];
    memcpy(changed, stops[1].color, sizeof changed);
    assert(memcmp(hdr, changed, sizeof hdr));
    editor_undo(e);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 3 && !memcmp(edited, stops[1].color, sizeof edited));
    editor_redo(e);
    select_node(e, ramp_id);
    assert(ramp_stops(e, ramp_id, stops) == 3 && !memcmp(changed, stops[1].color, sizeof changed));

    int linear = preview_center();
    unsigned compile_before_interpolation = app_scene_renderer_shader_compile_count();
    choose_interpolation(e, 1);
    unsigned compile_after_interpolation = app_scene_renderer_shader_compile_count();
    assert(compile_after_interpolation > compile_before_interpolation);
    assert(
        graph_node_by_id(editor_active_graph(e), ramp_id)
            ->input_pins[2]
            .default_value.i == 1
    );
    int constant = preview_center();
    assert(linear > constant + 20);
    editor_undo(e);
    shader_frame(e);
    assert(
        graph_node_by_id(editor_active_graph(e), ramp_id)
            ->input_pins[2]
            .default_value.i == 0
    );
    editor_redo(e);
    select_node(e, ramp_id);
    assert(
        graph_node_by_id(editor_active_graph(e), ramp_id)
            ->input_pins[2]
            .default_value.i == 1
    );
    choose_interpolation(e, 2);
    int history = acount(e->undo);
    choose_interpolation(e, 2);
    assert(acount(e->undo) == history);

    click(e, "shader.ramp.remove");
    assert(ramp_stops(e, ramp_id, stops) == 2);
    editor_undo(e);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].position == moved);
    assert(!memcmp(changed, stops[1].color, sizeof changed));
    editor_redo(e);
    shader_frame(e);
    assert(ramp_stops(e, ramp_id, stops) == 2);
    editor_undo(e);
    select_node(e, ramp_id);

    char path[] = "/tmp/shader-ui-ramp-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0 && close(descriptor) == 0);
    assert(editor_save(e, path) == 0 && editor_load(e, path) == 0);
    select_node(e, ramp_id);
    assert(ramp_stops(e, ramp_id, stops) == 3 && stops[1].position == moved);
    assert(!memcmp(changed, stops[1].color, sizeof changed));
    assert(
        graph_node_by_id(editor_active_graph(e), ramp_id)
            ->input_pins[2]
            .default_value.i == 2
    );
    ramp_settings_are_private(e, ramp_id, source_id);
    assert(unlink(path) == 0);
    ramp_numeric_position(e, ramp_id);
    ramp_tools_history(e, ramp_id);
    capture("shader-ramp-editor");
}

static void shader_spawn_defaults(editor *e)
{
    behavior_asset *asset = behavior_asset_new("Spawn defaults");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    assert(editor_open_behavior_asset(e, asset, asset->graph->graph_id) == 0);
    behavior_asset_free(asset);
    node *surface = editor_spawn(e, registry_node_by_name("Shader Surface Output"), 0, 0);
    node *mapping = editor_spawn(e, registry_node_by_name("Shader Mapping3D"), 0, 0);
    assert(surface && mapping);
    assert(graph_connect_data(e->active_graph, mapping, 0, surface, 0) == 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(e->active_graph, &ir, &error));
    shader_sample_context sample = { .object_position = { .25f, .5f, .75f } };
    float values[SHADER_OUTPUT_MAX][4];
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, values, &error));
    for (int i = 0; i < 3; i++) {
        assert(fabsf(values[0][i] - sample.object_position[i]) < .00001f);
    }
    shader_graph_free(&ir);
}

static void context_result(editor *e, const float expected[3])
{
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(e->active_graph, &ir, &error));
    shader_sample_context sample = { .object_position = { .25f, .5f, .75f } };
    float values[SHADER_OUTPUT_MAX][4];
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, values, &error));
    for (int i = 0; i < 3; i++) {
        assert(fabsf(values[0][i] - expected[i]) < .00001f);
    }
    shader_graph_free(&ir);
}

static void shader_context_ui(editor *e)
{
    node *mapping = NULL;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        if (!strcmp(e->active_graph->nodes[i]->desc->name, "Shader Mapping3D")) {
            mapping = e->active_graph->nodes[i];
        }
    }
    assert(mapping);
    uint64_t id = mapping->id;
    select_node(e, id);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, false);
    shader_frame(e);
    char context[96], choice[112], numeric[96], label[96];
    snprintf(context, sizeof context, "shader.context.node.%llu.0", (unsigned long long)id);
    snprintf(numeric, sizeof numeric, "vector.%llu.0.0", (unsigned long long)id);
    snprintf(label, sizeof label, "shader.context.node.%llu.0", (unsigned long long)id);
    item(label);
    editor_ui_rect rect;
    assert(!editor_ui_item_rect(numeric, &rect));
    editor_node_ui before, after;
    assert(editor_query_node_ui(e, id, &before));
    click(e, context);
    shader_frame(e);
    snprintf(choice, sizeof choice, "%s.value", context);
    click(e, choice);
    shader_frame(e);
    context_result(e, (float[]){ 0, 0, 0 });
    enter_component(e, numeric, 3, 0, "0.125");
    context_result(e, (float[]){ .125f, 0, 0 });
    assert(editor_query_node_ui(e, id, &after));
    assert(fabsf((before.right - before.left) - (after.right - after.left)) < .1f);
    assert(after.bottom - after.top > before.bottom - before.top);
    click(e, context);
    shader_frame(e);
    snprintf(choice, sizeof choice, "%s.context", context);
    click(e, choice);
    shader_frame(e);
    context_result(e, (float[]){ .25f, .5f, .75f });
    editor_undo(e);
    select_node(e, id);
    context_result(e, (float[]){ .125f, 0, 0 });
    editor_redo(e);
    select_node(e, id);
    context_result(e, (float[]){ .25f, .5f, .75f });
    node *source = editor_spawn(e, registry_node_by_name("Shader ObjectPosition"), 0, 0);
    assert(source);
    mapping = graph_node_by_id(e->active_graph, id);
    assert(graph_connect_data(e->active_graph, source, 0, mapping, 0) == 0);
    shader_frame(e);
    assert(editor_query_node_ui(e, id, &before));
    assert(graph_remove_edge(e->active_graph, source, 0, mapping, 0) == 0);
    shader_frame(e);
    assert(editor_query_node_ui(e, id, &after));
    assert(fabsf((before.right - before.left) - (after.right - after.left)) < .1f);
    assert(after.bottom - after.top > before.bottom - before.top);
    context_result(e, (float[]){ .25f, .5f, .75f });
    assert(!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR));
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
}

static void write_image_fixture(const char *path, unsigned char gray)
{
    CF_Pixel pixels[4];
    for (int i = 0; i < 4; i++) {
        pixels[i] = (CF_Pixel){ .colors = { gray, gray, gray, 128 } };
    }
    CF_Image image = { .w = 2, .h = 2, .pix = pixels };
    void *bytes;
    int size;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &bytes, &size)));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    cf_free(bytes);
}

static void shader_image_ui(editor *e)
{
    char directory[] = "content/shader-image-ui-XXXXXX";
    assert(mkdtemp(directory));
    char path[256];
    snprintf(path, sizeof path, "%s/Gray.png", directory);
    write_image_fixture(path, 128);
    behavior_asset *asset = behavior_asset_new("Image authoring");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *surface = shader_graph_add_output(g, false);
    node *image = graph_add_node(g, registry_node_by_name("Shader Image Texture"));
    assert(image && shader_graph_init_node(g, image));
    uint64_t id = image->id;
    assert(
        graph_set_pin_default(
            g,
            image,
            0,
            SHADER_IMAGE_INPUT_IMAGE,
            &(pin_value){ .type = PIN_STR, .str = path }
        ) == 0
    );
    assert(
        graph_set_pin_default(g, surface, 0, 6, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0
    );
    assert(graph_connect_data(g, image, 0, surface, 0) == 0);
    assert(editor_open_behavior_asset(e, asset, g->graph_id) == 0);
    behavior_asset_free(asset);
    select_node(e, id);
    item("shader.image.dimensions");
    editor_ui_rect rect;
    assert(!editor_ui_item_rect("shader.image.error", &rect));
    assert(!editor_ui_item_rect("shader.backend_error", &rect));
    int srgb = preview_center();
    choose_interpolation(e, SHADER_IMAGE_LINEAR); /* Shared setting selector at input 2. */
    int linear = preview_center();
    assert(linear > srgb + 20);
    editor_undo(e);
    select_node(e, id);
    assert(abs(preview_center() - srgb) < 3);
    editor_redo(e);
    select_node(e, id);
    assert(abs(preview_center() - linear) < 3);
    for (int setting = SHADER_IMAGE_INPUT_FILTER; setting <= SHADER_IMAGE_INPUT_WRAP;
         setting++) {
        char control[64];
        snprintf(control, sizeof control, "shader.setting.%d", setting);
        click(e, control);
        shader_frame(e);
        int value = setting == SHADER_IMAGE_INPUT_FILTER ? SHADER_IMAGE_NEAREST
                                                         : SHADER_IMAGE_CLAMP;
        snprintf(control, sizeof control, "shader.setting.%d.%d", setting, value);
        click(e, control);
        image = graph_node_by_id(e->active_graph, id);
        assert(image->input_pins[setting].default_value.i == value);
    }
    click(e, "shader.image.select");
    shader_frame(e);
    item("shader.image.empty"); /* An isolated graph editor has no scene image catalog. */
    int history = acount(e->undo);
    enter_component(e, "shader.image.path", 1, 0, path);
    assert(acount(e->undo) == history);

    write_image_fixture(path, 224);
    click(e, "shader.image.reload");
    shader_frame(e);
    int replacement = preview_center();
    assert(replacement > linear + 15);
    FILE *invalid = fopen(path, "wb");
    assert(invalid && fputs("invalid PNG", invalid) >= 0 && fclose(invalid) == 0);
    click(e, "shader.image.reload");
    shader_frame(e);
    item("shader.image.error");
    assert(abs(preview_center() - replacement) < 3);
    write_image_fixture(path, 128);
    click(e, "shader.image.reload");
    shader_frame(e);
    assert(!editor_ui_item_rect("shader.image.error", &rect));
    assert(abs(preview_center() - linear) < 3);
    char graph_path[256];
    snprintf(graph_path, sizeof graph_path, "%s/Material.json", directory);
    assert(editor_save(e, graph_path) == 0 && editor_load(e, graph_path) == 0);
    select_node(e, id);
    image = graph_node_by_id(e->active_graph, id);
    assert(!strcmp(image->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str, path));
    assert(image->input_pins[SHADER_IMAGE_INPUT_COLOR_SPACE].default_value.i == SHADER_IMAGE_LINEAR);
    assert(image->input_pins[SHADER_IMAGE_INPUT_FILTER].default_value.i == SHADER_IMAGE_NEAREST);
    assert(image->input_pins[SHADER_IMAGE_INPUT_WRAP].default_value.i == SHADER_IMAGE_CLAMP);
    assert(abs(preview_center() - linear) < 3);
    capture("shader-image-editor");
    assert(unlink(graph_path) == 0 && unlink(path) == 0 && rmdir(directory) == 0);
}

static void shader_authoring_ui(editor *e)
{
    behavior_asset *asset = behavior_asset_new("Surface authoring");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    uint64_t surface = shader_graph_add_output(asset->graph, false)->id;
    uint64_t parameter = graph_add_node(asset->graph, registry_node_by_name("Shader Parameter"))
                             ->id;
    uint64_t constant = graph_add_node(asset->graph, registry_node_by_name("Shader Constant"))
                            ->id;
    const char *readonly[] = { "Shader Normal", "Shader WorldPosition", "Shader Vector3" };
    uint64_t readonly_ids[3];
    for (int i = 0; i < 3; i++) {
        readonly_ids[i] = graph_add_node(asset->graph, registry_node_by_name(readonly[i]))
                              ->id;
    }
    assert(editor_open_behavior_asset(e, asset, asset->graph->graph_id) == 0);
    behavior_asset_free(asset);
    editable_shader_values(e, parameter, constant);
    for (int i = 0; i < 3; i++) {
        select_node(e, readonly_ids[i]);
        editor_pin_ui pin;
        assert(editor_query_pin_ui(e, output(e, readonly_ids[i])->id, &pin));
        editor_ui_rect rect;
        assert(!editor_ui_item_rect("shader.parameter.type", &rect));
        char id[96];
        snprintf(id, sizeof id, "vector.%llu.1.0", (unsigned long long)readonly_ids[i]);
        assert(!editor_ui_item_rect(id, &rect));
        snprintf(id, sizeof id, "inspector.output.%llu.0", (unsigned long long)readonly_ids[i]);
        if (!editor_ui_item_rect(id, &rect)) {
            click(e, "inspector.outputs");
        }
        item(id);
        snprintf(id, sizeof id, "vector.details.%llu.1.0", (unsigned long long)readonly_ids[i]);
        assert(!editor_ui_item_rect(id, &rect));
    }
    select_node(e, surface);
    char normal[96];
    snprintf(normal, sizeof normal, "vector.details.%llu.0.4", (unsigned long long)surface);
    enter_component(e, normal, 3, 2, "1");
    assert(
        ((float *)graph_node_by_id(editor_active_graph(e), surface)
             ->input_pins[4]
             .default_value.ptr)[2] == 1
    );
    editor_undo(e);
    shader_frame(e);
    assert(
        ((float *)graph_node_by_id(editor_active_graph(e), surface)
             ->input_pins[4]
             .default_value.ptr)[2] == 0
    );
    item("shader.preview");
    editor_ui_rect error;
    assert(!editor_ui_item_rect("shader.error", &error));
    assert(!editor_ui_item_rect("shader.backend_error", &error));
    cf_draw3d_stats();
    shader_frame(e);
    assert(cf_draw3d_stats().instances > 0);
    surface_preview_pixels();
    capture("shader-surface-editor");
}

/* Focused native drawing regression. Uses the real Cute/ImGui frame and GPU preview. */
int shader_ui_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Shader UI regression",
        0,
        0,
        0,
        2056,
        1286,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Shader UI app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    cf_app_init_imgui();
    app_input_initialize();
    editor_ui_initialize(false);
    ImGui_GetIO()->IniFilename = NULL;
    cf_app_set_imgui_render_target(cf_app_get_canvas());
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(2056, 1286);
    assert(sizeof(ImDrawIdx) == 4);
    shader_scene_ui();
    shader_image_files_ui();
    behavior_asset *library = read_library();
    const behavior_asset *catalog[] = { library };
    editor *e = editor_new(behavior_asset_new("Before shader"));
    assert(e);
    editor_set_inspector_active(e, true);
    editor_set_graph_visible(e, 1);
    editor_set_ui_context(e, EDITOR_UI_CONTEXT_BEHAVIOR);
    const char *paths[] = {
        "test/fixtures/content/materials/PulseRing.json",
        "test/fixtures/content/materials/OutlineGradient.json",
        "test/fixtures/content/shapes/SmoothUnion.json"
    };
    for (int frame = 0; frame < 150; frame++) {
        cf_app_update(NULL);
        app_input_frame();
        editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
        if (frame == 20 || frame == 60 || frame == 100) {
            int index = (frame - 20) / 40;
            assert(editor_load(e, paths[index]) == 0);
            editor_set_asset_catalog(e, catalog, 1);
        }
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
        editor_set_inspector_active(e, true);
        editor_set_ui_context(e, EDITOR_UI_CONTEXT_BEHAVIOR);
        if (frame == 24 || frame == 64 || frame == 104) {
            ed_zoom_fit(e, false);
        }
        editor_tick(e, frame / 60.0);
        editor_prepare_docking();
        editor_draw(e);
        char *snapshot = behavior_asset_save_json(e->asset);
        assert(snapshot);
        free(snapshot);
        cf_app_draw_onto_screen(true);
        if (frame == 59 || frame == 99 || frame == 139) {
            assert(e->shader_preview && e->shader_viewport);
            char name[128];
            snprintf(name, sizeof name, "shader-ui-native-%d", (frame - 59) / 40);
            capture(name);
        }
    }
    shader_spawn_defaults(e);
    shader_context_ui(e);
    shader_authoring_ui(e);
    shader_ramp_ui(e);
    shader_image_ui(e);
    editor_free(e);
    behavior_asset_free(library);
    app_input_shutdown();
    cf_destroy_app();
    puts("shader UI PASS: Files opens shader Inspector immediately, vector/color/ramp authoring, private settings, Undo/Redo/save/reload and rendered Surface updates");
    return 0;
}
