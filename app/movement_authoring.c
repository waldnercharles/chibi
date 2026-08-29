#include "movement_authoring.h"

#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../game/generated.h"
#include "../game/query.h"
#include "../game/system_document.h"
#include "cute_host.h"
#include "scene_draw.h"
#include "scene_imgui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOVEMENT_SYSTEM_PATH "content/systems/MovementSystem.json"
#define PLAYER_START_X 320.0f
#define PLAYER_START_Y -180.0f

struct movement_authoring
{
    ecs_world *world;
    ecs_entity player;
    game_system *system;
    editor *editor;
    engine *runtime;
    unsigned synced_revision;
    double last_time;
    bool require_health;
    char status[256];
};

static const ecs_component_desc *resolve_component(const char *name, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (strcmp(component->name, name) == 0 ||
            (component->key && strcmp(component->key, name) == 0)) {
            return component;
        }
    }
    return NULL;
}

static const char *component_key(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key ? component->key : component->name;
        }
    }
    return NULL;
}

static int write_atomic(const char *path, const char *text)
{
    size_t size = strlen(path) + 5;
    char *temporary = (char *)xmalloc(size);
    snprintf(temporary, size, "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        free(temporary);
        return -1;
    }
    int result = fputs(text, file) < 0;
    if (fclose(file) != 0) {
        result = -1;
    }
    if (!result && rename(temporary, path) != 0) {
        result = -1;
    }
    if (result) {
        remove(temporary);
    }
    free(temporary);
    return result;
}

static void set_status(movement_authoring *authoring, const char *message)
{
    snprintf(authoring->status, sizeof authoring->status, "%s", message);
}

static void set_system_error(movement_authoring *authoring, const flow_error *error)
{
    if (error && error->message) {
        snprintf(
            authoring->status,
            sizeof authoring->status,
            "System error: %s",
            error->message
        );
        return;
    }
    set_status(authoring, "System error");
}

movement_authoring *movement_authoring_new(
    ecs_world *world,
    ecs_entity player,
    game_system *system,
    editor *editor,
    engine *runtime
)
{
    if (!world || !system || !editor || !runtime) {
        return NULL;
    }
    movement_authoring *authoring = (movement_authoring *)xcalloc(1, sizeof *authoring);
    authoring->world = world;
    authoring->player = player;
    authoring->system = system;
    authoring->editor = editor;
    authoring->runtime = runtime;
    authoring->synced_revision = UINT_MAX;
    set_status(authoring, "Ready");
    return authoring;
}

void movement_authoring_free(movement_authoring *authoring)
{
    free(authoring);
}

void movement_authoring_sync(movement_authoring *authoring)
{
    unsigned revision = editor_edit_revision(authoring->editor);
    if (revision == authoring->synced_revision) {
        return;
    }
    char *json = blueprint_save_json(editor_blueprint(authoring->editor));
    blueprint *definition = json ? blueprint_load_json(json) : NULL;
    free(json);
    if (!definition) {
        set_status(authoring, graph_last_error());
        authoring->synced_revision = revision;
        return;
    }
    if (game_system_replace(authoring->system, definition) != 0) {
        const compile_report *compile = game_system_compile(authoring->system);
        const compile_diagnostic *diagnostic = compile_report_first_error(compile);
        if (diagnostic) {
            set_status(authoring, diagnostic->message);
        } else {
            set_status(authoring, "System graph could not compile");
        }
    } else {
        set_status(authoring, "System graph compiled");
    }
    authoring->synced_revision = revision;
}

void movement_authoring_input(movement_authoring *authoring, bool viewport_focused)
{
    GameInput *input = GAME_GameInput_get(authoring->world, authoring->player);
    if (!input) {
        return;
    }
    if (!viewport_focused || editor_host_captures_keyboard()) {
        input->move = (CF_V2){ 0.0f, 0.0f };
        return;
    }
    float x = (cf_key_down(CF_KEY_D) || cf_key_down(CF_KEY_RIGHT)) -
              (cf_key_down(CF_KEY_A) || cf_key_down(CF_KEY_LEFT));
    float y = (cf_key_down(CF_KEY_S) || cf_key_down(CF_KEY_DOWN)) -
              (cf_key_down(CF_KEY_W) || cf_key_down(CF_KEY_UP));
    input->move = (CF_V2){ x, y };
}

int movement_authoring_set_query_json(
    movement_authoring *authoring,
    const char *json,
    char *error,
    size_t error_size
)
{
    if (!authoring || !json || !strstr(json, "\"query\"")) {
        return 0;
    }
    game_system_document
        *document = game_system_document_load(json, resolve_component, NULL, error, error_size);
    if (!document) {
        return -1;
    }
    const ecs_query_expr *query = game_system_document_query(document);
    int result = game_system_set_query(authoring->system, query);
    if (result == 0 && query && ecs_query_expr_get_kind(query) == ECS_QUERY_HAS) {
        authoring->require_health = ecs_query_expr_get_component(query) ==
                                    GAME_COMPONENT_GameHealth.id;
    }
    game_system_document_free(document);
    return result;
}

static const char *component_name(ecs_component_id id)
{
    if (id == GAME_COMPONENT_GameTransform.id) {
        return "GameTransform";
    }
    if (id == GAME_COMPONENT_GameMovement.id) {
        return "GameMovement";
    }
    if (id == GAME_COMPONENT_GameInput.id) {
        return "GameInput";
    }
    return "Unknown";
}

static const char *system_backend_name(const game_system *system)
{
    if (game_system_uses_sljit_batch(system) && game_system_uses_sljit_simd(system)) {
        return "SLJIT SIMD batch";
    }
    if (game_system_uses_sljit_batch(system)) {
        return "SLJIT batch";
    }
    if (game_system_uses_sljit_simd(system)) {
        return "SLJIT SIMD";
    }
    if (game_system_uses_sljit_registers(system)) {
        return "SLJIT registers";
    }
    if (game_system_uses_sljit(system)) {
        return "SLJIT memory";
    }
    if (game_system_uses_typed_bytecode(system)) {
        return "typed bytecode";
    }
    if (game_system_uses_bytecode(system)) {
        return "generic bytecode";
    }
    return "flow interpreter";
}

static void draw_system_panel(movement_authoring *authoring)
{
    if (!ImGui_Begin("System", NULL, 0)) {
        ImGui_End();
        return;
    }
    ImGui_TextUnformatted("MovementSystem");
    ImGui_TextUnformatted("Implementation: JIT dataflow");
    const graph *active = editor_active_graph(authoring->editor);
    ImGui_Text("Graph nodes: %d", acount(active->nodes));
    ImGui_SeparatorText("Query");
    int count = game_system_query_count(authoring->system);
    for (int i = 0; i < count; i++) {
        ImGui_Text("- %s", component_name(game_system_query_at(authoring->system, i)));
    }
    ImGui_SeparatorText("Authored filter");
    ImGui_TextUnformatted("All inferred terms plus:");
    if (ImGui_Checkbox("GameHealth", &authoring->require_health)) {
        ecs_query_expr *filter = authoring->require_health
                                   ? ecs_query_expr_has(
                                         GAME_COMPONENT_GameHealth.id
                                     )
                                   : NULL;
        game_system_set_query(authoring->system, filter);
        ecs_query_expr_free(filter);
    }
    ImGui_Separator();
    bool playing = engine_is_playing(authoring->runtime);
    if (ImGui_Button(playing ? "Stop" : "Play")) {
        engine_set_playing(authoring->runtime, !playing);
    }
    ImGui_SameLine();
    if (ImGui_Button("Reset player")) {
        GameTransform
            *transform = GAME_GameTransform_get(authoring->world, authoring->player);
        if (transform) {
            *transform = (GameTransform){ PLAYER_START_X, PLAYER_START_Y };
        }
    }
    ImGui_Text("Backend: %s", system_backend_name(authoring->system));
    ImGui_Text("Matches: %d", game_system_match_count(authoring->system));
    ImGui_Text("Status: %s", authoring->status[0] ? authoring->status : "Ready");
    ImGui_End();
}

static void draw_entity_panel(movement_authoring *authoring)
{
    if (!ImGui_Begin("Entity", NULL, 0)) {
        ImGui_End();
        return;
    }
    ImGui_TextUnformatted("Player");
    GameTransform
        *transform = GAME_GameTransform_get(authoring->world, authoring->player);
    GameMovement *movement = GAME_GameMovement_get(authoring->world, authoring->player);
    GameInput *input = GAME_GameInput_get(authoring->world, authoring->player);
    if (transform) {
        ImGui_SeparatorText("GameTransform");
        ImGui_InputFloatEx("X", &transform->x, 1.0f, 10.0f, "%.1f", 0);
        ImGui_InputFloatEx("Y", &transform->y, 1.0f, 10.0f, "%.1f", 0);
    }
    if (movement) {
        ImGui_SeparatorText("GameMovement");
        ImGui_InputFloatEx("Speed", &movement->speed, 1.0f, 10.0f, "%.1f", 0);
    }
    if (input) {
        ImGui_SeparatorText("GameInput");
        ImGui_Text("Move: %.1f, %.1f", input->move.x, input->move.y);
        ImGui_TextDisabled("WASD / arrow keys while Scene is focused");
    }
    ImGui_End();
}

void movement_authoring_draw(movement_authoring *authoring)
{
    draw_system_panel(authoring);
    draw_entity_panel(authoring);
}

void movement_authoring_render(void *user)
{
    movement_authoring *authoring = (movement_authoring *)user;
    const GameTransform
        *transform = GAME_GameTransform_get_const(authoring->world, authoring->player);
    if (!transform) {
        return;
    }
    cf_draw_push_color((app_cf_color){ 0.25f, 0.75f, 1.0f, 1.0f });
    cf_draw_quad_fill(
        app_cf_make_aabb_pos_w_h(app_cf_make_v2(transform->x, -transform->y), 32.0f, 32.0f),
        0.0f
    );
    cf_draw_pop_color();
}

void movement_authoring_system(ecs_world *, double monotonic_time, void *user)
{
    movement_authoring *authoring = (movement_authoring *)user;
    double delta = authoring->last_time ? monotonic_time - authoring->last_time : 0.0;
    authoring->last_time = monotonic_time;
    if (delta < 0.0 || delta >= 1.0) {
        delta = 0.0;
    }
    flow_error error = { 0 };
    int result = game_system_tick(authoring->system, monotonic_time, delta, &error);
    if (result != FLOW_OK) {
        set_system_error(authoring, &error);
        return;
    }
    if (game_system_match_count(authoring->system) > 0) {
        editor_set_execution_pulse(authoring->editor, monotonic_time);
    }
}

int movement_authoring_save(editor *editor, void *user)
{
    movement_authoring *authoring = (movement_authoring *)user;
    char *json = game_system_document_save_blueprint(
        editor_blueprint(editor),
        game_system_query_expression(authoring->system),
        resolve_component,
        component_key,
        NULL
    );
    if (!json) {
        return -1;
    }
    int result = write_atomic(MOVEMENT_SYSTEM_PATH, json);
    free(json);
    return result;
}
