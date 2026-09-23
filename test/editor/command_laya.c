#include "editor/commands/internal.h"
#include "editor/commands/laya.h"
#include "editor/commands/model.h"
#include "editor/ui/command_transport.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_authoring.h"
#include "graph/model/registry.h"
#include "graph/model/type.h"
#include "runtime/components.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static void scene_command()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *asset = behavior_asset_new("Laya command");
    editor *graph = editor_new(asset);
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new(
        "Laya command",
        behavior_asset_new("Laya command")
    );
    assert(game_live_add_definition(live, definition) == 0);
    engine *runtime = engine_new(world, live);
    app_scene_authoring *
        scene = app_scene_authoring_new_headless(world, live, definition, asset, graph, runtime);
    assert(scene);
    const char
        *prototype = "{\"version\":1,\"name\":\"Player\",\"components\":{"
                     "\"GameAppearance\":{\"color\":4294967295},"
                     "\"GameCollider\":{\"shape\":1}},\"behaviors\":[]}";
    assert(app_scene_authoring_register_prototype(scene, prototype, NULL) == 0);
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *catalog_doc = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *objects = yyjson_obj_get(yyjson_doc_get_root(catalog_doc), "objects");
    yyjson_val *player = NULL;
    for (size_t i = 0; i < yyjson_arr_size(objects); i++) {
        yyjson_val *candidate = yyjson_arr_get(objects, i);
        if (!strcmp(command_string(candidate, "name"), "Player") &&
            !strcmp(command_string(candidate, "kind"), "prototype")) {
            player = candidate;
        }
    }
    test_check(
        player && yyjson_arr_size(yyjson_obj_get(player, "fields")) > 0,
        "headless editor catalog exposes the authored Player prototype"
    );
    yyjson_doc_free(catalog_doc);
    free(catalog);
    char *reply = editor_commands_api(scene, "{\"operation\":\"submit\",\"command\":\"Change Player's color to blue\"}");
    free(reply);
    for (int i = 0; i < 6; i++) {
        editor_commands_frame(scene);
    }
    const char *status = editor_commands_result(scene);
    yyjson_doc *result_doc = yyjson_read(status, strlen(status), 0);
    yyjson_val *result = yyjson_doc_get_root(result_doc);
    test_check(
        !strcmp(command_string(result, "status"), "needs_review"),
        "headless command task reaches review without waiting for a model"
    );
    yyjson_doc_free(result_doc);
    reply = editor_commands_api(scene, "{\"operation\":\"respond\",\"approve\":true}");
    result_doc = yyjson_read(reply, strlen(reply), 0);
    result = yyjson_doc_get_root(result_doc);
    pin_value color;
    int read = app_scene_authoring_read_prototype_field(scene, "Player", "GameAppearance", "color", &color);
    test_check(!strcmp(command_string(result, "status"), "applied") && !read && color.u == 0x0000ffffu, "headless editor applies the reviewed prototype color through the command palette API");
    yyjson_doc_free(result_doc);
    free(reply);

    reply = editor_commands_api(scene, "{\"operation\":\"submit\",\"command\":\"Change the player's hitbox to a circle\"}");
    free(reply);
    for (int i = 0; i < 6; i++) {
        editor_commands_frame(scene);
    }
    status = editor_commands_result(scene);
    result_doc = yyjson_read(status, strlen(status), 0);
    test_check(
        !strcmp(command_string(yyjson_doc_get_root(result_doc), "status"), "needs_review"),
        "headless collider command reaches review"
    );
    yyjson_doc_free(result_doc);
    reply = editor_commands_api(scene, "{\"operation\":\"respond\",\"approve\":true}");
    result_doc = yyjson_read(reply, strlen(reply), 0);
    read = app_scene_authoring_read_prototype_field(scene, "Player", "GameCollider", "shape", &color);
    test_check(
        !strcmp(command_string(yyjson_doc_get_root(result_doc), "status"), "applied") &&
            !read && color.i == GAME_COLLIDER_CIRCLE,
        "headless editor applies the reviewed Circle collider value"
    );
    yyjson_doc_free(result_doc);
    free(reply);

    assert(
        app_scene_authoring_register_prototype(scene, "{\"version\":1,\"name\":\"Burst\",\"components\":{},\"behaviors\":[]}", NULL) ==
        0
    );
    assert(app_scene_authoring_add_prototype_component(scene, "Burst", "GameParticleEmitter") == 0);
    reply = editor_commands_api(scene, "{\"operation\":\"submit\",\"command\":\"Increase the size of the particles\"}");
    free(reply);
    for (int i = 0; i < 6; i++) {
        editor_commands_frame(scene);
    }
    status = editor_commands_result(scene);
    result_doc = yyjson_read(status, strlen(status), 0);
    test_check(
        !strcmp(command_string(yyjson_doc_get_root(result_doc), "status"), "needs_review"),
        "headless particle size command reaches review"
    );
    yyjson_doc_free(result_doc);
    reply = editor_commands_api(scene, "{\"operation\":\"respond\",\"approve\":true}");
    result_doc = yyjson_read(reply, strlen(reply), 0);
    pin_value size_x, size_y;
    int read_x = app_scene_authoring_read_prototype_field(
        scene,
        "Burst",
        "GameParticleEmitter",
        "size.start.x",
        &size_x
    );
    int read_y = app_scene_authoring_read_prototype_field(
        scene,
        "Burst",
        "GameParticleEmitter",
        "size.start.y",
        &size_y
    );
    test_check(
        !strcmp(command_string(yyjson_doc_get_root(result_doc), "status"), "applied") &&
            !read_x && !read_y && size_x.f == 2.5f && size_y.f == 2.5f,
        "headless editor applies both nested particle size changes in one "
        "transaction"
    );
    yyjson_doc_free(result_doc);
    free(reply);
    app_scene_authoring_free(scene);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(graph);
    ecs_world_free(world);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
}

static char *bound_packet(const char *packet, const char *target, const char *field)
{
    yyjson_doc *read = yyjson_read(packet, strlen(packet), 0);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *bindings = yyjson_mut_obj_add_obj(doc, root, "bindings");
    if (target) {
        yyjson_mut_obj_add_str(doc, bindings, "target", target);
    }
    if (field) {
        yyjson_mut_obj_add_str(doc, bindings, "field", field);
    }
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    return result;
}

static char *command_packet(const char *packet, const char *command)
{
    yyjson_doc *read = yyjson_read(packet, strlen(packet), 0);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_obj_remove_key(root, "command");
    yyjson_mut_obj_add_strcpy(doc, root, "command", command);
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    return result;
}

static char *local_result(const char *packet)
{
    char error[256] = { 0 };
    char *request = editor_command_model_prepare(packet, error, sizeof error);
    if (!request) {
        return NULL;
    }
    editor_command_request *transport = editor_command_request_start(request);
    char *raw = transport ? editor_command_request_poll(transport) : NULL;
    char *result = raw ? editor_command_model_result(packet, raw, error, sizeof error)
                       : NULL;
    editor_command_request_free(transport);
    free(raw);
    free(request);
    return result;
}

static void emitter_decision(const char *goal, bool reversed, const char *expected)
{
    const char
        *boost = "{\"id\":\"prototype:boost\",\"name\":\"BoostEmitter\","
                 "\"kind\":\"prototype\",\"components\":["
                 "\"GameParticleEmitter\"],\"fields\":[{\"id\":\"GAME."
                 "GameParticleEmitter.size.start.x\","
                 "\"name\":\"size.start.x\",\"type\":\"number\",\"value\":4}]}";
    const char
        *smoke = "{\"id\":\"prototype:smoke\",\"name\":\"SmokeEmitter\","
                 "\"kind\":\"prototype\",\"components\":["
                 "\"GameParticleEmitter\"],\"fields\":[{\"id\":\"GAME."
                 "GameParticleEmitter.size.start.x\","
                 "\"name\":\"size.start.x\",\"type\":\"number\",\"value\":4}]}";
    char packet[2048];
    snprintf(
        packet,
        sizeof packet,
        "{\"command\":\"%s\",\"catalog\":{\"revision\":\"r1\",\"document\":"
        "\"scene\",\"objects\":[%s,%s]}}",
        goal,
        reversed ? smoke : boost,
        reversed ? boost : smoke
    );
    char *result = local_result(packet);
    yyjson_doc *read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    yyjson_val *root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        expected ? !strcmp(command_string(root, "status"), "continue") &&
                       !strcmp(command_string(root, "value"), expected)
                 : !strcmp(command_string(root, "status"), "needs_clarification"),
        expected ? "named emitter resolves to the intended prototype"
                 : "ambiguous emitter request asks for clarification"
    );
    yyjson_doc_free(read);
    free(result);
    if (!expected) {
        char *bound = bound_packet(packet, "prototype:boost", NULL);
        result = local_result(bound);
        read = result ? yyjson_read(result, strlen(result), 0) : NULL;
        root = read ? yyjson_doc_get_root(read) : NULL;
        test_check(
            !strcmp(command_string(root, "status"), "continue") &&
                !strcmp(command_string(root, "value"), "GAME.GameParticleEmitter.size.start.x"),
            "clarified emitter choice resumes the command"
        );
        yyjson_doc_free(read);
        free(result);
        free(bound);
    }
}

void test_editor_command_laya()
{
    char old_host[256], old_model[128], error[256] = { 0 };
    editor_command_model_get_settings(old_host, sizeof old_host, old_model, sizeof old_model);
    test_check(
        !editor_command_laya_set_token("bad\nkey") &&
            editor_command_laya_set_token("test-key"),
        "native Laya API key rejects header injection"
    );
    test_check(
        editor_command_model_set_settings("http://127.0.0.1:8768/v1/decide", "laya", error, sizeof error),
        "Laya can be selected as the command provider"
    );
    const char *packet =
        "{\"command\":\"Change Ship's color to blue\",\"catalog\":{"
        "\"revision\":\"r1\",\"document\":\"scene\",\"objects\":["
        "{\"id\":\"entity:ship\",\"name\":\"Ship\",\"kind\":\"entity\","
        "\"prototype\":\"Player\",\"fields\":[{\"id\":\"GAME.GameAppearance."
        "color\","
        "\"name\":\"color\",\"type\":\"color\",\"value\":4294967295}]},"
        "{\"id\":\"prototype:player\",\"name\":\"Player\",\"kind\":"
        "\"prototype\","
        "\"fields\":[{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\","
        "\"label\":\"Appearance "
        "Color\",\"type\":\"color\",\"value\":4294967295}]}]}}";
    char *result = local_result(packet);
    yyjson_doc *read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    yyjson_val *root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        !strcmp(command_string(root, "status"), "continue") &&
            !strcmp(command_string(root, "value"), "prototype:player"),
        "named Ship instance resolves to its Player prototype without a model "
        "call"
    );
    yyjson_doc_free(read);
    free(result);

    char *target_packet = bound_packet(packet, "prototype:player", NULL);
    result = local_result(target_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        !strcmp(command_string(root, "status"), "continue") &&
            !strcmp(command_string(root, "value"), "GAME.GameAppearance.color"),
        "color request narrows to the appearance field without a model call"
    );
    yyjson_doc_free(read);
    free(result);
    free(target_packet);

    char *field_packet = bound_packet(packet, "prototype:player", "GAME.GameAppearance.color");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            !strcmp(command_string(operation, "target"), "prototype:player") &&
            yyjson_get_uint(yyjson_obj_get(operation, "value")) == 0x0000ffffu,
        "Laya path proposes a reviewed blue edit on the prototype"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);
    char *phrase = command_packet(packet, "Make the ship blue");
    field_packet = bound_packet(phrase, "prototype:player", "GAME.GameAppearance.color");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            yyjson_get_uint(yyjson_obj_get(operation, "value")) == 0x0000ffffu,
        "color names work without saying the word color"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);
    free(phrase);

    const char *hitbox =
        "{\"command\":\"Change the player's hitbox to a circle\",\"catalog\":{"
        "\"revision\":\"r2\",\"document\":\"scene\",\"objects\":["
        "{\"id\":\"prototype:player\",\"name\":\"Player\",\"kind\":"
        "\"prototype\","
        "\"fields\":[{\"id\":\"GAME.GameCollider.shape\",\"name\":\"shape\","
        "\"label\":\"Collider Shape\",\"type\":\"choice\",\"value\":1,"
        "\"choices\":[{\"label\":\"Circle\",\"value\":0},{\"label\":\"Box\","
        "\"value\":1}]}]}]}}";
    field_packet = bound_packet(hitbox, "prototype:player", "GAME.GameCollider.shape");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            yyjson_is_int(yyjson_obj_get(operation, "value")) &&
            yyjson_get_int(yyjson_obj_get(operation, "value")) == 0,
        "hitbox command chooses the reflected Circle enum value"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);
    phrase = command_packet(hitbox, "Make the player's hitbox round");
    field_packet = bound_packet(phrase, "prototype:player", "GAME.GameCollider.shape");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            yyjson_get_int(yyjson_obj_get(operation, "value")) == 0,
        "round hitbox maps to the Circle choice"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);
    free(phrase);

    GameParticleEmitter emitter = game_particle_emitter_defaults();
    pin_value initial, changed = { .type = PIN_F32, .f = 5 };
    int nested = pin_read_field_path(
        &emitter,
        GAME_COMPONENT_GameParticleEmitter.type,
        "size.start.x",
        &initial
    );
    nested |= pin_write_field_path(&emitter, GAME_COMPONENT_GameParticleEmitter.type, "size.start.x", changed);
    test_check(
        !nested && initial.type == PIN_F32 && initial.f == 2 &&
            emitter.size.start.x == 5,
        "reflected nested particle size can be read and written"
    );

    const char *particles =
        "{\"command\":\"Increase the size of the particles\",\"catalog\":{"
        "\"revision\":\"r3\",\"document\":\"scene\",\"objects\":["
        "{\"id\":\"prototype:burst\",\"name\":\"Burst\",\"kind\":\"prototype\","
        "\"components\":[\"GameParticleEmitter\"],\"fields\":["
        "{\"id\":\"GAME.GameParticleEmitter.size.start.x\",\"name\":\"size."
        "start.x\","
        "\"label\":\"Particle size start x\",\"type\":\"number\",\"value\":4},"
        "{\"id\":\"GAME.GameParticleEmitter.size.start.y\",\"name\":\"size."
        "start.y\","
        "\"label\":\"Particle size start "
        "y\",\"type\":\"number\",\"value\":6}]}]}}";
    result = local_result(particles);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        !strcmp(command_string(root, "status"), "continue") &&
            !strcmp(command_string(root, "value"), "prototype:burst"),
        "particle request finds the emitter prototype from its component"
    );
    yyjson_doc_free(read);
    free(result);
    field_packet = bound_packet(particles, "prototype:burst", "GAME.GameParticleEmitter.size.start.x");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    yyjson_val *operations = yyjson_obj_get(root, "operations");
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            yyjson_arr_size(operations) == 2 &&
            yyjson_get_num(yyjson_obj_get(yyjson_arr_get(operations, 0), "value")) == 5 &&
            yyjson_get_num(yyjson_obj_get(yyjson_arr_get(operations, 1), "value")) == 7.5,
        "particle size request scales both start values in one proposal"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);

    const char *percent_particles =
        "{\"command\":\"Increase particle size by 50%\",\"catalog\":{"
        "\"revision\":\"r4\",\"document\":\"scene\",\"objects\":["
        "{\"id\":\"prototype:burst\",\"name\":\"Burst\",\"kind\":\"prototype\","
        "\"fields\":["
        "{\"id\":\"GAME.GameParticleEmitter.size.start.x\",\"name\":\"size."
        "start.x\",\"type\":\"number\",\"value\":4},"
        "{\"id\":\"GAME.GameParticleEmitter.size.start.y\",\"name\":\"size."
        "start.y\",\"type\":\"number\",\"value\":6}]}]}}";
    field_packet = bound_packet(percent_particles, "prototype:burst", "GAME.GameParticleEmitter.size.start.x");
    result = local_result(field_packet);
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    operations = yyjson_obj_get(root, "operations");
    test_check(
        !strcmp(command_string(root, "status"), "needs_review") &&
            yyjson_get_num(yyjson_obj_get(yyjson_arr_get(operations, 0), "value")) == 6 &&
            yyjson_get_num(yyjson_obj_get(yyjson_arr_get(operations, 1), "value")) == 9,
        "particle percentage edits scale both axes from their current values"
    );
    yyjson_doc_free(read);
    free(result);
    free(field_packet);

    const char *
        ambiguous = "{\"command\":\"Change Ship's color to blue\",\"catalog\":{"
                    "\"revision\":\"r1\",\"document\":\"scene\",\"objects\":["
                    "{\"id\":\"entity:a\",\"name\":\"Ship\",\"kind\":"
                    "\"entity\",\"prototype\":\"Player\"},"
                    "{\"id\":\"entity:b\",\"name\":\"Ship\",\"kind\":"
                    "\"entity\",\"prototype\":\"Enemy\"},"
                    "{\"id\":\"prototype:player\",\"name\":\"Player\",\"kind\":"
                    "\"prototype\"},"
                    "{\"id\":\"prototype:enemy\",\"name\":\"Enemy\",\"kind\":"
                    "\"prototype\"}]}}";
    char *request = editor_command_model_prepare(ambiguous, error, sizeof error);
    read = request ? yyjson_read(request, strlen(request), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    const char *body = command_string(root, "body");
    test_check(
        !strcmp(command_string(root, "url"), "http://127.0.0.1:8768/v1/decide") &&
            !strcmp(command_string(root, "auth_token"), "test-key") &&
            strstr(body, "Player prototype") && strstr(body, "Enemy prototype"),
        "ambiguous targets produce a bounded authenticated Laya choice request"
    );
    yyjson_doc_free(read);
    free(request);
    assert(editor_command_model_set_settings("127.0.0.1:8768", "laya", error, sizeof error));
    request = editor_command_model_prepare(ambiguous, error, sizeof error);
    read = request ? yyjson_read(request, strlen(request), 0) : NULL;
    test_check(
        !strcmp(command_string(yyjson_doc_get_root(read), "url"), "http://127.0.0.1:8768/v1/decide"),
        "Laya accepts a host without an explicit scheme or path"
    );
    yyjson_doc_free(read);
    free(request);
    assert(
        editor_command_model_set_settings("http://example.com/v1/decide", "laya", error, sizeof error)
    );
    request = editor_command_model_prepare(ambiguous, error, sizeof error);
    read = request ? yyjson_read(request, strlen(request), 0) : NULL;
    test_check(
        strstr(command_string(yyjson_doc_get_root(read), "local_response"), "local tunnel") !=
            NULL,
        "native Laya does not send its API key to a nonlocal HTTP host"
    );
    yyjson_doc_free(read);
    free(request);
    assert(
        editor_command_model_set_settings("http://127.0.0.1:8768/v1/decide", "laya", error, sizeof error)
    );
    result = editor_command_model_result(
        ambiguous,
        "{\"answers\":{\"action\":{\"choice\":\"0\",\"confidence\":0.51,"
        "\"probabilities\":{\"0\":0.51,\"1\":0.49}}}}",
        error,
        sizeof error
    );
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        !strcmp(command_string(root, "status"), "needs_clarification") &&
            yyjson_arr_size(yyjson_obj_get(root, "choices")) == 2,
        "low confidence asks the user to choose a catalog target"
    );
    yyjson_doc_free(read);
    free(result);
    result = editor_command_model_result(
        ambiguous,
        "{\"answers\":{\"action\":{\"choice\":\"0\",\"confidence\":0.98,"
        "\"probabilities\":{\"0\":0.98,\"1\":0.02}}}}",
        error,
        sizeof error
    );
    read = result ? yyjson_read(result, strlen(result), 0) : NULL;
    root = read ? yyjson_doc_get_root(read) : NULL;
    test_check(
        !strcmp(command_string(root, "status"), "continue") &&
            !strcmp(command_string(root, "value"), "prototype:player"),
        "confident Laya answer selects the catalog target"
    );
    yyjson_doc_free(read);
    free(result);
    editor_command_laya_set_token("");
    emitter_decision("Increase the boost particles size", false, "prototype:boost");
    emitter_decision("Increase the smoke particles size", true, "prototype:smoke");
    emitter_decision("Make the boost particles larger", true, "prototype:boost");
    emitter_decision("Make the smoke particles larger", false, "prototype:smoke");
    emitter_decision("Increase the particles size", false, NULL);
    scene_command();
    editor_command_model_set_settings(old_host, old_model, error, sizeof error);
}
