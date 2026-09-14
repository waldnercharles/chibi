#include "render/widgets.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>

static double time;
static bool draw(ecs_world *world, game_live_world *live, engine *runtime)
{
    ImGui_NewFrame();
    ImGui_SetNextWindowPos((ImVec2){ 0, 0 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 1280, 720 }, ImGuiCond_Always);
    ImGui_Begin("Player UI", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    bool captured = render_widgets(
        world,
        live->entities,
        live->visibility,
        (CF_V2){ 0 },
        (CF_V2){ 1280, 720 },
        true
    );
    ImGui_End();
    ImGui_Render();
    time += 1.0 / 60;
    assert(engine_frame(runtime, time) == FLOW_OK);
    return captured;
}

void test_player_widgets()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 1280, 720 };
    io->DeltaTime = 1.0f / 60;
    io->IniFilename = NULL;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    assert(ImFontAtlas_Build(io->Fonts));
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    engine *runtime = engine_new(world, live);
    ecs_entity target = ecs_world_create_entity(world),
               source = ecs_world_create_entity(world);
    assert(GAME_GameWidget_add(world, target, 0) == ECS_OK);
    assert(GAME_GameWidget_add(world, source, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, target, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameWidget_get(world, target) = (GameWidget){ .kind = GAME_WIDGET_SLOT,
                                                        .text = "Fit",
                                                        .event = "Choose",
                                                        .drop_group = "parts",
                                                        .size = { 180, 44 },
                                                        .offset = { 20, 20 } };
    *GAME_GameWidget_get(world, source) = (GameWidget){ .kind = GAME_WIDGET_SLOT,
                                                        .text = "Ricochet",
                                                        .event = "Choose",
                                                        .drop_group = "parts",
                                                        .draggable = true,
                                                        .value = 2,
                                                        .size = { 180, 44 },
                                                        .offset = { 20, 100 } };
    behavior_asset *asset = behavior_asset_new("Fitting");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    assert(graph_set_event_name(g, event, "Choose") == 0);
    assert(
        graph_add_pin(g, event, true, &(pin_spec){ .name = "Source", .type = PIN_HANDLE, .type_name = "ecs.entity" }) ==
        1
    );
    assert(
        graph_add_pin(g, event, true, &(pin_spec){ .name = "Value", .type = PIN_F32, .type_name = "float" }) ==
        2
    );
    node *health = graph_add_node(g, registry_node_by_name("Set_GameHealth_current"));
    assert(
        graph_set_pin_default(g, health, false, 1, &(pin_value){ .type = PIN_I32, .i = 7 }) == 0
    );
    assert(graph_connect_exec(g, event, 0, health, 0, 0) == 0);
    node *value = graph_add_node(g, registry_node_by_name("Set_GameWidget_value"));
    assert(graph_connect_exec(g, health, 0, value, 0, 0) == 0);
    assert(graph_connect_data(g, event, 2, value, 1) == 0);
    assert(game_behavior_register_asset(live->behaviors, "Fitting", asset) == 0);
    assert(game_behavior_attach(live->behaviors, target, "Fitting") == 0);
    engine_set_playing(runtime, true);
    time = 0;
    draw(world, live, runtime);
    draw(world, live, runtime);
    ImGuiIO_AddMousePosEvent(io, 70, 42);
    draw(world, live, runtime);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    draw(world, live, runtime);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    draw(world, live, runtime);
    assert(GAME_GameHealth_get_const(world, target)->current == 7);
    ImGuiIO_AddMousePosEvent(io, 600, 400);
    assert(!draw(world, live, runtime)); /* A pointer click must not trap keyboard focus. */
    GAME_GameHealth_get(world, target)->current = 0;
    ImGuiIO_AddMousePosEvent(io, 70, 122);
    draw(world, live, runtime);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    draw(world, live, runtime);
    ImGuiIO_AddMousePosEvent(io, 75, 110);
    draw(world, live, runtime);
    ImGuiIO_AddMousePosEvent(io, 70, 42);
    draw(world, live, runtime);
    draw(world, live, runtime);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    draw(world, live, runtime);
    assert(GAME_GameHealth_get_const(world, target)->current == 7);
    assert(GAME_GameWidget_get_const(world, target)->value == 2);
    GAME_GameHealth_get(world, target)->current = 0;
    GAME_GameWidget_get(world, target)->disabled = true;
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    draw(world, live, runtime);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    draw(world, live, runtime);
    assert(GAME_GameHealth_get_const(world, target)->current == 0);
    GAME_GameWidget_get(world, target)->disabled = false;
    ImGuiIO_AddMousePosEvent(io, 600, 400);
    draw(world, live, runtime);
    for (int i = 0; i < 2 && GAME_GameHealth_get_const(world, target)->current == 0;
         i++) {
        ImGuiIO_AddKeyEvent(io, ImGuiKey_Tab, true);
        draw(world, live, runtime);
        ImGuiIO_AddKeyEvent(io, ImGuiKey_Tab, false);
        draw(world, live, runtime);
        ImGuiIO_AddKeyEvent(io, ImGuiKey_Space, true);
        draw(world, live, runtime);
        ImGuiIO_AddKeyEvent(io, ImGuiKey_Space, false);
        draw(world, live, runtime);
    }
    assert(GAME_GameHealth_get_const(world, target)->current == 7);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
    puts("Player widgets passed: pointer activation, drag transfer, disabled controls, keyboard activation and focus release");
}
