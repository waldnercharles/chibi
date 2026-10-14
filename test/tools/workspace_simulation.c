#include "authoring/workspace_simulation.h"

#include "authoring/workspace_simulation_definition.h"
#include "execution/function.h"
#include "execution/status.h"
#include "runtime/components.h"
#include "runtime/engine_input.h"
#include "support/asset_json.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static arena native_storage;
static char *json;
static input_keyframe *decoded;
static ecs_world *test_world;
static source_registry *sources;
static engine *runtime;
static engine_input *input;
static const char
    ID[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static void cleanup()
{
    arena_free(&native_storage);
    free(json);
    json = NULL;
    free(decoded);
    decoded = NULL;
    if (input) {
        engine_input_free(input);
    }
    input = NULL;
    engine_free(runtime);
    runtime = NULL;
    if (sources) {
        source_registry_free(sources);
    }
    sources = NULL;
    ecs_world_free(test_world);
    test_world = NULL;
}
TEST_CASE(simulation_timeline_roundtrips_exact_wide_controls_and_ordered_transitions)
{
    input_keyframe keys[3] = {
        { .step = 0,
          .movement = { .25f, -.5f },
          .aim = { 3, 7 },
          .aim_point = true,
          .firing = true,
          .controls = (UINT64_C(1) << CONTROL_PAD_RIGHT_TRIGGER) |
                      (UINT64_C(1) << CONTROL_A),
          .controllers = { { .connected = true, .buttons = UINT32_C(1) << PAD_A } },
          .pointer = { .origin = { 1, 2, 3 }, .direction = { 0, -1, 0 }, .active = true } },
        { .step = 0, .controls = UINT64_C(1) << CONTROL_B },
        { .step = 4, .movement = { 1, 0 } }
    };
    char error[256] = "";
    REQUIRE(workspace_simulation_encode(keys, 3, 2147483647u, 8, ID, &json, error, sizeof error));
    int count = 0;
    unsigned seed = 0;
    uint64_t steps = 0;
    REQUIRE(
        workspace_simulation_decode(json, ID, &decoded, &count, &seed, &steps, error, sizeof error)
    );
    REQUIRE(count == 3 && seed == 2147483647u && steps == 8);
    for (int i = 0; i < 3; i++) {
        REQUIRE(
            decoded[i].step == keys[i].step &&
            input_keyframe_same(&decoded[i], &keys[i])
        );
    }
    REQUIRE(
        decoded[0].controls ==
        ((UINT64_C(1) << CONTROL_PAD_RIGHT_TRIGGER) | (UINT64_C(1) << CONTROL_A))
    );
    free(decoded);
    decoded = NULL;
    char *replacement = NULL;
    REQUIRE(
        workspace_simulation_encode(keys, 3, 2147483647u, 8, ID, &replacement, error, sizeof error)
    );
    REQUIRE(!strcmp(json, replacement));
    free(replacement);
    return true;
}
TEST_CASE(simulation_timeline_refuses_malformed_data_without_publishing_outputs)
{
    char error[256];
    input_keyframe key = { 0 };
    REQUIRE(workspace_simulation_encode(&key, 1, 7, 0, ID, &json, error, sizeof error));
    input_keyframe sentinel = { 0 }, *unchanged = &sentinel;
    int count = 91;
    unsigned seed = 92;
    uint64_t steps = 93;
    REQUIRE(!workspace_simulation_decode(
        json,
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        &unchanged,
        &count,
        &seed,
        &steps,
        error,
        sizeof error
    ));
    REQUIRE(unchanged == &sentinel && count == 91 && seed == 92 && steps == 93);
    const char *bad[] = { "{\"keys\":[[[[[[[0]]]]]]],\"version\":1}",
                          "[]",
                          "{\"version\":1,\"identity\":true}",
                          "{} null" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            !workspace_simulation_decode(bad[i], ID, &unchanged, &count, &seed, &steps, error, sizeof error)
        );
        REQUIRE(unchanged == &sentinel && count == 91 && seed == 92 && steps == 93);
    }
    /* A current timeline must declare the empty save baseline explicitly.
     * Historical v1, omitted baseline and nonempty saves all fail closed. */
    for (int variant = 0; variant < 3; variant++) {
        av_doc *candidate = av_json_read(json);
        REQUIRE(candidate);
        av_value *root = av_doc_root(candidate);
        if (variant == 0) {
            av_obj_set(candidate, root, "version", av_sint(candidate, 1));
        } else if (variant == 1) {
            REQUIRE(av_obj_remove_str(root, "initial_save"));
        } else {
            av_value *initial_save = av_obj_get(root, "initial_save");
            av_obj_add_str(candidate, initial_save, "prior-session", "99");
        }
        char *invalid = av_json_write(root);
        av_doc_free(candidate);
        bool accepted = workspace_simulation_decode(
            invalid,
            ID,
            &unchanged,
            &count,
            &seed,
            &steps,
            error,
            sizeof error
        );
        free(invalid);
        REQUIRE(!accepted);
        REQUIRE(unchanged == &sentinel && count == 91 && seed == 92 && steps == 93);
    }
    char *old = json;
    REQUIRE(
        !workspace_simulation_encode(&key, WORKSPACE_TIMELINE_KEYS_MAX + 1, 7, 0, ID, &json, error, sizeof error)
    );
    REQUIRE(json == old);
    key.movement.x = NAN;
    REQUIRE(!workspace_simulation_encode(&key, 1, 7, 0, ID, &json, error, sizeof error));
    REQUIRE(json == old);
    uint64_t controls = 99;
    const char *names[] = { "W", "ArrowLeft", "MouseRight", "9" };
    REQUIRE(workspace_simulation_keys(names, 4, &controls, error, sizeof error));
    REQUIRE(
        controls ==
        ((UINT64_C(1) << CONTROL_W) | (UINT64_C(1) << CONTROL_ARROW_LEFT) |
         (UINT64_C(1) << CONTROL_MOUSE_RIGHT) | (UINT64_C(1) << CONTROL_9))
    );
    const char *unknown[] = { "W", "not-a-control" };
    controls = 99;
    REQUIRE(
        !workspace_simulation_keys(unknown, 2, &controls, error, sizeof error) &&
        controls == 99
    );
    return true;
}
TEST_CASE(simulation_bounded_recording_and_queued_devices_refuse_partial_mutation)
{
    test_world = ecs_world_new();
    REQUIRE(test_world);
    input = engine_input_new(test_world, NULL);
    REQUIRE(input);
    REQUIRE(engine_input_limit_trail(input, 1));
    input_controller_state controllers[INPUT_CONTROLLER_MAX] = { 0 };
    bool firing = false;
    uint64_t a = UINT64_C(1) << CONTROL_A, b = UINT64_C(1) << CONTROL_B;
    REQUIRE(engine_input_devices(input, a, controllers, &firing) == FLOW_OK);
    REQUIRE(!engine_input_devices_admissible(input, b, controllers));
    REQUIRE(engine_input_devices(input, b, controllers, &firing) == FLOW_ERR_ARGUMENT);
    REQUIRE(engine_input_held(input).controls == a);
    REQUIRE(engine_input_devices(input, a, controllers, &firing) == FLOW_OK);
    const input_keyframe *trail;
    REQUIRE(engine_input_trail(input, &trail) == 1 && trail[0].controls == a);
    REQUIRE(!engine_input_limit_trail(input, -1));
    REQUIRE(engine_input_limit_trail(input, 0));
    REQUIRE(
        engine_input_devices(input, b, controllers, &firing) == FLOW_OK &&
        engine_input_held(input).controls == b
    );
    engine_input_free(input);
    input = NULL;
    sources = source_registry_new(test_world);
    REQUIRE(sources);
    runtime = engine_new(test_world, &(engine_modules){ .input = true, .sources = sources });
    REQUIRE(WORKSPACE_SIMULATION_DECLARATION_PACK.binding_count == WORKSPACE_SIMULATION_OPERATION_COUNT);
    for (int i = 0; i < WORKSPACE_SIMULATION_OPERATION_COUNT; i++) {
        const native_definition_binding *binding = &WORKSPACE_SIMULATION_DECLARATION_PACK
                                                        .bindings[i];
        REQUIRE(binding->implementation.call && binding->implementation.call->context);
        REQUIRE(binding->implementation.call->optional_service);
        REQUIRE(
            !strcmp(binding->implementation.call->context(NULL, NULL, NULL, NULL), "Workspace simulation requires an authoring host")
        );
        REQUIRE(!strcmp(binding->implementation.call->service, "workspace_simulation"));
        REQUIRE(
            !strcmp(binding->canonical_name, WORKSPACE_SIMULATION_DEFINITIONS[i].canonical_name)
        );
    }
    REQUIRE(runtime && engine_has_input(runtime));
    REQUIRE(!engine_record_input_bounded(runtime, 7, 0));
    REQUIRE(engine_record_input_bounded(runtime, 7, 1));
    REQUIRE(engine_recording_input(runtime));
    REQUIRE(engine_is_playing(runtime));
    REQUIRE(engine_step(runtime) == FLOW_ERR_STATE);
    REQUIRE(engine_set_playing(runtime, false) == FLOW_OK);
    REQUIRE(engine_step(runtime) == FLOW_OK);
    REQUIRE(!engine_frame(runtime, 0));
    int count = 0;
    const input_keyframe *keys = engine_recorded_input(runtime, &count);
    REQUIRE(count == 1 && keys[0].step == 0);
    REQUIRE(!engine_step(runtime));
    REQUIRE(!engine_sample_controls(runtime, a, &firing));
    REQUIRE(!engine_frame(runtime, ENGINE_FIXED_SECONDS));
    keys = engine_recorded_input(runtime, &count);
    REQUIRE(count == 1 && keys[0].step == 0 && !keys[0].controls);
    REQUIRE(!engine_recording_input(runtime) && engine_recorded_input_truncated(runtime));
    return true;
}
static const char *native_string(void *storage, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
TEST_CASE(simulation_optional_bindings_refuse_inactive_project_without_reading_inputs)
{
    behavior_function_context context = { .value_storage = &native_storage,
                                          .copy_string = native_string };
    const native_definition_pack *pack = &WORKSPACE_SIMULATION_DEFINITION_PACK;
    REQUIRE(pack->binding_count == WORKSPACE_SIMULATION_OPERATION_COUNT);
    for (int b = 0; b < pack->binding_count; b++) {
        const node_host_call *call = pack->bindings[b].implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        pin_value out[15];
        memset(out, 0xa5, sizeof out);
        REQUIRE(!call->context(&context, NULL, NULL, out));
        REQUIRE(!out[0].b && !out[1].b && !out[2].b && !out[3].b);
        REQUIRE(out[10].str && strstr(out[10].str, "unavailable"));
        REQUIRE(out[9].str && !out[9].str[0] && out[11].str && !out[11].str[0]);
        REQUIRE(!out[6].i && !out[7].i && !out[12].i && !out[13].b && !out[14].i);
    }
    return true;
}
void test_workspace_simulation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(simulation_optional_bindings_refuse_inactive_project_without_reading_inputs);
    RUN_TEST_CASE(simulation_timeline_roundtrips_exact_wide_controls_and_ordered_transitions);
    RUN_TEST_CASE(simulation_timeline_refuses_malformed_data_without_publishing_outputs);
    RUN_TEST_CASE(simulation_bounded_recording_and_queued_devices_refuse_partial_mutation);
    pu_clear_setup();
}
