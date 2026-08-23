#include "scene_authoring.h"

#include "../core/array.h"
#include "../core/xalloc.h"
#include "../game/generated.h"
#include "scene_imgui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_entity entity;
    int player;
    int behavior;
} scene_actor;

struct app_scene_authoring
{
    ecs_world *world;
    game_live_world *live;
    game_live_definition *definition;
    const blueprint *editor_working;
    engine *runtime;
    scene_actor *actors;
    int selected;
    char message[256];
};

static scene_actor *selected_actor(app_scene_authoring *scene)
{
    if (!scene || scene->selected < 0 || scene->selected >= acount(scene->actors)) {
        return NULL;
    }
    return &scene->actors[scene->selected];
}

static void select_player(app_scene_authoring *scene, int index)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->actors[i].player = i == index;
    }
    if (index >= 0 && index < acount(scene->actors)) {
        engine_set_player(scene->runtime, scene->actors[index].entity);
        engine_set_focus(scene->runtime, ENGINE_FOCUS_VIEWPORT);
    }
}

static int add_actor(app_scene_authoring *scene, int player)
{
    ecs_entity entity = ecs_world_create_entity(scene->world);
    if (!entity.slot || GAME_GameTransform_add(scene->world, entity, 0) != ECS_OK ||
        GAME_GameHealth_add(scene->world, entity, 1) != ECS_OK ||
        ecs_world_apply_commands(scene->world) != ECS_OK) {
        return -1;
    }
    GameTransform *transform = GAME_GameTransform_get(scene->world, entity);
    GameHealth *health = GAME_GameHealth_get(scene->world, entity);
    int index = acount(scene->actors);
    transform->x = (float)(index * 80);
    transform->y = 0.0f;
    health->current = 100;
    health->maximum = 100;
    scene_actor actor = { entity, player != 0, 1 };
    if (game_behavior_attach(
            game_live_behaviors(scene->live),
            entity,
            game_live_definition_name(scene->definition)
        ) != 0) {
        actor.behavior = 0;
    }
    apush(scene->actors, actor);
    scene->selected = acount(scene->actors) - 1;
    if (player) {
        select_player(scene, scene->selected);
    }
    return 0;
}

static void remove_selected(app_scene_authoring *scene)
{
    scene_actor *actor = selected_actor(scene);
    if (!actor) {
        return;
    }
    game_behavior_destroy_entity(game_live_behaviors(scene->live), actor->entity);
    adel(scene->actors, scene->selected);
    if (scene->selected >= acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        if (scene->actors[i].player) {
            engine_set_player(scene->runtime, scene->actors[i].entity);
            return;
        }
    }
}

app_scene_authoring *app_scene_authoring_new(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const blueprint *editor_working,
    engine *runtime
)
{
    if (!world || !live || !definition || !editor_working || !runtime) {
        return NULL;
    }
    app_scene_authoring *scene = (app_scene_authoring *)xcalloc(1, sizeof *scene);
    scene->world = world;
    scene->live = live;
    scene->definition = definition;
    scene->editor_working = editor_working;
    scene->runtime = runtime;
    scene->selected = -1;
    snprintf(scene->message, sizeof scene->message, "No scene entities yet.");
    return scene;
}

void app_scene_authoring_set_editor_working(app_scene_authoring *scene, const blueprint *editor_working)
{
    if (scene && editor_working) {
        scene->editor_working = editor_working;
    }
}

void app_scene_authoring_free(app_scene_authoring *scene)
{
    if (!scene) {
        return;
    }
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    afree(scene->actors);
    free(scene);
}

static void draw_actor_details(app_scene_authoring *scene)
{
    scene_actor *actor = selected_actor(scene);
    if (!actor) {
        ImGui_TextUnformatted("Select an entity to edit its authored components.");
        return;
    }
    GameTransform *transform = GAME_GameTransform_get(scene->world, actor->entity);
    GameHealth *health = GAME_GameHealth_get(scene->world, actor->entity);
    ImGui_Separator();
    ImGui_Text("Entity %u:%u", actor->entity.slot, actor->entity.generation);
    if (!transform || !health) {
        ImGui_TextUnformatted("This entity is missing a generated scene component.");
        return;
    }
    bool player = actor->player != 0;
    if (ImGui_Checkbox("Player", &player)) {
        select_player(scene, scene->selected);
    }
    bool behavior = actor->behavior != 0;
    if (ImGui_Checkbox("Tick graph attached", &behavior)) {
        game_behavior_detach_entity(game_live_behaviors(scene->live), actor->entity);
        actor->behavior = behavior && game_behavior_attach(
                                          game_live_behaviors(scene->live),
                                          actor->entity,
                                          game_live_definition_name(scene->definition)
                                      ) == 0;
    }
    ImGui_TextUnformatted("GameTransform");
    ImGui_InputFloatEx("X", &transform->x, 1.0f, 0.0f, "%.2f", 0);
    ImGui_InputFloatEx("Y", &transform->y, 1.0f, 0.0f, "%.2f", 0);
    ImGui_TextUnformatted("GameHealth");
    ImGui_InputInt("Current", &health->current);
    ImGui_InputInt("Maximum", &health->maximum);
    if (ImGui_Button("Delete selected")) {
        remove_selected(scene);
    }
}

void app_scene_authoring_draw(app_scene_authoring *scene)
{
    if (!scene || !ImGui_Begin("Scene authoring", NULL, 0)) {
        if (scene) {
            ImGui_End();
        }
        return;
    }
    ImGui_TextUnformatted("Scene values are authored by these editor controls.");
    if (ImGui_Button("+ Player")) {
        if (add_actor(scene, 1) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not create player.");
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("+ Enemy")) {
        if (add_actor(scene, 0) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not create enemy.");
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("Compile / Apply graph")) {
        if (game_live_sync_working(scene->definition, scene->editor_working) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not capture the editor graph.");
        } else {
            (void)game_live_apply(
                scene->live,
                scene->definition,
                scene->message,
                sizeof scene->message
            );
        }
    }
    ImGui_Text("%s", scene->message);
    ImGui_Separator();
    for (int i = 0; i < acount(scene->actors); i++) {
        char label[64];
        snprintf(
            label,
            sizeof label,
            "%s %d##scene_actor_%d",
            scene->actors[i].player ? "Player" : "Enemy",
            i + 1,
            i
        );
        if (ImGui_SelectableEx(label, i == scene->selected, 0, (ImVec2){ 0.0f, 0.0f })) {
            scene->selected = i;
        }
    }
    draw_actor_details(scene);
    ImGui_End();
}

int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!scene->actors[i].behavior) {
            continue;
        }
        flow_error error = { 0 };
        (void)game_behavior_dispatch(
            game_live_behaviors(scene->live),
            scene->actors[i].entity,
            "Tick",
            (flow_inputs){ 0 },
            &error
        );
    }
    return engine_frame(scene->runtime, monotonic_time);
}

int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed)
{
    return scene
             ? engine_dispatch_player_input(scene->runtime, action, value, pressed, NULL)
             : FLOW_ERR_ARGUMENT;
}
