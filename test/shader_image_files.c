#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "runtime/generated.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} image_files_fixture;

static void frame(image_files_fixture *f)
{
    cf_app_update(NULL);
    app_input_frame();
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    editor_tick(f->graph, f->graph->monotonic_time + 1.0 / 60);
    editor_prepare_docking();
    app_scene_authoring_draw(f->scene);
    editor_draw(f->graph);
    cf_app_draw_onto_screen(true);
}

static editor_ui_rect item(const char *id)
{
    editor_ui_rect rect;
    if (!editor_ui_item_rect(id, &rect)) {
        fprintf(stderr, "Image Files UI item not visible: %s\n", id);
        assert(!"Required image asset control is not visible");
    }
    return rect;
}

static void click(image_files_fixture *f, const char *id)
{
    editor_ui_rect rect = item(id);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), rect.x + rect.width * .5f, rect.y + rect.height * .5f);
    frame(f);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    frame(f);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    frame(f);
}

static void write_png(const char *path)
{
    CF_Pixel pixels[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        pixels[i].colors.r = i & 1 ? 255 : 32;
        pixels[i].colors.g = i & 2 ? 255 : 64;
        pixels[i].colors.b = 128;
        pixels[i].colors.a = 255;
    }
    CF_Image image = { 2, 2, pixels };
    void *data;
    int size;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &data, &size)));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    cf_free(data);
}

static image_files_fixture fixture(const char *root)
{
    image_files_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new(
        "Image Files",
        behavior_asset_new("Image Files")
    );
    assert(game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("Image Files"));
    f.runtime = engine_new(f.world, f.live);
    f.scene = app_scene_authoring_new(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        NULL,
        NULL,
        f.runtime
    );
    assert(f.scene && app_scene_authoring_set_content_root(f.scene, root) == 0);
    editor_ui_reset_layout();
    editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, false);
    return f;
}

static void open_graph(image_files_fixture *f, behavior_domain domain)
{
    char name[80];
    snprintf(name, sizeof name, "Image Files domain %d", domain);
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = domain;
    graph *g = asset->graph;
    if (domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE) {
        assert(shader_graph_add_output(g, domain == BEHAVIOR_DOMAIN_SHAPE));
    }
    if (domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        g = behavior_asset_add_function(asset, "Image Pattern", true);
        assert(g);
    }
    assert(editor_open_behavior_asset(f->graph, asset, g->graph_id) == 0);
    behavior_asset_free(asset);
    app_scene_authoring_set_editor_working(f->scene, editor_behavior_asset(f->graph));
    scene_authoring_select_document(f->scene, APP_EDITOR_DOCUMENT_BEHAVIOR, name);
    editor_ui_request_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER);
    for (int i = 0; i < 8; i++) {
        frame(f);
    }
}

static node *image_node(editor *e)
{
    node *found = NULL;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (n->desc == registry_node_by_name("Shader Image Texture")) {
            assert(!found);
            found = n;
        }
    }
    return found;
}

static void expect_path(editor *e, const char *path)
{
    node *n = image_node(e);
    assert(n && !strcmp(n->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str, path));
}

static void drag_file(image_files_fixture *f, const char *path, bool inspector)
{
    editor_ui_request_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER);
    for (int i = 0; i < 3; i++) {
        frame(f);
    }
    editor_ui_rect source;
    if (!editor_ui_item_rect(path, &source)) {
        click(f, "content.folders");
        frame(f);
        source = item(path);
    }
    ImVec2 start = { source.x + source.width * .5f, source.y + source.height * .5f };
    ImVec2 destination = { f->graph->canvas_min.x + f->graph->canvas_size.x * .7f,
                           f->graph->canvas_min.y + f->graph->canvas_size.y * .65f };
    if (inspector) {
        editor_ui_rect target = item("shader.image.select");
        destination = (ImVec2){ target.x + target.width * .5f,
                                target.y + target.height * .5f };
    }
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), start.x, start.y);
    frame(f);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    frame(f);
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), start.x + 16, start.y + 12);
    frame(f);
    const ImGuiPayload *payload = ImGui_GetDragDropPayload();
    assert(payload && !strcmp(payload->Data, path));
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), destination.x, destination.y);
    frame(f);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    frame(f);
    frame(f);
}

static void canvas_drop(image_files_fixture *f, const char *path, behavior_domain domain)
{
    open_graph(f, domain);
    editor *e = f->graph;
    int before = acount(e->active_graph->nodes), history = acount(e->undo);
    drag_file(f, path, false);
    if (domain == BEHAVIOR_DOMAIN_EVENT) {
        assert(!image_node(e) && acount(e->active_graph->nodes) == before);
        assert(acount(e->undo) == history);
        return;
    }
    expect_path(e, path);
    assert(acount(e->active_graph->nodes) == before + 1 && acount(e->undo) == history + 1);
    assert(acount(e->selected) == 1 && e->selected[0] == image_node(e));
    item("shader.image.dimensions");
    editor_ui_rect error;
    assert(!editor_ui_item_rect("shader.image.error", &error));
    node *n = image_node(e);
    assert(n->input_pins[SHADER_IMAGE_INPUT_FILTER].default_value.i == SHADER_IMAGE_BILINEAR);
    editor_undo(e);
    frame(f);
    assert(!image_node(e) && acount(e->active_graph->nodes) == before);
    editor_redo(e);
    frame(f);
    expect_path(e, path);
}

static void refresh_choices(image_files_fixture *f, const char *keep, const char *remove, const char *directory)
{
    editor *e = f->graph;
    ed_select_node(e, image_node(e), false);
    frame(f);
    int count = acount(e->active_graph->nodes), history = acount(e->undo);
    drag_file(f, remove, true);
    expect_path(e, remove);
    assert(acount(e->active_graph->nodes) == count && acount(e->undo) == history + 1);
    editor_undo(e);
    ed_select_node(e, image_node(e), false);
    frame(f);
    expect_path(e, keep);

    click(f, "shader.image.select");
    frame(f);
    char choice[1024];
    snprintf(choice, sizeof choice, "shader.image.choice.%s", directory);
    editor_ui_rect rect;
    assert(!editor_ui_item_rect(choice, &rect));
    snprintf(choice, sizeof choice, "shader.image.choice.%s", remove);
    item(choice);
    click(f, "shader.image.search");
    ImGuiIO_AddInputCharactersUTF8(ImGui_GetIO(), "uPpEr");
    frame(f);
    assert(!editor_ui_item_rect(choice, &rect));
    char kept[1024];
    snprintf(kept, sizeof kept, "shader.image.choice.%s", keep);
    item(kept);
    ImGuiKey modifier = ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGuiMod_Super
                                                             : ImGuiMod_Ctrl;
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, true);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, true);
    frame(f);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_A, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), modifier, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Backspace, true);
    frame(f);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), ImGuiKey_Backspace, false);
    frame(f);
    item(choice);
    history = acount(e->undo);
    assert(unlink(remove) == 0);
    app_scene_authoring_refresh_content(f->scene);
    frame(f);
    assert(f->scene->image_choices.count == 1 && !editor_ui_item_rect(choice, &rect));
    expect_path(e, keep); /* Refresh cannot invalidate the graph-owned selection. */
    snprintf(choice, sizeof choice, "shader.image.choice.%s", keep);
    click(f, choice);
    frame(f); /* The closed popup restores the selector on the next frame. */
    expect_path(e, keep);
    assert(acount(e->undo) == history);

    click(f, "shader.image.select");
    assert(unlink(keep) == 0);
    app_scene_authoring_refresh_content(f->scene);
    frame(f);
    assert(f->scene->image_choices.count == 0);
    item("shader.image.empty");
    expect_path(e, keep);
    click(f, "shader.image.none");
    expect_path(e, "");
}

void shader_image_files_ui()
{
    char root[] = "content/shader-image-files-XXXXXX";
    assert(mkdtemp(root));
    char upper[256], second[256], directory[256];
    snprintf(upper, sizeof upper, "%s/Upper.PNG", root);
    snprintf(second, sizeof second, "%s/Second.png", root);
    snprintf(directory, sizeof directory, "%s/Folder.PNG", root);
    write_png(upper);
    write_png(second);
    assert(mkdir(directory, 0700) == 0);
    image_files_fixture f = fixture(root);
    assert(f.scene->image_choices.count == 2);
    char missing[256];
    snprintf(missing, sizeof missing, "%s/missing", root);
    assert(app_scene_authoring_set_content_root(f.scene, missing) != 0);
    assert(!strcmp(f.scene->content_root, root) && f.scene->image_choices.count == 2);
    canvas_drop(&f, upper, BEHAVIOR_DOMAIN_SHAPE);
    canvas_drop(&f, upper, BEHAVIOR_DOMAIN_SHADER_LIBRARY);
    canvas_drop(&f, upper, BEHAVIOR_DOMAIN_EVENT);
    canvas_drop(&f, upper, BEHAVIOR_DOMAIN_MATERIAL);
    refresh_choices(&f, upper, second, directory);
    app_scene_authoring_free(f.scene);
    engine_free(f.runtime);
    game_live_world_free(f.live);
    editor_free(f.graph);
    ecs_world_free(f.world);
    assert(rmdir(directory) == 0 && rmdir(root) == 0);
}
