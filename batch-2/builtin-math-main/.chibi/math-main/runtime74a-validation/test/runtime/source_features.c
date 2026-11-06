#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/source_edit.h"
#include "authoring/source_prepare.h"
#include "ecs/ecs.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "render/shader_catalog.h"
#include "render/shader_parameters.h"
#include "render/shader_publish.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/engine_input.h"
#include "runtime/math_native.h"
#include "runtime/particles.h"
#include "runtime/scenes/physics3d.h"
#include "runtime/shaders/prepared.h"
#include "runtime/url.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical replacements exercise the owning native capabilities through real
 * source modules; graph executor/oracle/private artifact state is absent. */
static authoring_language_catalog *catalog;
static authoring_source *prepared;
static execution_program *program;
static execution_instance *instance;
static ecs_world *test_world;
static particles *test_particles, *expected_particles;
static entities *test_entities;
static physics3d *test_physics3d;
static url_requests *test_urls;
static engine_input *input_service;
static declared_components *declared, *newer_declared;
static shader_prepared_asset shader_record;
static unsigned char *shader_bytes;
static player_resource_host resource_host;
static void *resource_generation;
static uint64_t done_id, vector_id, target_id;
static bool fixture_natives;
static struct
{
    CF_V3 vector;
    uint64_t target;
    int calls;
} captured;

/* These fixture capabilities exercise argument retention and live runtime
 * context without depending on an optional gameplay feature. */
static const char *capture(void *, const pin_value *arguments, pin_value *)
{
    captured.vector = *(const CF_V3 *)arguments[0].ptr;
    captured.target = arguments[1].u64;
    captured.calls++;
    return NULL;
}
static const char *read_clock(
    const behavior_function_context *context,
    void *,
    const pin_value *,
    pin_value *out
)
{
    if (!context->instance_time) {
        return "Fixture clock requires an instance clock";
    }
    out[0].d = *context->instance_time;
    return NULL;
}
static const native_parameter_definition CAPTURE_INPUTS[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    { .name = "Vector", .storage = PIN_STRUCT, .type_name = "vec3" },
    { .name = "Target",
      .storage = PIN_HANDLE,
      .type_name = "ecs.entity",
      .has_default = true,
      .default_owner = true,
      .default_value = { .type = PIN_HANDLE } },
};
static const native_parameter_definition CAPTURE_OUTPUTS[] = {
    { .name = "Then", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
};
static const native_parameter_definition CLOCK_OUTPUTS[] = {
    { .name = "Time", .storage = PIN_F64, .type_name = "f64" },
};
static const native_capability_definition FIXTURE_DEFINITIONS[] = {
    { .canonical_name = "Source Fixture Capture",
      .source_name = "fixture_capture",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = CAPTURE_INPUTS,
      .input_count = NATIVE_COUNT(CAPTURE_INPUTS),
      .outputs = CAPTURE_OUTPUTS,
      .output_count = NATIVE_COUNT(CAPTURE_OUTPUTS),
      .completion_output = 0 },
    { .canonical_name = "Source Fixture Clock",
      .source_name = "fixture_clock",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .outputs = CLOCK_OUTPUTS,
      .output_count = NATIVE_COUNT(CLOCK_OUTPUTS),
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CAPTURE_CALL = { .invoke = capture };
static const node_host_call CLOCK_CALL = { .context = read_clock };
static const native_definition_binding FIXTURE_BINDINGS[] = {
    { .canonical_name = "Source Fixture Capture",
      .implementation = { .call = &CAPTURE_CALL } },
    { .canonical_name = "Source Fixture Clock",
      .implementation = { .call = &CLOCK_CALL } },
};
static const native_definition_pack FIXTURE_PACK = {
    .definitions = FIXTURE_DEFINITIONS,
    .definition_count = NATIVE_COUNT(FIXTURE_DEFINITIONS),
    .bindings = FIXTURE_BINDINGS,
    .binding_count = NATIVE_COUNT(FIXTURE_BINDINGS),
};
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    instance = NULL;
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
    if (test_urls) {
        url_requests_free(test_urls);
    }
    test_urls = NULL;
    if (input_service) {
        engine_input_free(input_service);
    }
    input_service = NULL;
    if (test_particles) {
        particles_free(test_particles);
    }
    test_particles = NULL;
    if (expected_particles) {
        particles_free(expected_particles);
    }
    expected_particles = NULL;
    if (test_physics3d) {
        physics3d_free(test_physics3d);
    }
    test_physics3d = NULL;
    if (test_entities) {
        entities_free(test_entities);
    }
    test_entities = NULL;
    if (test_world) {
        render_shader_clear_world(test_world);
    }
    ecs_world_free(test_world);
    test_world = NULL;
    declared_components_free(newer_declared);
    newer_declared = NULL;
    declared_components_free(declared);
    declared = NULL;
    if (resource_generation) {
        resource_host.retire(resource_host.context, resource_generation);
    }
    resource_generation = NULL;
    shader_prepared_asset_free(&shader_record);
    free(shader_bytes);
    shader_bytes = NULL;
    fixture_natives = false;
    memset(&captured, 0, sizeof captured);
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < prepared->analysis->symbol_count; i++) {
        const script_symbol *s = &prepared->analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, name)) {
            return s->id;
        }
    }
    return 0;
}
static bool prepare(const char *text, behavior_backend backend)
{
    char error[256];
    native_capabilities *native;
    if (fixture_natives) {
        const native_definition_pack extra[] = { RENDER_SHADER_PARAMETER_DEFINITION_PACK,
                                                 FIXTURE_PACK };
        native = host_native_default_catalog_new(NULL, 0, extra, NATIVE_COUNT(extra), error, sizeof error);
    } else {
        native = authoring_application_catalog_new(false, error, sizeof error);
    }
    if (!native) {
        fprintf(stderr, "source feature catalog: %s\n", error);
        return false;
    }
    authoring_language_catalog_input input = { .native = native };
    catalog = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(native);
    if (!catalog) {
        fprintf(stderr, "source feature declarations: %s\n", error);
        return false;
    }
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    prepared = authoring_source_prepare_document(
        "features.chibi",
        "test/native-features",
        text,
        authoring_language_catalog_view(catalog),
        &bindings,
        backend
    );
    if (!prepared->valid) {
        for (int i = 0; i < prepared->diagnostic_count; i++) {
            fprintf(
                stderr,
                "source feature compile: %s\n",
                prepared->diagnostics[i].message
            );
        }
        return false;
    }
    done_id = state_id("done");
    vector_id = state_id("vector");
    target_id = state_id("target");
    service_binding *services = NULL;
    if (test_urls) {
        apush(services, ((service_binding){ URL_SERVICE, test_urls }));
    }
    if (test_particles) {
        apush(services, ((service_binding){ "game.particles", test_particles }));
    }
    if (test_entities) {
        apush(services, ((service_binding){ "game.entities", test_entities }));
    }
    if (test_physics3d) {
        apush(services, ((service_binding){ "game.physics3d", test_physics3d }));
    }
    program = execution_program_new(prepared->module, test_world, services);
    afree(services);
    if (!program) {
        fprintf(stderr, "source feature program rejected required services/layout\n");
    }
    return program != NULL;
}
static void retire_producer()
{
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
}
static int dispatch(uint64_t event, unsigned budget)
{
    int status = execution_instance_dispatch(instance, event, NULL, 0, 0, 0, budget);
    if (status < 0) {
        const execution_program_failure *failure = execution_program_error(program);
        fprintf(
            stderr,
            "source feature dispatch: %d %s\n",
            status,
            failure ? failure->message : "no retained diagnostic"
        );
    }
    return status;
}
static int pump(double now, double delta, unsigned budget)
{
    int status = execution_program_pump(program, now, delta, budget);
    if (status < 0) {
        const execution_program_failure *failure = execution_program_error(program);
        fprintf(
            stderr,
            "source feature pump: %d %s\n",
            status,
            failure ? failure->message : "no retained diagnostic"
        );
    }
    return status;
}
TEST_CASE(source_native_call_preserves_delay_snapshot_and_target)
{
    const char
        *format = "Capture :: behavior { done:retain bool=false; "
                  "vector:retain vec3={x:-1.0,y:2.0,z:3.0}; "
                  "target:retain entity=0; Apply :: "
                  "(Vector:vec3,Target:entity)->(){ "
                  "await delay(Duration:0.125); await "
                  "fixture_capture(Vector:Vector%s); } on "
                  "Tick :: () { await Apply(Vector:vector,Target:target); "
                  "done=true; } }";
    for (int backend = 0; backend < 2; backend++) {
        for (int explicit_target = 0; explicit_target < 2; explicit_target++) {
            char text[1024];
            snprintf(text, sizeof text, format, explicit_target ? ",Target:Target" : "");
            test_world = ecs_world_new();
            ecs_entity owner = ecs_world_create_entity(test_world),
                       other = ecs_world_create_entity(test_world);
            fixture_natives = true;
            REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            uint64_t tick = execution_module_event(prepared->module, "Tick");
            REQUIRE(tick && done_id && vector_id && target_id);
            instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance);
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    target_id,
                    (pin_value){ .type = PIN_HANDLE,
                                 .type_name = "ecs.entity",
                                 .u64 = explicit_target ? ecs_entity_to_u64(other) : 0 }
                ) == 0
            );
            retire_producer();
            REQUIRE(dispatch(tick, 1024) >= 0);
            REQUIRE(execution_instance_pending(instance) > 0);
            REQUIRE(!execution_instance_state(instance, done_id)->b);
            REQUIRE(captured.calls == 0);
            CF_V3 changed = { 99, 99, 99 };
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    vector_id,
                    (pin_value){ .type = PIN_STRUCT, .type_name = "vec3", .ptr = &changed }
                ) == 0
            );
            REQUIRE(pump(.125, .125, 1024) >= 0);
            REQUIRE(
                execution_instance_pending(instance) == 0 &&
                execution_instance_state(instance, done_id)->b
            );
            REQUIRE(captured.calls == 1);
            REQUIRE(
                captured.vector.x == -1 && captured.vector.y == 2 &&
                captured.vector.z == 3
            );
            REQUIRE(captured.target == ecs_entity_to_u64(explicit_target ? other : owner));
            cleanup();
        }
    }
    return true;
}
TEST_CASE(source_native_context_samples_live_clock_after_nested_wait)
{
    const char *text =
        "Clock :: behavior { initial:retain f64=-1; resumed:retain f64=-1; "
        "Read :: ()->f64 { return fixture_clock(); } "
        "Apply :: ()->f64 { await delay(Duration:0.125); return Read(); } "
        "on Tick :: () { initial=Read(); resumed=await Apply(); } }";
    for (int backend = 0; backend < 2; backend++) {
        fixture_natives = true;
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        uint64_t initial = state_id("initial"), resumed = state_id("resumed");
        REQUIRE(tick && initial && resumed);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_pending(instance) > 0);
        REQUIRE(execution_instance_state(instance, initial)->d == 0);
        REQUIRE(execution_instance_state(instance, resumed)->d == -1);
        /* Update advances the live instance clock beyond the wait deadline. */
        REQUIRE(execution_program_update(program, .375, .375, 1024) >= 0);
        REQUIRE(execution_instance_pending(instance) == 0);
        REQUIRE(execution_instance_state(instance, resumed)->d == .375);
        cleanup();
    }
    return true;
}
TEST_CASE(source_native_particles_preserve_nested_wait_and_exact_emission)
{
    const char *
        text = "Particles :: behavior { done:retain bool=false; Emit :: "
               "()->(){ await "
               "emit_particles(Position:{x:1.0,y:-2.0},Color:4040002047,Count:"
               "3,Speed:80.0,Lifetime:0.3); await delay(Duration:0.125); await "
               "emit_particles(Position:{x:1.0,y:-2.0},Color:4040002047,Count:"
               "3,Speed:80.0,Lifetime:0.3); } on Tick :: () { Emit(); "
               "done=true; } }";
    for (int backend = 0; backend < 2; backend++) {
        test_particles = particles_new();
        expected_particles = particles_new();
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        REQUIRE(tick && done_id);
        REQUIRE(execution_program_bind_service(program, "game.particles", test_particles) == 0);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        particle_burst burst = { .position = { 1, -2 },
                                 .color = 4040002047u,
                                 .count = 3,
                                 .speed = 80,
                                 .lifetime = .3f };
        REQUIRE(particles_emit(expected_particles, &burst) == 3);
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(
            particles_count(test_particles) == 3 &&
            execution_instance_pending(instance) > 0
        );
        REQUIRE(!memcmp(
            particles_data(test_particles),
            particles_data(expected_particles),
            3 * sizeof(particle)
        ));
        REQUIRE(particles_emit(expected_particles, &burst) == 3);
        REQUIRE(pump(.125, .125, 1024) >= 0);
        REQUIRE(
            particles_count(test_particles) == 6 &&
            execution_instance_pending(instance) == 0
        );
        REQUIRE(execution_instance_state(instance, done_id)->b);
        REQUIRE(!memcmp(
            particles_data(test_particles),
            particles_data(expected_particles),
            6 * sizeof(particle)
        ));
        cleanup();
    }
    return true;
}
TEST_CASE(source_native_motion_retains_numeric_outputs)
{
    const char *text =
        "Motion :: behavior { input:retain f64=0; angle:retain f64=0; "
        "smooth:retain f64=0; root:retain f64=0; low:retain f64=0; "
        "high:retain f64=0; nearest:retain f64=0; position:retain "
        "number=0; velocity:retain number=0; Calculate :: "
        "(Value:f64)->(Angle:f64,Smooth:f64,Root:f64,Low:f64,"
        "High:f64,Nearest:f64){ "
        "return(Angle:quantize_angle(Angle:Value,Step:1.5707963267948966),"
        "Smooth:smooth_step(Value:Value),Root:sqrt(Value:Value),Low:floor("
        "Value:Value),High:ceil(Value:Value),Nearest:round(Value:Value)); } on "
        "Tick :: () { r:=Calculate(Value:input); angle=r.Angle; "
        "smooth=r.Smooth; "
        "root=r.Root; low=r.Low; high=r.High; nearest=r.Nearest; "
        "spring:=advance_damped_spring(Position:0.0,Velocity:0.0,Target:1.0,"
        "Frequency:1.0,Damping:1.0,Delta:0.125); position=spring.Position; "
        "velocity=spring.Velocity; } }";
    const char *names[] = { "angle", "smooth",  "root",     "low",
                            "high",  "nearest", "position", "velocity" };
    const double samples[] = {
        NAN, INFINITY,         -INFINITY,         -0.0, 0.0, -.5, .5, -1.5,
        1.5, nextafter(.5, 0), nextafter(-.5, 0), .125
    };
    pin_value (*const kernels[])(const pin_value *, int, arena *) = {
        native_math_Quantize_Angle, native_math_Smooth_Step,
        native_math_SquareRoot,     native_math_Floor,
        native_math_Ceiling,        native_math_Round
    };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 input = state_id("input"), ids[8];
        REQUIRE(tick && input);
        for (int i = 0; i < 8; i++) {
            ids[i] = state_id(names[i]);
            REQUIRE(ids[i]);
        }
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        for (int sample = 0; sample < 12; sample++) {
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    input,
                    (pin_value){ .type = PIN_F64, .d = samples[sample] }
                ) == 0
            );
            if (!isfinite(samples[sample])) {
                pin_value inputs[] = { { .type = PIN_F64, .d = samples[sample] },
                                       { .type = PIN_F64, .d = 1.5707963267948966 } };
                REQUIRE(native_math_Quantize_Angle(inputs, 2, NULL).type == PIN_FLOW);
                REQUIRE(dispatch(tick, 1024) == FLOW_ERR_INVALID_GRAPH);
                const execution_program_failure *failure = execution_program_error(program);
                REQUIRE(failure && failure->status == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(!strcmp(failure->message, "Native value operation failed"));
                REQUIRE(execution_instance_pending(instance) == 0);
                for (int i = 0; i < 8; i++) {
                    const pin_value *value = execution_instance_state(instance, ids[i]);
                    REQUIRE(value && (i < 6 ? value->d == 0 : value->f == 0));
                }
                execution_program_clear_error(program);
                continue;
            }
            REQUIRE(dispatch(tick, 1024) >= 0);
            REQUIRE(execution_instance_pending(instance) == 0);
            for (int i = 0; i < 6; i++) {
                pin_value args[] = { { .type = PIN_F64, .d = samples[sample] },
                                     { .type = PIN_F64, .d = 1.5707963267948966 } };
                pin_value expected = kernels[i](args, i ? 1 : 2, NULL);
                const pin_value *actual = execution_instance_state(instance, ids[i]);
                REQUIRE(actual && actual->type == PIN_F64);
                REQUIRE(
                    isnan(expected.d) ? isnan(actual->d) : actual->d == expected.d
                );
                if (expected.d == 0 && actual->d == 0) {
                    REQUIRE(signbit(actual->d) == signbit(expected.d));
                }
            }
            pin_value spring[] = { { .type = PIN_F32, .f = 0 },
                                   { .type = PIN_F32, .f = 0 },
                                   { .type = PIN_F32, .f = 1 },
                                   { .type = PIN_F32, .f = 1 },
                                   { .type = PIN_F32, .f = 1 },
                                   { .type = PIN_F32, .f = .125f } },
                      result[2] = { { 0 } };
            REQUIRE(!native_math_spring(spring, result));
            REQUIRE(execution_instance_state(instance, ids[6])->f == result[0].f);
            REQUIRE(execution_instance_state(instance, ids[7])->f == result[1].f);
            REQUIRE(result[0].f > 0 && result[0].f < 1 && result[1].f > 0);
        }
        cleanup();
    }
    return true;
}
/* random_range of numbers fails on a nonfinite bound, whichever bound it is,
 * and draws between the bounds in either order. */
TEST_CASE(source_native_number_random_range_checks_both_bounds)
{
    const char
        *text = "Draw :: behavior { lo:retain number=0; hi:retain number=1; "
                "out:retain number=0; on Tick :: () { out=random_range(Min:lo,"
                "Max:hi); } }";
    const float bounds[][2] = { { NAN, 1 },      { 1, NAN },
                                { INFINITY, 1 }, { 1, -INFINITY },
                                { 2, 1 },        { 1, 2 },
                                { 3, 3 },        { -0.5f, -0.25f } };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 lo = state_id("lo"), hi = state_id("hi"), out = state_id("out");
        REQUIRE(tick && lo && hi && out);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        for (size_t i = 0; i < sizeof bounds / sizeof *bounds; i++) {
            float min = bounds[i][0], max = bounds[i][1];
            REQUIRE(
                execution_instance_set_state(instance, lo, (pin_value){ .type = PIN_F32, .f = min }) == 0
            );
            REQUIRE(
                execution_instance_set_state(instance, hi, (pin_value){ .type = PIN_F32, .f = max }) == 0
            );
            if (!isfinite(min) || !isfinite(max)) {
                REQUIRE(dispatch(tick, 1024) == FLOW_ERR_INVALID_GRAPH);
                const execution_program_failure *failure = execution_program_error(program);
                REQUIRE(failure && !strcmp(failure->message, "Native value operation failed"));
                execution_program_clear_error(program);
                continue;
            }
            REQUIRE(dispatch(tick, 1024) >= 0);
            const pin_value *value = execution_instance_state(instance, out);
            REQUIRE(value && value->type == PIN_F32);
            REQUIRE(value->f >= fminf(min, max) && value->f <= fmaxf(min, max));
        }
        cleanup();
    }
    return true;
}
/* Origins at a source offset; NULL when the presentation has none. */
static const source_presentation_origin *origin_at(
    const authoring_source_presentation *view,
    source_presentation_kind kind,
    const char *text
)
{
    const char *start = strstr(prepared->analysis->text, text);
    assert(start);
    int offset = (int)(start - prepared->analysis->text);
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind == kind && !o->pin && o->range.start == offset) {
            return o;
        }
    }
    return NULL;
}
/* 16,777,217 is the first integer an f32 rounds. A wire from an int into an
 * f64 keeps it; an explicit To Real still yields a number. */
TEST_CASE(source_int_wire_into_f64_keeps_every_digit)
{
    REQUIRE(prepare(
        "Widen :: behavior { total:retain f64=0; real:retain number=0; on Tick "
        ":: () { whole:=16777217; total=0; real=to_real(whole); } }",
        BEHAVIOR_BYTECODE
    ));
    const script_catalog *view_catalog = authoring_language_catalog_view(catalog);
    authoring_source_presentation
        *view = authoring_source_presentation_new(prepared->analysis, view_catalog, NULL);
    REQUIRE(view);
    const source_presentation_origin
        *target = origin_at(view, SOURCE_PRESENTATION_EXPRESSION, "0; real=to_real"),
        *producer = origin_at(view, SOURCE_PRESENTATION_EXPRESSION, "16777217");
    REQUIRE(target && producer);
    authoring_edit_request request = { .kind = AUTHORING_EDIT_CONNECT,
                                       .generation = authoring_source_edit_generation(view),
                                       .origin = target->identity,
                                       .producer = producer->identity };
    authoring_edit_result edited = { 0 };
    char error[256];
    bool connected = authoring_source_edit(
        prepared->analysis,
        view_catalog,
        view,
        &request,
        &edited,
        error,
        sizeof error
    );
    authoring_source_presentation_free(view);
    if (!connected || !strstr(edited.text, "total=whole;")) {
        fprintf(stderr, "int wire: %s / %s\n", error, connected ? edited.text : "rejected");
    }
    REQUIRE(connected && strstr(edited.text, "total=whole;"));
    char *text = xstrdup(edited.text);
    authoring_edit_result_free(&edited);
    cleanup();
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 total = state_id("total"), real = state_id("real");
        REQUIRE(tick && total && real);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        const pin_value *wide = execution_instance_state(instance, total);
        REQUIRE(wide && wide->type == PIN_F64 && wide->d == 16777217.0);
        const pin_value *narrow = execution_instance_state(instance, real);
        REQUIRE(narrow && narrow->type == PIN_F32 && narrow->f == 16777216.0f);
        cleanup();
    }
    free(text);
    return true;
}
TEST_CASE(source_native_query_retains_nested_iteration_and_membership)
{
    const char *
        text = "Query :: behavior { visits:retain int=0; last:retain "
               "entity=0; Count :: ()->(){ for (outer in "
               "find_entities_with_component(Component:Health)) { for (inner "
               "in find_entities_with_component(Component:Health)) { "
               "visits+=1; last=inner; } } } on Tick :: () { visits=0; last=0; "
               "Count(); } }";
    for (int backend = 0; backend < 2; backend++) {
        test_world = ecs_world_new();
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Health, NULL) == 0);
        ecs_entity members[3];
        for (int i = 0; i < 3; i++) {
            members[i] = ecs_world_create_entity(test_world);
            REQUIRE(Health_add(test_world, members[i], 0) == 0);
        }
        REQUIRE(ecs_world_apply_commands(test_world) == 0);
        test_entities = entities_new(test_world, NULL);
        REQUIRE(test_entities);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 visits = state_id("visits"), last = state_id("last");
        REQUIRE(tick && visits && last);
        REQUIRE(execution_program_bind_service(program, "game.entities", test_entities) == 0);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        for (int count = 3; count >= 0; count--) {
            REQUIRE(dispatch(tick, 4096) >= 0);
            REQUIRE(execution_instance_pending(instance) == 0);
            int observed = execution_instance_state(instance, visits)->i;
            if (observed != count * count) {
                fprintf(
                    stderr,
                    "nested query backend=%d members=%d visits=%d expected=%d "
                    "last=%llu\n",
                    backend,
                    count,
                    observed,
                    count * count,
                    (unsigned long long)execution_instance_state(instance, last)->u64
                );
            }
            REQUIRE(observed == count * count);
            REQUIRE(
                execution_instance_state(instance, last)->u64 ==
                (count ? ecs_entity_to_u64(members[count - 1]) : 0)
            );
            if (count) {
                REQUIRE(
                    ecs_world_record_remove(
                        test_world,
                        members[count - 1],
                        COMPONENT_Health.id,
                        0
                    ) == 0
                );
                REQUIRE(ecs_world_apply_commands(test_world) == 0);
            }
        }
        cleanup();
    }
    return true;
}

TEST_CASE(source_native_relationship_snapshot_mutations_commit_between_handlers)
{
    const char *text =
        "Links :: behavior { count:retain int=0; phase:retain int=0; "
        "targets:retain entity[]=[]; on Tick :: () { if phase==0 { "
        "for target in self.ChildOf { "
        "targets=append(targets,target); } for target in targets { "
        "if has_pair(self,ChildOf,target) { remove_pair(self,ChildOf,target); "
        "count+=1; } } phase=1; } else { for target in targets { "
        "if not has_pair(self,ChildOf,target) { add_pair(self,ChildOf,target); "
        "count+=1; } } targets=[]; } } }";
    for (int backend = 0; backend < 2; backend++) {
        test_world = ecs_world_new();
        ecs_entity owner = ecs_world_create_entity(test_world);
        ecs_entity parent = ecs_world_create_entity(test_world);
        REQUIRE(!ecs_world_record_add_pair(test_world, owner, ECS_CHILD_OF, parent, 0));
        REQUIRE(!ecs_world_apply_commands(test_world));
        test_entities = entities_new(test_world, NULL);
        REQUIRE(test_entities);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        uint64_t count = state_id("count");
        REQUIRE(tick && count);
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, count)->i == 1);
        REQUIRE(ecs_has_pair(test_world, owner, ECS_CHILD_OF, parent));
        REQUIRE(!ecs_world_apply_commands(test_world));
        REQUIRE(!ecs_has_pair(test_world, owner, ECS_CHILD_OF, parent));
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, count)->i == 2);
        REQUIRE(!ecs_has_pair(test_world, owner, ECS_CHILD_OF, parent));
        REQUIRE(!ecs_world_apply_commands(test_world));
        REQUIRE(ecs_has_pair(test_world, owner, ECS_CHILD_OF, parent));
        cleanup();
    }
    return true;
}

/* Contextual EntityCondition literals query a retained entity snapshot against
 * current declared Boolean fields and fractional transforms. Rectangle contact
 * is strict, while a point includes the low edge and excludes the high edge. */
TEST_CASE(source_native_query_matches_authored_rectangles)
{
    const char *text =
        "Query :: behavior { members:retain entity[]; boxes:retain int=0; "
        "left:retain number=0; top:retain number=0; right:retain number=8; "
        "bottom:retain number=8; x:retain number=8; y:retain number=4; "
        "points:retain int=0; on Begin :: () { "
        "members=find_entities_with_component(SolidTile); } on Tick :: () { "
        "boxes=count_matching_entities(members,[{component:\"SolidTile\",field:"
        "\"enabled\",test:\"==\",value:1},{component:\"Transform\",field:\"x\","
        "test:\">\",value:left-4},{component:\"Transform\",field:\"x\",test:\"<"
        "\",value:right+4},{component:\"Transform\",field:\"y\",test:\">\","
        "value:top-4},{component:\"Transform\",field:\"y\",test:\"<\",value:"
        "bottom+4}]);points=count_matching_entities(members,[{component:"
        "\"SolidTile\",field:\"enabled\",test:\"==\",value:1},{component:"
        "\"Transform\",field:\"x\",test:\"<=\",value:x+4},{component:"
        "\"Transform\",field:\"x\",test:\">\",value:x-4},{component:"
        "\"Transform\",field:\"y\",test:\"<=\",value:y+4},{component:"
        "\"Transform\",field:\"y\",test:\">\",value:y-4}]); } }";
    static const struct
    {
        float x, y;
        int boxes, points;
    } poses[] = { { 12, 4, 0, 1 },     { 11.75f, 4, 1, 1 }, { -4, 4, 0, 0 },
                  { -3.75f, 4, 1, 0 }, { 4, -4, 0, 0 },     { 4, -3.75f, 1, 0 },
                  { 4, 12, 0, 0 },     { 4, 11.75f, 1, 0 }, { 8, 8, 1, 1 },
                  { 8, 0, 1, 0 } };
    for (int backend = 0; backend < 2; backend++) {
        char error[256];
        declared_field field = { .name = "enabled", .type = "bool" };
        declared_component component = { .name = "SolidTile",
                                         .fields = &field,
                                         .field_count = 1 };
        declared = declared_components_new(&component, 1, error, sizeof error);
        REQUIRE(declared);
        ecs_component_catalog components = declared_components_ecs(declared);
        const ecs_component_desc *solid = components.components[0];
        test_world = ecs_world_new();
        REQUIRE(ecs_world_register_component(test_world, solid, NULL) == 0);
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Transform, NULL) == 0);
        ecs_entity tile = ecs_world_create_entity(test_world);
        bool enabled = true;
        Transform transform = { .x = 4, .y = 4 };
        REQUIRE(ecs_world_record_add(test_world, tile, solid->id, &enabled, 0) == 0);
        REQUIRE(
            ecs_world_record_add(test_world, tile, COMPONENT_Transform.id, &transform, 0) == 0
        );
        REQUIRE(ecs_world_apply_commands(test_world) == 0);
        test_entities = entities_new(test_world, NULL);
        REQUIRE(test_entities);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t begin = execution_module_event(prepared->module, "Begin"),
                 tick = execution_module_event(prepared->module, "Tick"),
                 boxes = state_id("boxes"), points = state_id("points");
        REQUIRE(begin && tick && boxes && points);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(begin, 4096) >= 0);
        for (int i = 0; i < (int)(sizeof poses / sizeof *poses); i++) {
            Transform *at = Transform_get(test_world, tile);
            REQUIRE(at);
            at->x = poses[i].x;
            at->y = poses[i].y;
            REQUIRE(dispatch(tick, 4096) >= 0);
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(execution_instance_state(instance, boxes)->i == poses[i].boxes);
            REQUIRE(execution_instance_state(instance, points)->i == poses[i].points);
        }
        /* The snapshot survives field changes, component removal and stale
         * handles; only the currently enabled live rectangle can match. */
        Transform *at = Transform_get(test_world, tile);
        at->x = at->y = 8;
        *(bool *)ecs_component_data(test_world, tile, solid->id) = false;
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        *(bool *)ecs_component_data(test_world, tile, solid->id) = true;
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 1);
        REQUIRE(ecs_world_record_remove(test_world, tile, solid->id, 0) == 0);
        REQUIRE(ecs_world_apply_commands(test_world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        REQUIRE(ecs_world_record_add(test_world, tile, solid->id, &enabled, 0) == 0);
        REQUIRE(ecs_world_apply_commands(test_world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 1);
        REQUIRE(ecs_world_record_destroy(test_world, tile, 0) == 0);
        REQUIRE(ecs_world_apply_commands(test_world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        cleanup();
    }
    return true;
}

static ecs_entity place_collider(CF_V3 position, Collider3D collider)
{
    ecs_entity entity = ecs_world_create_entity(test_world);
    assert(Transform3D_add(test_world, entity, 0) == 0);
    assert(Collider3D_add(test_world, entity, 0) == 0);
    assert(ecs_world_apply_commands(test_world) == 0);
    *Transform3D_get(test_world, entity) = (Transform3D){ .position = position,
                                                          .rotation = cf_quat_identity(),
                                                          .scale = { 1, 1, 1 } };
    collider.mask = ~0u;
    *Collider3D_get(test_world, entity) = collider;
    return entity;
}
/* The 3D queries read colliders from scripts on both backends before any
 * physics step: a ray down meets a rock, the owner's crate slides into a wall
 * past the floor it rests on, and touches only that floor. Lengths are project
 * units, 32 per meter, so a cast stops Box3D's linear slop of .16 short. */
TEST_CASE(source_native_physics3d_queries_read_colliders)
{
    const char
        *text = "Probe :: behavior { start:retain vec3={x:0.0,y:320.0,z:0.0}; "
                "finish:retain vec3={x:0.0,y:-32.0,z:0.0}; push:retain "
                "vec3={x:320.0,y:0.0,z:0.0}; found:retain bool=false; "
                "target:retain entity=0; distance:retain number=0.0; "
                "normal:retain vec3={x:0.0,y:0.0,z:0.0}; blocked:retain "
                "entity=0; travel:retain number=0.0; touching:retain int=0; "
                "on Tick :: () { "
                "found=raycast_3d(start,finish,1,0,false).Found; "
                "target=raycast_3d(start,finish,1,0,false).Entity; "
                "distance=raycast_3d(start,finish,1,0,false).Distance; "
                "normal=raycast_3d(start,finish,1,0,false).Normal; "
                "blocked=shape_cast_3d(self,push,1,false).Entity; "
                "travel=shape_cast_3d(self,push,1,false).Distance; "
                "touching=length(contact_entities_3d(self,2)); } }";
    for (int backend = 0; backend < 2; backend++) {
        test_world = ecs_world_new();
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Transform3D, NULL) == 0);
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Collider3D, NULL) == 0);
        Collider3D slab = { .shape = COLLIDER3D_BOX,
                            .size = { 1280, 32, 1280 },
                            .category = 2 },
                   cube = { .shape = COLLIDER3D_BOX,
                            .size = { 32, 32, 32 },
                            .category = 1 },
                   ball = { .shape = COLLIDER3D_SPHERE, .radius = 16, .category = 1 };
        place_collider((CF_V3){ 0, -16, 0 }, slab);
        ecs_entity rock = place_collider((CF_V3){ 0, 64, 0 }, ball);
        ecs_entity crate = place_collider((CF_V3){ 0, 16, 128 }, cube);
        ecs_entity wall = place_collider((CF_V3){ 160, 16, 128 }, cube);
        test_entities = entities_new(test_world, NULL);
        REQUIRE(test_entities);
        test_physics3d = physics3d_new(test_world, test_entities);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        uint64_t found = state_id("found"), target = state_id("target"),
                 distance = state_id("distance"), normal = state_id("normal"),
                 blocked = state_id("blocked"), travel = state_id("travel"),
                 touching = state_id("touching");
        REQUIRE(tick && found && target && distance && normal && blocked && travel && touching);
        instance = execution_program_attach(program, ecs_entity_to_u64(crate));
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, found)->b);
        REQUIRE(execution_instance_state(instance, target)->u64 == ecs_entity_to_u64(rock));
        REQUIRE(fabsf(execution_instance_state(instance, distance)->f - 240) < .01f);
        const CF_V3 *up = execution_instance_state(instance, normal)->ptr;
        REQUIRE(fabsf(up->x) < 1e-3f && fabsf(up->y - 1) < 1e-3f && fabsf(up->z) < 1e-3f);
        REQUIRE(execution_instance_state(instance, blocked)->u64 == ecs_entity_to_u64(wall));
        REQUIRE(fabsf(execution_instance_state(instance, travel)->f - 127.84f) < .01f);
        REQUIRE(execution_instance_state(instance, touching)->i == 1);
        cleanup();
    }
    return true;
}

static bool publish_material(int width)
{
    shader_error error = { 0 };
    char message[256];
    size_t length = 0;
    shader_prepared_asset_init(&shader_record);
    shader_record.name = xstrdup("Typed host material");
    shader_record.graph.output_count = 4;
    for (int i = 0; i < 2; i++) {
        shader_record.names[i] = xstrdup(i ? "Second" : "Value");
        shader_record.widths[i] = width;
        int parameter = shader_graph_add(&shader_record.graph, SHADER_PARAMETER, width);
        shader_record.graph.nodes[parameter].parameter = i;
        int result = parameter;
        if (width > 1) {
            result = shader_graph_add(&shader_record.graph, SHADER_LENGTH, 1);
            shader_record.graph.nodes[result].input[0] = parameter;
        }
        shader_record.graph.output[i + 2] = result;
    }
    if (!shader_asset_prepared_write(&shader_record, &shader_bytes, &length, &error)) {
        fprintf(stderr, "source material artifact: %s\n", error.message);
        return false;
    }
    player_asset asset = { .name = "Typed host material",
                           .kind = PLAYER_ASSET_SHADER,
                           .data = shader_bytes,
                           .length = length };
    resource_host = render_prepared_resources(NULL);
    size_t remaining = 1024 * 1024;
    resource_generation = resource_host.stage(resource_host.context, &asset, 1, &remaining, message, sizeof message);
    if (!resource_generation) {
        fprintf(stderr, "source material stage: %s\n", message);
        return false;
    }
    resource_host.commit(resource_host.context, resource_generation, NULL);
    shader_prepared_asset_free(&shader_record);
    free(shader_bytes);
    shader_bytes = NULL;
    return render_shader_find("Typed host material") > 0;
}
TEST_CASE(source_native_material_retains_typed_parameter_writes_after_wait)
{
    const char *payloads[] = { "number=0.25", "f64=0.5", "bool=true", "vec2={x:0.25,y:-0.5}" };
    const float expected[][2] = { { .25f, 0 }, { .5f, 0 }, { 1, 0 }, { .25f, -.5f } };
    for (int backend = 0; backend < 2; backend++) {
        for (int kind = 0; kind < 4; kind++) {
            int width = kind == 3 ? 2 : 1;
            REQUIRE(publish_material(width));
            test_world = ecs_world_new();
            REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Shader, NULL) == 0);
            ecs_entity owner = ecs_world_create_entity(test_world);
            Shader binding = { .material = "Typed host material" };
            REQUIRE(
                ecs_world_record_add(test_world, owner, COMPONENT_Shader.id, &binding, 0) == 0
            );
            REQUIRE(ecs_world_apply_commands(test_world) == 0);
            char text[1024];
            snprintf(
                text,
                sizeof text,
                "Material :: behavior { "
                "done:retain bool=false; payload:retain %s; "
                "Apply :: (Value:%s,Target:entity)->(){ "
                "await delay(Duration:0.125); "
                "await "
                "set_material_parameter(Entity:Target,Name:\"Value\",Value:"
                "Value); "
                "await "
                "set_material_parameter(Entity:Target,Name:\"Second\",Value:"
                "Value); } "
                "on Tick :: () { await Apply(Value:payload,Target:self); "
                "done=true; "
                "} }",
                payloads[kind],
                kind == 0   ? "number"
                : kind == 1 ? "f64"
                : kind == 2 ? "bool"
                            : "vec2"
            );
            REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            uint64_t tick = execution_module_event(prepared->module, "Tick");
            REQUIRE(tick && done_id);
            instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance);
            retire_producer();
            REQUIRE(dispatch(tick, 1024) >= 0);
            REQUIRE(
                execution_instance_pending(instance) > 0 &&
                !execution_instance_state(instance, done_id)->b
            );
            float values[SHADER_PARAMETER_MAX][4];
            int material = render_shader_find("Typed host material");
            render_shader_parameters(material, test_world, owner, NULL, values);
            REQUIRE(values[0][0] == 0 && values[1][0] == 0);
            REQUIRE(pump(.125, .125, 1024) >= 0);
            REQUIRE(
                execution_instance_pending(instance) == 0 &&
                execution_instance_state(instance, done_id)->b
            );
            render_shader_parameters(material, test_world, owner, NULL, values);
            for (int slot = 0; slot < 2; slot++) {
                for (int field = 0; field < width; field++) {
                    REQUIRE(values[slot][field] == expected[kind][field]);
                }
            }
            cleanup();
        }
    }
    return true;
}

TEST_CASE(source_tilemap_queries_use_live_authored_cells)
{
    const char
        *text = "Grid :: behavior { frame:retain int=-1; count:retain int=0; "
                "point:retain vec2={x:11,y:-4}; minimum:retain "
                "vec2={x:10,y:-5}; maximum:retain "
                "vec2={x:13,y:0}; on Tick :: () { "
                "frame=tilemap_frame_at(self,point); "
                "count=tilemap_occupied_in_box(self,minimum,maximum); } }";
    for (int backend = 0; backend < 2; backend++) {
        test_world = ecs_world_new();
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Tilemap, NULL) == ECS_OK);
        REQUIRE(ecs_world_register_component(test_world, &COMPONENT_Transform, NULL) == ECS_OK);
        ecs_entity tilemap = ecs_world_create_entity(test_world);
        REQUIRE(Tilemap_add(test_world, tilemap, 0) == ECS_OK);
        REQUIRE(Transform_add(test_world, tilemap, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
        *Transform_get(test_world, tilemap) = (Transform){ 10, -5 };
        *Tilemap_get(test_world, tilemap) = (Tilemap){ .cells = "7 -1",
                                                       .columns = 2,
                                                       .rows = 1,
                                                       .frame_width = 3,
                                                       .frame_height = 5 };
        test_entities = entities_new(test_world, NULL);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 frame = state_id("frame"), count = state_id("count");
        REQUIRE(tick && frame && count);
        REQUIRE(execution_program_bind_service(program, "game.entities", test_entities) == 0);
        instance = execution_program_attach(program, ecs_entity_to_u64(tilemap));
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, frame)->i == 7);
        REQUIRE(execution_instance_state(instance, count)->i == 1);
        Tilemap_get(test_world, tilemap)->cells = "-1 9";
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, frame)->i == -1);
        REQUIRE(execution_instance_state(instance, count)->i == 0);
        REQUIRE(execution_instance_pending(instance) == 0);
        cleanup();
    }
    return true;
}

void test_source_input_save();

TEST_CASE(url_requests_validate_copy_replace_and_cancel)
{
    test_urls = url_requests_new();
    REQUIRE(!url_requests_open(test_urls, "https://example.org"));
    url_requests_enable(test_urls, true);
    const char *invalid[] = {
        NULL,
        "",
        "javascript:alert(1)",
        "file:///tmp/page.html",
        "//example.org",
        "https://",
        "https:///path",
        "http://?page",
        "https://:443/",
        "https://example.org:",
        "https://user@example.org/",
        "https://example.org/\n",
        "https://example.org/a b",
        "https://example.org\\path",
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(!url_requests_open(test_urls, invalid[i]));
        char *url;
        REQUIRE(url_requests_take(test_urls, &url) && !url);
    }
    const char *accepted[] = { "https://example.org/path?q=a%20b#part",
                               "http://localhost:8080/",
                               "https://[::1]/" };
    for (int i = 0; i < (int)(sizeof accepted / sizeof *accepted); i++) {
        char buffer[128];
        snprintf(buffer, sizeof buffer, "%s", accepted[i]);
        REQUIRE(url_requests_open(test_urls, buffer));
        memset(buffer, 0, sizeof buffer);
        char *url;
        REQUIRE(url_requests_take(test_urls, &url) && url);
        REQUIRE(!strcmp(url, accepted[i]));
        free(url);
        REQUIRE(!url_requests_take(test_urls, &url) && !url);
    }
    REQUIRE(url_requests_open(test_urls, "https://example.org/first"));
    REQUIRE(url_requests_open(test_urls, "https://example.org/second"));
    char *url;
    REQUIRE(url_requests_take(test_urls, &url) && url);
    /* Ownership transfers, so retiring the runtime cannot invalidate the host. */
    url_requests_free(test_urls);
    test_urls = url_requests_new();
    REQUIRE(!strcmp(url, "https://example.org/second"));
    free(url);
    url_requests_enable(test_urls, true);
    REQUIRE(url_requests_open(test_urls, "https://example.org/"));
    url_requests_cancel(test_urls);
    REQUIRE(url_requests_take(test_urls, &url) && !url);
    REQUIRE(url_requests_open(test_urls, "https://example.org/"));
    url_requests_enable(test_urls, false);
    REQUIRE(url_requests_take(test_urls, &url) && !url);
    REQUIRE(!url_requests_open(test_urls, "https://example.org/"));
    return true;
}

TEST_CASE(source_native_url_action_reports_acceptance_and_cancellation)
{
    const char *text = "Links :: behavior { done:retain bool=true; mode:retain "
                       "int=0; on Tick :: () { if mode==0 { result := await "
                       "open_url(Url:\"https://example.org/help\"); "
                       "done=result.Accepted; } if mode==1 { "
                       "open_url(\"http://localhost/help\"); } if mode==2 { "
                       "cancel_open_url(); } } }";
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 mode = state_id("mode");
        REQUIRE(tick && mode && done_id);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, done_id)->b);
        test_urls = url_requests_new();
        REQUIRE(execution_program_bind_service(program, URL_SERVICE, test_urls) == 0);
        url_requests_enable(test_urls, true);
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, done_id)->b);
        char *url;
        REQUIRE(url_requests_take(test_urls, &url) && url);
        REQUIRE(!strcmp(url, "https://example.org/help"));
        free(url);
        REQUIRE(
            execution_instance_set_state(instance, mode, (pin_value){ .type = PIN_I32, .i = 1 }) == 0
        );
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(
            execution_instance_set_state(instance, mode, (pin_value){ .type = PIN_I32, .i = 2 }) == 0
        );
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(url_requests_take(test_urls, &url) && !url);
        cleanup();
    }
    return true;
}

TEST_CASE(source_pointer_ray_reads_copied_values_and_inactive_default)
{
    const char
        *text = "Pointer :: behavior { active:retain bool=true; origin:retain "
                "vec3={}; direction:retain vec3={}; on Tick "
                ":: () "
                "{ ray:=get_pointer_ray(); active=ray.Active; "
                "origin=ray.Origin; direction=ray.Direction; } }";
    for (int backend = 0; backend < 2; backend++) {
        test_world = ecs_world_new();
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 active = state_id("active"), origin = state_id("origin"),
                 direction = state_id("direction");
        instance = execution_program_attach(program, 0);
        REQUIRE(instance && tick && active && origin && direction);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, active)->b);
        input_service = engine_input_new(test_world, NULL);
        REQUIRE(execution_program_bind_service(program, INPUT_SERVICE, input_service) == 0);
        input_pointer_ray ray = { .origin = { 2, 3, 4 },
                                  .direction = { 0, 0, -1 },
                                  .active = true };
        engine_input_pointer(input_service, ray);
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, active)->b);
        engine_input_clear(input_service);
        REQUIRE(
            !memcmp(execution_instance_state(instance, origin)->ptr, &ray.origin, sizeof(CF_V3))
        );
        REQUIRE(
            !memcmp(execution_instance_state(instance, direction)->ptr, &ray.direction, sizeof(CF_V3))
        );
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(!execution_instance_state(instance, active)->b);
        cleanup();
    }
    return true;
}

void test_source_native_features()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_call_preserves_delay_snapshot_and_target);
    RUN_TEST_CASE(source_native_context_samples_live_clock_after_nested_wait);
    RUN_TEST_CASE(source_native_particles_preserve_nested_wait_and_exact_emission);
    RUN_TEST_CASE(url_requests_validate_copy_replace_and_cancel);
    RUN_TEST_CASE(source_native_url_action_reports_acceptance_and_cancellation);
    pu_clear_setup();
    test_source_input_save();
}

TEST_CASE(source_numeric_ranges_clamp_storage_and_survive_program_replacement)
{
    const char *source =
        "Ranges :: behavior { input:retain number=0; observed:retain number=0; "
        "steps:retain number=0; converted:retain int=0; constructed:retain "
        "number=0; fresh:retain Bounded={}; defaulted:retain number=0; on Tick "
        ":: () "
        "{ self.Bounded.value=input; observed=self.Bounded.value; "
        "self.Bounded.value+=100; self.Bounded.value-=1; "
        "steps=self.Bounded.value; self.Bounded.count=int(observed); "
        "converted=self.Bounded.count; local:Bounded={value:input}; "
        "constructed=local.value; defaulted=fresh.positive; } on Begin :: () { "
        "copy:=self.Bounded; self.Bounded=copy; observed=self.Bounded.value; } "
        "}";
    static const struct
    {
        double input, expected;
        int integer;
    } cases[] = {
        { -5, 0, 0 },         { 2.75, 2.75, 2 },   { 100, 10, 10 },
        { INFINITY, 10, 10 }, { -INFINITY, 0, 0 }, { NAN, 0, 0 },
        { FLT_MAX, 10, 10 },
    };
    for (int backend = 0; backend < 2; backend++) {
        char error[256];
        declared_field fields[] = {
            { .name = "value", .type = "number", .min = 0, .max = 10 },
            { .name = "count", .type = "int", .min = 0, .max = 10 },
            { .name = "positive", .type = "number", .min = 2, .max = 5 },
        };
        declared_component component = { .name = "Bounded",
                                         .fields = fields,
                                         .field_count = 3 };
        declared = declared_components_new(&component, 1, error, sizeof error);
        REQUIRE(declared);
        const ecs_component_desc *desc = declared_components_ecs(declared).components[0];
        test_world = ecs_world_new();
        REQUIRE(ecs_world_register_component(test_world, desc, NULL) == ECS_OK);
        ecs_entity owner = ecs_world_create_entity(test_world);
        REQUIRE(ecs_world_record_add(test_world, owner, desc->id, NULL, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
        behavior_backend selected = backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        REQUIRE(prepare(source, selected));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        uint64_t input = state_id("input"), observed = state_id("observed"),
                 steps = state_id("steps"), converted = state_id("converted"),
                 constructed = state_id("constructed"),
                 defaulted = state_id("defaulted");
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(
            instance && tick && input && observed && steps && converted &&
            constructed && defaulted
        );
        for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    input,
                    (pin_value){ .type = PIN_F32, .f = (float)cases[i].input }
                ) == FLOW_OK
            );
            REQUIRE(dispatch(tick, 4096) >= 0);
            REQUIRE(
                execution_instance_state(instance, observed)->f ==
                (float)cases[i].expected
            );
            REQUIRE(
                execution_instance_state(instance, constructed)->f ==
                (float)cases[i].expected
            );
            REQUIRE(execution_instance_state(instance, converted)->i == cases[i].integer);
            REQUIRE(execution_instance_state(instance, defaulted)->f == 2);
            /* Each saturating assignment is observable: these two updates
             * cannot be reassociated into a single addition of 99. */
            REQUIRE(execution_instance_state(instance, steps)->f == 9);
        }
        REQUIRE(
            ecs_component_write_field(
                test_world,
                owner,
                desc->id,
                0,
                (pin_value){ .type = PIN_F32, .f = 1.75f }
            ) == ECS_OK
        );
        fields[0].max = 1;
        newer_declared = declared_components_new(&component, 1, error, sizeof error);
        REQUIRE(newer_declared);
        uint64_t copy = execution_module_event(prepared->module, "Begin");
        REQUIRE(copy && dispatch(copy, 4096) >= 0);
        /* Both whole-component snapshot and publication retain the world's
         * original range while a newer global schema is being prepared. */
        REQUIRE(execution_instance_state(instance, observed)->f == 1.75f);
        declared_components_free(newer_declared);
        newer_declared = NULL;
        script_lower_bindings bindings = authoring_language_bindings(catalog);
        authoring_source *replacement = authoring_source_prepare_document(
            "features.chibi",
            "test/native-features",
            "Ranges :: behavior { input:retain number=0; on Tick :: () { "
            "self.Bounded.value=input*input; } }",
            authoring_language_catalog_view(catalog),
            &bindings,
            selected
        );
        REQUIRE(replacement && replacement->valid);
        int replaced = execution_program_replace(program, replacement->module, false);
        authoring_source_free(replacement);
        REQUIRE(replaced == FLOW_OK);
        instance = execution_program_instance(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        /* The retained FLT_MAX input overflows only floating intermediates;
         * publication sees +infinity and stores the finite high endpoint. */
        REQUIRE(dispatch(tick, 4096) >= 0);
        pin_value actual;
        REQUIRE(ecs_component_read_field(NULL, test_world, owner, desc->id, 0, &actual) == ECS_OK);
        REQUIRE(actual.f == 10);
        cleanup();
    }
    return true;
}

void test_source_native_numeric_queries()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_motion_retains_numeric_outputs);
    RUN_TEST_CASE(source_native_number_random_range_checks_both_bounds);
    RUN_TEST_CASE(source_int_wire_into_f64_keeps_every_digit);
    RUN_TEST_CASE(source_native_query_retains_nested_iteration_and_membership);
    RUN_TEST_CASE(source_native_relationship_snapshot_mutations_commit_between_handlers);
    RUN_TEST_CASE(source_native_query_matches_authored_rectangles);
    RUN_TEST_CASE(source_native_physics3d_queries_read_colliders);
    RUN_TEST_CASE(source_tilemap_queries_use_live_authored_cells);
    RUN_TEST_CASE(source_pointer_ray_reads_copied_values_and_inactive_default);
    RUN_TEST_CASE(source_numeric_ranges_clamp_storage_and_survive_program_replacement);
    pu_clear_setup();
}

void test_source_native_material_features()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_material_retains_typed_parameter_writes_after_wait);
    pu_clear_setup();
}
