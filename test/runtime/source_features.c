#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/source_prepare.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "render/shader_catalog.h"
#include "render/shader_publish.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/math_native.h"
#include "runtime/particles.h"
#include "runtime/scenes/physics3d.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
static ecs_world *world;
static game_particles *particles, *expected_particles;
static game_entities *entities;
static game_physics3d *physics3d;
static declared_components *declared;
static shader_prepared_asset shader_record;
static unsigned char *shader_bytes;
static game_player_resource_host resource_host;
static void *resource_generation;
static uint64_t done_id, rotation_id, target_id, phase_id, pulse_id, period_id,
    duration_id;
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    instance = NULL;
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
    if (particles) {
        game_particles_free(particles);
    }
    particles = NULL;
    if (expected_particles) {
        game_particles_free(expected_particles);
    }
    expected_particles = NULL;
    if (physics3d) {
        game_physics3d_free(physics3d);
    }
    physics3d = NULL;
    if (entities) {
        game_entities_free(entities);
    }
    entities = NULL;
    if (world) {
        render_shader_clear_world(world);
    }
    ecs_world_free(world);
    world = NULL;
    declared_components_free(declared);
    declared = NULL;
    if (resource_generation) {
        resource_host.retire(resource_host.context, resource_generation);
    }
    resource_generation = NULL;
    shader_prepared_asset_free(&shader_record);
    free(shader_bytes);
    shader_bytes = NULL;
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
    native_capabilities *native = authoring_application_catalog_new(false, error, sizeof error);
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
    rotation_id = state_id("rotation");
    target_id = state_id("target");
    phase_id = state_id("phase");
    pulse_id = state_id("pulse");
    period_id = state_id("period");
    duration_id = state_id("duration");
    service_binding *services = NULL;
    if (particles) {
        apush(services, ((service_binding){ "game.particles", particles }));
    }
    if (entities) {
        apush(services, ((service_binding){ "game.entities", entities }));
    }
    if (physics3d) {
        apush(services, ((service_binding){ "game.physics3d", physics3d }));
    }
    program = execution_program_new(prepared->module, world, services);
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
TEST_CASE(source_native_reaction_preserves_delay_snapshot_and_target)
{
    const char *format =
        "behavior Reaction { owner \"ecs.entity\"; state done:bool=false; "
        "state rotation:struct<\"CF_V3\">={x:-1.0,y:2.0,z:3.0}; state "
        "target:handle<\"ecs.entity\">=0; "
        "function "
        "Apply(Rotation:struct<\"CF_V3\">,Target:handle<\"ecs.entity\">)->(){ "
        "await delay(Duration:0.125); await "
        "trigger_visual_reaction(Rotation:Rotation,Lift:4.0%s); } "
        "on Tick { await Apply(Rotation:rotation,Target:target); done=true; } "
        "}";
    for (int backend = 0; backend < 2; backend++) {
        for (int explicit_target = 0; explicit_target < 2; explicit_target++) {
            char text[1024];
            snprintf(text, sizeof text, format, explicit_target ? ",Target:Target" : "");
            world = ecs_world_new();
            REQUIRE(
                ecs_world_register_component(world, &GAME_COMPONENT_GameVisualMotion, NULL) == 0
            );
            ecs_entity owner = ecs_world_create_entity(world),
                       other = ecs_world_create_entity(world);
            REQUIRE(GAME_GameVisualMotion_add(world, owner, 0) == 0);
            REQUIRE(GAME_GameVisualMotion_add(world, other, 0) == 0);
            REQUIRE(ecs_world_apply_commands(world) == 0);
            GameVisualMotion initial = { .speed = 2,
                                         .reaction_age = 7,
                                         .reaction_duration = .25f };
            *GAME_GameVisualMotion_get(world, owner) = initial;
            *GAME_GameVisualMotion_get(world, other) = initial;
            REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
            uint64_t tick = execution_module_event(prepared->module, "Tick");
            REQUIRE(tick && done_id && rotation_id && target_id);
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
            CF_V3 changed = { 99, 99, 99 };
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    rotation_id,
                    (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &changed }
                ) == 0
            );
            REQUIRE(pump(.125, .125, 1024) >= 0);
            REQUIRE(
                execution_instance_pending(instance) == 0 &&
                execution_instance_state(instance, done_id)->b
            );
            const GameVisualMotion *actual = GAME_GameVisualMotion_get_const(
                world,
                explicit_target ? other : owner
            );
            REQUIRE(
                actual->reaction_rotation.x == -1 &&
                actual->reaction_rotation.y == 2 && actual->reaction_rotation.z == 3
            );
            REQUIRE(
                actual->reaction_lift == 4 && actual->reaction_age == 0 &&
                actual->speed == 2
            );
            REQUIRE(
                GAME_GameVisualMotion_get_const(world, explicit_target ? owner : other)
                    ->reaction_age == 7
            );
            cleanup();
        }
    }
    return true;
}
TEST_CASE(source_native_rhythm_samples_live_clock_after_nested_wait)
{
    const char
        *text = "behavior Rhythm { state phase:double=0; state pulse:double=0; "
                "state period:double=0; state duration:double=0; "
                "function "
                "Read()->(Phase:double,Pulse:double,Period:double,Duration:"
                "double){ r:=scene_rhythm(); "
                "return(Phase:r.Phase,Pulse:r.Pulse,Period:r.Period,Duration:r."
                "Duration); } "
                "function "
                "Apply()->(Phase:double,Pulse:double,Period:double,Duration:"
                "double){ await delay(Duration:0.125); r:=Read(); "
                "return(Phase:r.Phase,Pulse:r.Pulse,Period:r.Period,Duration:r."
                "Duration); } "
                "on Tick { r:=await Apply(); phase=r.Phase; pulse=r.Pulse; "
                "period=r.Period; duration=r.Duration; } }";
    for (int backend = 0; backend < 2; backend++) {
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameRhythm, NULL) == 0);
        ecs_entity clock = ecs_world_create_entity(world);
        GameRhythm settings = { 60, .25f };
        REQUIRE(
            ecs_world_record_add(world, clock, GAME_COMPONENT_GameRhythm.id, &settings, 0) == 0
        );
        REQUIRE(ecs_world_apply_commands(world) == 0);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        REQUIRE(tick && phase_id && pulse_id && period_id && duration_id);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_pending(instance) > 0);
        /* Pump only resumes work; update advances the instance rhythm clock. */
        REQUIRE(execution_program_update(program, .125, .125, 1024) >= 0);
        REQUIRE(execution_instance_pending(instance) == 0);
        REQUIRE(execution_instance_state(instance, phase_id)->d == .125);
        REQUIRE(execution_instance_state(instance, pulse_id)->d == .25);
        REQUIRE(execution_instance_state(instance, period_id)->d == 1);
        REQUIRE(execution_instance_state(instance, duration_id)->d == .25);
        cleanup();
    }
    return true;
}
TEST_CASE(source_native_particles_preserve_nested_wait_and_exact_emission)
{
    const char *text = "behavior Particles { state done:bool=false; function "
                       "Emit()->(){ "
                       "await "
                       "emit_particles(Position:{x:1.0,y:-2.0},Color:"
                       "4040002047,Count:3,Speed:80.0,Lifetime:0.3); "
                       "await delay(Duration:0.125); "
                       "await "
                       "emit_particles(Position:{x:1.0,y:-2.0},Color:"
                       "4040002047,Count:3,Speed:80.0,Lifetime:0.3); } "
                       "on Tick { Emit(); done=true; } }";
    for (int backend = 0; backend < 2; backend++) {
        particles = game_particles_new();
        expected_particles = game_particles_new();
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick");
        REQUIRE(tick && done_id);
        REQUIRE(execution_program_bind_service(program, "game.particles", particles) == 0);
        instance = execution_program_attach(program, 0);
        REQUIRE(instance);
        retire_producer();
        game_particle_burst burst = { .position = { 1, -2 },
                                      .color = 4040002047u,
                                      .count = 3,
                                      .speed = 80,
                                      .lifetime = .3f };
        REQUIRE(game_particles_emit(expected_particles, &burst) == 3);
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(
            game_particles_count(particles) == 3 &&
            execution_instance_pending(instance) > 0
        );
        REQUIRE(!memcmp(
            game_particles_data(particles),
            game_particles_data(expected_particles),
            3 * sizeof(game_particle)
        ));
        REQUIRE(game_particles_emit(expected_particles, &burst) == 3);
        REQUIRE(pump(.125, .125, 1024) >= 0);
        REQUIRE(
            game_particles_count(particles) == 6 &&
            execution_instance_pending(instance) == 0
        );
        REQUIRE(execution_instance_state(instance, done_id)->b);
        REQUIRE(!memcmp(
            game_particles_data(particles),
            game_particles_data(expected_particles),
            6 * sizeof(game_particle)
        ));
        cleanup();
    }
    return true;
}
TEST_CASE(source_native_motion_retains_numeric_outputs)
{
    const char *
        text = "behavior Motion { state input:double=0; state angle:double=0; "
               "state smooth:double=0; state root:double=0; state "
               "low:double=0; state high:double=0; state nearest:double=0; "
               "state position:float=0; state velocity:float=0; "
               "function "
               "Calculate(Value:double)->(Angle:double,Smooth:double,Root:"
               "double,Low:double,High:double,Nearest:double){ "
               "return(Angle:quantize_angle(Angle:Value,Step:1."
               "5707963267948966),Smooth:smooth_step(Value:Value),Root:sqrt("
               "Value:Value),Low:floor(Value:Value),High:ceil(Value:Value),"
               "Nearest:round(Value:Value)); } "
               "on Tick { r:=Calculate(Value:input); angle=r.Angle; "
               "smooth=r.Smooth; root=r.Root; low=r.Low; high=r.High; "
               "nearest=r.Nearest; "
               "spring:=advance_damped_spring(Position:0.0,Velocity:0.0,Target:"
               "1.0,Frequency:1.0,Damping:1.0,Delta:0.125); "
               "position=spring.Position; velocity=spring.Velocity; } }";
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
TEST_CASE(source_native_query_retains_nested_iteration_and_membership)
{
    const char *
        text = "behavior Query { state visits:int=0; state "
               "last:handle<\"ecs.entity\">=0; "
               "function Count()->(){ foreach (outer in "
               "find_entities_with_component(Component:\"GAME.GameHealth\")) { "
               "foreach (inner in "
               "find_entities_with_component(Component:\"GAME.GameHealth\")) { "
               "visits+=1; last=inner; } } } "
               "on Tick { visits=0; last=0; Count(); } }";
    for (int backend = 0; backend < 2; backend++) {
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) == 0);
        ecs_entity members[3];
        for (int i = 0; i < 3; i++) {
            members[i] = ecs_world_create_entity(world);
            REQUIRE(GAME_GameHealth_add(world, members[i], 0) == 0);
        }
        REQUIRE(ecs_world_apply_commands(world) == 0);
        entities = game_entities_new(world, NULL);
        REQUIRE(entities);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 visits = state_id("visits"), last = state_id("last");
        REQUIRE(tick && visits && last);
        REQUIRE(execution_program_bind_service(program, "game.entities", entities) == 0);
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
                        world,
                        members[count - 1],
                        GAME_COMPONENT_GameHealth.id,
                        0
                    ) == 0
                );
                REQUIRE(ecs_world_apply_commands(world) == 0);
            }
        }
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
        "behavior Query { state members:entity[]; state boxes:int=0; "
        "state left:number=0; state top:number=0; state right:number=8; "
        "state bottom:number=8; state x:number=8; state y:number=4; "
        "state points:int=0; on Begin { "
        "members=find_entities_with_component(\"SolidTile\"); } "
        "on Tick { boxes=count_matching_entities(members,["
        "{component:\"SolidTile\",field:\"enabled\",test:\"==\",value:1},"
        "{component:\"GAME.GameTransform\",field:\"x\",test:\">\",value:left-4}"
        ","
        "{component:\"GAME.GameTransform\",field:\"x\",test:\"<\",value:right+"
        "4},"
        "{component:\"GAME.GameTransform\",field:\"y\",test:\">\",value:top-4},"
        "{component:\"GAME.GameTransform\",field:\"y\",test:\"<\",value:bottom+"
        "4}]);"
        "points=count_matching_entities(members,["
        "{component:\"SolidTile\",field:\"enabled\",test:\"==\",value:1},"
        "{component:\"GAME.GameTransform\",field:\"x\",test:\"<=\",value:x+4},"
        "{component:\"GAME.GameTransform\",field:\"x\",test:\">\",value:x-4},"
        "{component:\"GAME.GameTransform\",field:\"y\",test:\"<=\",value:y+4},"
        "{component:\"GAME.GameTransform\",field:\"y\",test:\">\",value:y-4}]);"
        " "
        "} }";
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
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, solid, NULL) == 0);
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == 0);
        ecs_entity tile = ecs_world_create_entity(world);
        bool enabled = true;
        GameTransform transform = { .x = 4, .y = 4 };
        REQUIRE(ecs_world_record_add(world, tile, solid->id, &enabled, 0) == 0);
        REQUIRE(
            ecs_world_record_add(world, tile, GAME_COMPONENT_GameTransform.id, &transform, 0) == 0
        );
        REQUIRE(ecs_world_apply_commands(world) == 0);
        entities = game_entities_new(world, NULL);
        REQUIRE(entities);
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
            GameTransform *at = GAME_GameTransform_get(world, tile);
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
        GameTransform *at = GAME_GameTransform_get(world, tile);
        at->x = at->y = 8;
        *(bool *)ecs_component_data(world, tile, solid->id) = false;
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        *(bool *)ecs_component_data(world, tile, solid->id) = true;
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 1);
        REQUIRE(ecs_world_record_remove(world, tile, solid->id, 0) == 0);
        REQUIRE(ecs_world_apply_commands(world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        REQUIRE(ecs_world_record_add(world, tile, solid->id, &enabled, 0) == 0);
        REQUIRE(ecs_world_apply_commands(world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 1);
        REQUIRE(ecs_world_record_destroy(world, tile, 0) == 0);
        REQUIRE(ecs_world_apply_commands(world) == 0);
        REQUIRE(dispatch(tick, 4096) >= 0);
        REQUIRE(execution_instance_state(instance, boxes)->i == 0);
        cleanup();
    }
    return true;
}

static ecs_entity place_collider(CF_V3 position, GameCollider3D collider)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform3D_add(world, entity, 0) == 0);
    assert(GAME_GameCollider3D_add(world, entity, 0) == 0);
    assert(ecs_world_apply_commands(world) == 0);
    *GAME_GameTransform3D_get(world, entity) = (GameTransform3D){
        .position = position,
        .rotation = cf_quat_identity(),
        .scale = { 1, 1, 1 }
    };
    collider.mask = ~0u;
    *GAME_GameCollider3D_get(world, entity) = collider;
    return entity;
}
/* The 3D queries read colliders from scripts on both backends before any
 * physics step: a ray down meets a rock, the owner's crate slides into a wall
 * past the floor it rests on, and touches only that floor. Lengths are project
 * units, 32 per meter, so a cast stops Box3D's linear slop of .16 short. */
TEST_CASE(source_native_physics3d_queries_read_colliders)
{
    const char *
        text = "behavior Probe { owner \"ecs.entity\"; "
               "state start:struct<\"CF_V3\">={x:0.0,y:320.0,z:0.0}; "
               "state finish:struct<\"CF_V3\">={x:0.0,y:-32.0,z:0.0}; "
               "state push:struct<\"CF_V3\">={x:320.0,y:0.0,z:0.0}; "
               "state found:bool=false; state target:handle<\"ecs.entity\">=0; "
               "state distance:float=0.0; state "
               "normal:struct<\"CF_V3\">={x:0.0,y:0.0,z:0.0}; "
               "state blocked:handle<\"ecs.entity\">=0; state "
               "travel:float=0.0; "
               "state touching:int=0; "
               "on Tick { found=raycast_3d(start,finish,1,0,false).Found; "
               "target=raycast_3d(start,finish,1,0,false).Entity; "
               "distance=raycast_3d(start,finish,1,0,false).Distance; "
               "normal=raycast_3d(start,finish,1,0,false).Normal; "
               "blocked=shape_cast_3d(self,push,1,false).Entity; "
               "travel=shape_cast_3d(self,push,1,false).Distance; "
               "touching=length(contact_entities_3d(self,2)); } }";
    for (int backend = 0; backend < 2; backend++) {
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform3D, NULL) == 0);
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameCollider3D, NULL) == 0);
        GameCollider3D slab = { .shape = GAME_COLLIDER3D_BOX,
                                .size = { 1280, 32, 1280 },
                                .category = 2 },
                       cube = { .shape = GAME_COLLIDER3D_BOX,
                                .size = { 32, 32, 32 },
                                .category = 1 },
                       ball = { .shape = GAME_COLLIDER3D_SPHERE,
                                .radius = 16,
                                .category = 1 };
        place_collider((CF_V3){ 0, -16, 0 }, slab);
        ecs_entity rock = place_collider((CF_V3){ 0, 64, 0 }, ball);
        ecs_entity crate = place_collider((CF_V3){ 0, 16, 128 }, cube);
        ecs_entity wall = place_collider((CF_V3){ 160, 16, 128 }, cube);
        entities = game_entities_new(world, NULL);
        REQUIRE(entities);
        physics3d = game_physics3d_new(world, entities);
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
    game_player_asset asset = { .name = "Typed host material",
                                .kind = GAME_PLAYER_ASSET_SHADER,
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
    const char *payloads[] = { "float=0.25",
                               "double=0.5",
                               "bool=true",
                               "struct<\"CF_V2\">={x:0.25,y:-0.5}" };
    const float expected[][2] = { { .25f, 0 }, { .5f, 0 }, { 1, 0 }, { .25f, -.5f } };
    for (int backend = 0; backend < 2; backend++) {
        for (int kind = 0; kind < 4; kind++) {
            int width = kind == 3 ? 2 : 1;
            REQUIRE(publish_material(width));
            world = ecs_world_new();
            REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameShader, NULL) == 0);
            ecs_entity owner = ecs_world_create_entity(world);
            GameShader binding = { .material = "Typed host material" };
            REQUIRE(
                ecs_world_record_add(world, owner, GAME_COMPONENT_GameShader.id, &binding, 0) == 0
            );
            REQUIRE(ecs_world_apply_commands(world) == 0);
            char text[1024];
            snprintf(
                text,
                sizeof text,
                "behavior Material { owner \"ecs.entity\"; state "
                "done:bool=false; state payload:%s; "
                "function Apply(Value:%s,Target:handle<\"ecs.entity\">)->(){ "
                "await delay(Duration:0.125); "
                "await "
                "set_material_parameter(Entity:Target,Name:\"Value\",Value:"
                "Value); "
                "await "
                "set_material_parameter(Entity:Target,Name:\"Second\",Value:"
                "Value); } "
                "on Tick { await Apply(Value:payload,Target:self); done=true; "
                "} }",
                payloads[kind],
                kind == 0   ? "float"
                : kind == 1 ? "double"
                : kind == 2 ? "bool"
                            : "struct<\"CF_V2\">"
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
            render_shader_parameters(material, world, owner, NULL, values);
            REQUIRE(values[0][0] == 0 && values[1][0] == 0);
            REQUIRE(pump(.125, .125, 1024) >= 0);
            REQUIRE(
                execution_instance_pending(instance) == 0 &&
                execution_instance_state(instance, done_id)->b
            );
            render_shader_parameters(material, world, owner, NULL, values);
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
        *text = "behavior Grid { state frame:int=-1; state count:int=0; "
                "state point:struct<\"CF_V2\">={x:11,y:-4}; "
                "state minimum:struct<\"CF_V2\">={x:10,y:-5}; "
                "state maximum:struct<\"CF_V2\">={x:13,y:0}; "
                "on Tick { frame=tilemap_frame_at(self,point); "
                "count=tilemap_occupied_in_box(self,minimum,maximum); } }";
    for (int backend = 0; backend < 2; backend++) {
        world = ecs_world_new();
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameTilemap, NULL) == ECS_OK);
        REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
        ecs_entity tilemap = ecs_world_create_entity(world);
        REQUIRE(GAME_GameTilemap_add(world, tilemap, 0) == ECS_OK);
        REQUIRE(GAME_GameTransform_add(world, tilemap, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        *GAME_GameTransform_get(world, tilemap) = (GameTransform){ 10, -5 };
        *GAME_GameTilemap_get(world, tilemap) = (GameTilemap){ .cells = "7 -1",
                                                               .columns = 2,
                                                               .rows = 1,
                                                               .frame_width = 3,
                                                               .frame_height = 5 };
        entities = game_entities_new(world, NULL);
        REQUIRE(prepare(text, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE));
        uint64_t tick = execution_module_event(prepared->module, "Tick"),
                 frame = state_id("frame"), count = state_id("count");
        REQUIRE(tick && frame && count);
        REQUIRE(execution_program_bind_service(program, "game.entities", entities) == 0);
        instance = execution_program_attach(program, ecs_entity_to_u64(tilemap));
        REQUIRE(instance);
        retire_producer();
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, frame)->i == 7);
        REQUIRE(execution_instance_state(instance, count)->i == 1);
        GAME_GameTilemap_get(world, tilemap)->cells = "-1 9";
        REQUIRE(dispatch(tick, 1024) >= 0);
        REQUIRE(execution_instance_state(instance, frame)->i == -1);
        REQUIRE(execution_instance_state(instance, count)->i == 0);
        REQUIRE(execution_instance_pending(instance) == 0);
        cleanup();
    }
    return true;
}

void test_source_input_save();

void test_source_native_features()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_reaction_preserves_delay_snapshot_and_target);
    RUN_TEST_CASE(source_native_rhythm_samples_live_clock_after_nested_wait);
    RUN_TEST_CASE(source_native_particles_preserve_nested_wait_and_exact_emission);
    pu_clear_setup();
    test_source_input_save();
}

void test_source_native_numeric_queries()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_motion_retains_numeric_outputs);
    RUN_TEST_CASE(source_native_query_retains_nested_iteration_and_membership);
    RUN_TEST_CASE(source_native_query_matches_authored_rectangles);
    RUN_TEST_CASE(source_native_physics3d_queries_read_colliders);
    RUN_TEST_CASE(source_tilemap_queries_use_live_authored_cells);
    pu_clear_setup();
}

void test_source_native_material_features()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_native_material_retains_typed_parameter_writes_after_wait);
    pu_clear_setup();
}
