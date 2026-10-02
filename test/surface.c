#include "assets/prototype.h"
#include "graph/document/serialize.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/arena3d.h"
#include "render/arena_influence.h"
#include "render/arena_parts.h"
#include "render/arena_post.h"
#include "render/arena_trail.h"
#include "render/arena_visual.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/particle_emitters.h"
#include "runtime/rhythm.h"
#include "runtime/visual_motion.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static node *add(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void scalar(graph *g, node *n, int output, int pin, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(graph_set_pin_default(g, n, output, pin, &v) == 0);
}

static void vector(graph *g, node *n, int output, int pin, CF_V3 value)
{
    pin_value v = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = &value };
    assert(graph_set_pin_default(g, n, output, pin, &v) == 0);
}

static node *parameter(graph *g, const char *name, int slot, int width)
{
    node *n = add(g, "Shader Parameter");
    assert(graph_set_node_reference(g, n, slot, name) == 0);
    assert(
        graph_set_pin_type(g, n, 1, 0, width == 3 ? PIN_STRUCT : PIN_F32, width == 3 ? "ShaderVec3" : NULL) == 0
    );
    return n;
}

static behavior_asset *material(const char *name, bool unlit)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(a->graph, false);
    pin_value value = { .type = PIN_BOOL, .b = unlit };
    assert(graph_set_pin_default(a->graph, out, 0, 6, &value) == 0);
    return a;
}

static node *surface(behavior_asset *a)
{
    return a->graph->nodes[0];
}

static void publish(behavior_asset *a)
{
    shader_error error = { 0 };
    if (!render_shader_upsert(a, &error)) {
        fprintf(
            stderr,
            "Surface %s node %llu: %s\n",
            a->name,
            (unsigned long long)error.node,
            error.message
        );
        assert(false);
    }
}

static ecs_entity influence_source(ecs_world *world, float x, unsigned color, float strength, float linger)
{
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { x, 0 };
    GameSurfaceInfluence source = { color, strength, 20, linger };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &position, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameSurfaceInfluence.id, &source, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static ecs_world *influence_world()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    assert(
        ecs_world_register_component(world, &GAME_COMPONENT_GameSurfaceInfluence, NULL) == ECS_OK
    );
    return world;
}

static float activity(render_influences *influences, float x)
{
    float color[3];
    render_influences_sample(influences, (float[3]){ x, 700, 0 }, color);
    assert(color[1] == 0 && color[2] == 0);
    return color[0];
}

static void parts_and_trails()
{
    arena_parts parts = { .count = 2,
                          .parts = {
                              { .primitive = GAME_PRIMITIVE_TRIANGLE_PRISM,
                                .dimensions = { 20, 4, 12 },
                                .rotation = { .2f, .3f, .4f } },
                              { .primitive = GAME_PRIMITIVE_TETRAHEDRON,
                                .dimensions = { 5, 2, 4 },
                                .position = { -6, 1, 8 },
                                .material = "Fin" },
                          } };
    char *json = render_arena_parts_write(&parts);
    assert(json);
    arena_parts copy;
    char error[128];
    assert(render_arena_parts_parse(json, &copy, error, sizeof error));
    assert(copy.count == 2 && !memcmp(&parts.parts, &copy.parts, sizeof parts.parts));
    free(json);
    assert(!render_arena_parts_parse(
        "(array)- { - { primitive 0; (array)size 1 0 1; }; }\n",
        &copy,
        error,
        sizeof error
    ));
    assert(strstr(error, "part 1"));
    assert(!render_arena_parts_parse(
        "(array)- { - { primitive 4294967296; (array)size 1 1 1; }; }\n",
        &copy,
        error,
        sizeof error
    ));
    assert(!render_arena_parts_parse("{broken", &copy, error, sizeof error));
    assert(render_arena_parts_parse("", &copy, error, sizeof error) && copy.count == 0);

    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTrail, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameGeometry3D, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { 0 };
    GameTrail trail = { .color = 0x40ffffffu, .width = 2, .lifetime = .4f, .emission = 2 };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &position, 0) == ECS_OK
    );
    assert(ecs_world_record_add(world, entity, GAME_COMPONENT_GameTrail.id, &trail, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    arena_trails *trails = render_arena_trails_new();
    render_arena_trails_collect(trails, world, NULL, 0);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 10, 10 };
    render_arena_trails_collect(trails, world, NULL, .1f);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 20, 0 };
    render_arena_trails_collect(trails, world, NULL, .2f);
    int count;
    const arena_trail_segment *segments = render_arena_trails_segments(trails, &count);
    assert(count > 2 && count <= 128);
    assert(cf_len_v3(segments[0].from) < .0001f);
    assert(cf_len_v3(cf_sub_v3(segments[count - 1].to, (CF_V3){ 20, 0, 0 })) < .0001f);
    assert(segments[0].width < segments[count - 1].width);
    bool rounded = false;
    for (int i = 1; i < count; i++) {
        assert(cf_len_v3(cf_sub_v3(segments[i - 1].to, segments[i].from)) < .0001f);
        if (cf_len_v3(cf_sub_v3(segments[i].from, (CF_V3){ 10, 0, 10 })) < .0001f) {
            CF_V3 incoming = cf_norm_v3(
                cf_sub_v3(segments[i - 1].to, segments[i - 1].from)
            );
            CF_V3 outgoing = cf_norm_v3(cf_sub_v3(segments[i].to, segments[i].from));
            rounded = cf_dot_v3(incoming, outgoing) > .65f;
        }
    }
    assert(rounded);
    /* A render between history samples still ends exactly at the live actor. */
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 21, 1 };
    render_arena_trails_collect(trails, world, NULL, .205);
    segments = render_arena_trails_segments(trails, &count);
    assert(cf_len_v3(cf_sub_v3(segments[count - 1].to, (CF_V3){ 21, 0, 1 })) < .0001f);
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    render_arena_trails_collect(trails, world, NULL, .25);
    segments = render_arena_trails_segments(trails, &count);
    arena_trail_segment paused[128];
    int paused_count = count;
    assert(count <= 128);
    memcpy(paused, segments, (size_t)count * sizeof *segments);
    render_arena_trails_collect(trails, world, NULL, .25);
    segments = render_arena_trails_segments(trails, &count);
    assert(count == paused_count && !memcmp(paused, segments, (size_t)count * sizeof *segments));
    render_arena_trails_collect(trails, world, NULL, .61);
    render_arena_trails_segments(trails, &count);
    assert(count == 0);
    render_arena_trails_reset(trails);
    render_arena_trails_free(trails);
    ecs_world_free(world);
}

static void emitter_motion()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity e = ecs_world_create_entity(world);
    GameTransform position = { 10, 20 };
    GameGeometry3D appearance = { .primitive = GAME_PRIMITIVE_BOX,
                                  .width = 2,
                                  .height = 2,
                                  .depth = 2,
                                  .elevation = 7 };
    GameVisualTransform visual = { .roll = 1.5707963268f, .scale = 2 };
    GameOrientation heading = { 1.5707963268f };
    GameVelocity velocity = { 0, -40 };
    GameParticleEmitter emitter = { .active = true,
                                    .shape = GAME_PARTICLE_TRIANGLE,
                                    .color = 0xffffffffu,
                                    .end_color = 0xffffff00u,
                                    .rate = 20,
                                    .lifetime = { .5f, .5f },
                                    .speed = { 30, 30 },
                                    .size = { { 2, 2 }, { 0, 0 }, 1 },
                                    .velocity_inherit = 1,
                                    .offset = { -4, 1, 0 },
                                    .direction = { -1, 0, 0 } };
    assert(ecs_world_record_add(world, e, GAME_COMPONENT_GameTransform.id, &position, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, e, GAME_COMPONENT_GameGeometry3D.id, &appearance, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, e, GAME_COMPONENT_GameVisualTransform.id, &visual, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, e, GAME_COMPONENT_GameOrientation.id, &heading, 0) == ECS_OK
    );
    assert(ecs_world_record_add(world, e, GAME_COMPONENT_GameVelocity.id, &velocity, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, e, GAME_COMPONENT_GameParticleEmitter.id, &emitter, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_particle_emitters *emitters = game_particle_emitters_new(NULL);
    char error[128];
    assert(game_particle_emitters_collect(emitters, world, NULL, 0, error, sizeof error));
    int count;
    const game_particle *head = game_particle_emitters_heads(emitters, &count);
    assert(
        count == 1 && fabsf(head->position.x - 8) < .0001f &&
        fabsf(head->position.y - 12) < .0001f
    );
    assert(fabsf(head->elevation - 7) < .0001f && head->shape == GAME_PARTICLE_TRIANGLE);
    GAME_GameTransform_get(world, e)->y = 16;
    assert(game_particle_emitters_collect(emitters, world, NULL, .1f, error, sizeof error));
    const game_particles *tail = game_particle_emitters_particles(emitters);
    assert(game_particles_count(tail) == 2);
    const game_particle *particles = game_particles_data(tail);
    assert(fabsf(particles[0].velocity.y + 70) < .0001f);
    assert(particles[0].position.y < 8); // Backward travel still leaves exhaust behind the current nozzle.
    game_particle paused[2];
    memcpy(paused, particles, sizeof paused);
    game_particle paused_head = *game_particle_emitters_heads(emitters, &count);
    assert(game_particle_emitters_collect(emitters, world, NULL, .1f, error, sizeof error));
    assert(
        game_particles_count(tail) == 2 &&
        !memcmp(paused, game_particles_data(tail), sizeof paused)
    );
    assert(!memcmp(&paused_head, game_particle_emitters_heads(emitters, &count), sizeof paused_head));
    // Flight-heading attachment stays behind a tumbling source at its elevation.
    GameParticleEmitter *style = GAME_GameParticleEmitter_get(world, e);
    style->heading_only = true;
    style->offset = (CF_V3){ -6.5f, 0, 0 };
    *GAME_GameVisualTransform_get(world, e) = (GameVisualTransform){ .pitch = 1.2f,
                                                                     .yaw = .8f,
                                                                     .roll = .9f,
                                                                     .scale = 1 };
    assert(game_particle_emitters_collect(emitters, world, NULL, .1f, error, sizeof error));
    head = game_particle_emitters_heads(emitters, &count);
    assert(
        count == 1 && fabsf(head->position.x - 10) < .0001f &&
        fabsf(head->position.y - 9.5f) < .0001f
    );
    assert(fabsf(head->elevation - 7) < .0001f && fabsf(head->angle + 1.5707963268f) < .0001f);
    // A stationary quarter-turn must not emit along a chord through the hull.
    GAME_GameTransform_get(world, e)->x = GAME_GameTransform_get(world, e)->y = 0;
    GAME_GameOrientation_get(world, e)->radians = 0;
    *GAME_GameVisualTransform_get(world, e) = (GameVisualTransform){ .scale = 1 };
    *GAME_GameVelocity_get(world, e) = (GameVelocity){ 0 };
    style->heading_only = false;
    style->offset = (CF_V3){ -4, 0, 0 };
    style->velocity_inherit = 0;
    game_particle_emitters_reset(emitters);
    assert(game_particle_emitters_collect(emitters, world, NULL, 0, error, sizeof error));
    GAME_GameOrientation_get(world, e)->radians = 1.5707963268f;
    assert(game_particle_emitters_collect(emitters, world, NULL, .1f, error, sizeof error));
    particles = game_particles_data(tail);
    assert(game_particles_count(tail) == 2);
    for (int i = 0; i < 2; i++) {
        assert(fabsf(particles[i].position.x) < .0001f && particles[i].position.y <= -3.9999f);
    }
    assert(ecs_world_destroy_entity(world, e) == ECS_OK);
    assert(game_particle_emitters_collect(emitters, world, NULL, .2f, error, sizeof error));
    game_particle_emitters_heads(emitters, &count);
    assert(count == 0 && game_particles_count(tail) == 2);
    assert(game_particle_emitters_collect(emitters, world, NULL, .7f, error, sizeof error));
    assert(game_particles_count(tail) == 0);
    assert(game_particle_emitters_collect(emitters, world, NULL, .05f, error, sizeof error));
    assert(game_particles_count(tail) == 0);
    game_particle_emitters_free(emitters);
    ecs_world_free(world);
}

static int emitter_trajectory(int hz, game_particle out[128])
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    engine *runtime = engine_new(world, live);
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { 0 };
    GameVelocity velocity = { 480, 0 };
    GameBody body = { .type = GAME_BODY_KINEMATIC };
    GameCollider collider = { .radius = 1, .category = 1, .sensor = true };
    GameParticleEmitter style = { .active = true,
                                  .shape = GAME_PARTICLE_TRIANGLE,
                                  .color = 0xffffffffu,
                                  .rate = 85,
                                  .lifetime = { 2, 2 },
                                  .speed = { 8, 8 },
                                  .size = { { 1.8f, 1.8f }, { 0, 0 }, 1 },
                                  .spread = .5f,
                                  .drag = { { 2, 2 }, { 0, 0 }, 0 },
                                  .offset = { -6.5f, 0, 0 },
                                  .direction = { -1, 0, 0 },
                                  .heading_only = true };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &position, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameVelocity.id, &velocity, 0) == ECS_OK
    );
    assert(ecs_world_record_add(world, entity, GAME_COMPONENT_GameBody.id, &body, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameCollider.id, &collider, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameParticleEmitter.id, &style, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_particle_emitters *emitters = game_particle_emitters_new(NULL);
    const game_particles *tail = game_particle_emitters_particles(emitters);
    char error[128];
    uint64_t previous_step = 0;
    game_particle previous = { 0 };
    bool interpolated = false;
    for (int frame = 0; frame <= hz / 2; frame++) {
        assert(engine_frame(runtime, (double)frame / hz) == FLOW_OK);
        const engine_frame_timing *timing = engine_last_timing(runtime);
        assert(
            game_particle_emitters_collect(emitters, world, runtime, timing->simulation_time, error, sizeof error)
        );
        int count = game_particles_count(tail);
        if (count && previous.age > 0 && timing->simulation_steps == previous_step) {
            const game_particle *first = game_particles_data(tail);
            assert(
                first->age > previous.age &&
                first->position.x < previous.position.x
            );
            interpolated = true;
        }
        if (count) {
            previous = game_particles_data(tail)[0];
        }
        previous_step = timing->simulation_steps;
    }
    /* Continuous emission is observable without any renderer collecting it. */
    assert(game_particles_count(live->particles) == 41);
    assert(hz == 60 || interpolated);
    int count = game_particles_count(tail);
    assert(count == 41);
    memcpy(out, game_particles_data(tail), (size_t)count * sizeof *out);
    int heads;
    const game_particle *head = game_particle_emitters_heads(emitters, &heads);
    assert(engine_render_position(runtime, entity, &position));
    assert(heads == 1 && fabsf(head->position.x - position.x + 6.5f) < .0001f);

    assert(engine_set_playing(runtime, false) == FLOW_OK && engine_frame(runtime, 1) == FLOW_OK);
    assert(game_particle_emitters_collect(emitters, world, runtime, .5, error, sizeof error));
    game_particle frozen[128];
    int frozen_count = game_particles_count(tail);
    memcpy(frozen, game_particles_data(tail), (size_t)frozen_count * sizeof *frozen);
    assert(engine_frame(runtime, 2) == FLOW_OK);
    assert(game_particle_emitters_collect(emitters, world, runtime, .5, error, sizeof error));
    assert(
        game_particles_count(tail) == frozen_count &&
        !memcmp(frozen, game_particles_data(tail), (size_t)frozen_count * sizeof *frozen)
    );
    assert(engine_set_playing(runtime, true) == FLOW_OK && engine_frame(runtime, 2) == FLOW_OK);
    assert(game_particle_emitters_collect(emitters, world, runtime, .5, error, sizeof error));
    assert(
        game_particles_count(tail) == frozen_count &&
        !memcmp(frozen, game_particles_data(tail), (size_t)frozen_count * sizeof *frozen)
    );
    engine_reset(runtime);
    assert(engine_frame(runtime, 3) == FLOW_OK);
    assert(game_particle_emitters_collect(emitters, world, runtime, 0, error, sizeof error));
    assert(game_particles_count(tail) == 0);
    game_particle_emitters_free(emitters);
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
    return count;
}

static void emitter_contract()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity parent = ecs_world_create_entity(world),
               child = ecs_world_create_entity(world);
    GameTransform position = { 100, 20 }, local = { 0 };
    GameVelocity velocity = { 40, 0 };
    GameParticleEmitter style = game_particle_emitter_defaults();
    style.parent = ecs_entity_to_u64(parent);
    style.rate = 0;
    style.burst_count = 8;
    style.planar = true;
    style.offset = (CF_V3){ -9, 0, 0 };
    style.speed = (CF_V2){ 0 };
    style.lifetime = (CF_V2){ .5f, .7f };
    style.drag.start = (CF_V2){ 2, 4 };
    style.size.start = (CF_V2){ 2, 3 };
    style.metaball_group = 2;
    style.metaball_blend = 1.5f;
    style.velocity_align = true;
    style.stretch = (CF_V2){ .01f, .02f };
    assert(
        ecs_world_record_add(world, parent, GAME_COMPONENT_GameTransform.id, &position, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, parent, GAME_COMPONENT_GameVelocity.id, &velocity, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, child, GAME_COMPONENT_GameTransform.id, &local, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, child, GAME_COMPONENT_GameParticleEmitter.id, &style, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_particle_emitters *emitters = game_particle_emitters_new(NULL);
    char error[128];
    assert(game_particle_emitters_collect(emitters, world, NULL, 0, error, sizeof error));
    const game_particles *particles = game_particle_emitters_particles(emitters);
    assert(game_particles_count(particles) == 8);
    game_particle reference[8];
    memcpy(reference, game_particles_data(particles), sizeof reference);
    for (int i = 0; i < 8; i++) {
        const game_particle *p = &reference[i];
        assert(p->position.x == 91 && p->position.y == 20 && p->velocity.x == 40);
        assert(p->lifetime >= .5f && p->lifetime <= .7f && p->size >= 2 && p->size <= 3);
        assert(p->drag >= 2 && p->drag <= 4 && p->metaball_group == 2 && p->metaball_blend == 1.5f);
        assert(p->stretch >= .01f && p->stretch <= .02f && p->velocity_align && p->angle == 0);
    }
    assert(game_particle_emitters_collect(emitters, world, NULL, .1, error, sizeof error));
    assert(game_particles_count(particles) == 8);
    assert(game_particles_data(particles)[0].position.x > 91);
    game_particle_emitters_reset(emitters);
    assert(game_particle_emitters_collect(emitters, world, NULL, 0, error, sizeof error));
    assert(!memcmp(reference, game_particles_data(particles), sizeof reference));
    GAME_GameParticleEmitter_get(world, child)->active = false;
    assert(game_particle_emitters_collect(emitters, world, NULL, .1, error, sizeof error));
    assert(game_particles_count(particles) == 8);
    game_particle_emitters_free(emitters);
    ecs_world_free(world);
}

static void emitter_cadence()
{
    game_particle slow[128], fast[128];
    int count = emitter_trajectory(60, slow);
    assert(emitter_trajectory(120, fast) == count);
    for (int i = 0; i < count; i++) {
        assert(fabsf(slow[i].position.x - fast[i].position.x) < .0005f);
        assert(fabsf(slow[i].position.y - fast[i].position.y) < .0005f);
        assert(fabsf(slow[i].elevation - fast[i].elevation) < .0005f);
        assert(fabsf(slow[i].velocity.x - fast[i].velocity.x) < .0001f);
        assert(fabsf(slow[i].age - fast[i].age) < .000001f);
    }
}

static void visual_offset_node()
{
    behavior_asset *asset = behavior_asset_new("Visual offset transform");
    node *n = add(asset->graph, "Transform Visual Offset");
    CF_V2 offset = { 2, 3 };
    GameVisualTransform visual = { .pitch = 1.5707963268f,
                                   .yaw = .7853981634f,
                                   .roll = 1.5707963268f,
                                   .scale = 2 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &offset };
    assert(graph_set_pin_default(asset->graph, n, 0, 0, &value) == 0);
    scalar(asset->graph, n, 0, 1, 4);
    scalar(asset->graph, n, 0, 2, .7853981634f);
    value = (pin_value){ .type = PIN_STRUCT,
                         .type_name = "GameVisualTransform",
                         .ptr = &visual };
    assert(graph_set_pin_default(asset->graph, n, 0, 3, &value) == 0);
    eval_context context;
    eval_context_init(&context);
    arena storage;
    arena_init(&storage, 4096);
    assert(graph_eval_output(asset->graph, n, 0, &storage, &value, &context) == 0);
    const CF_V2 *actual = value.ptr;
    assert(fabsf(actual->x + 8) < .0001f && fabsf(actual->y - 6) < .0001f);
    assert(graph_eval_output(asset->graph, n, 1, &storage, &value, &context) == 0);
    assert(fabsf(value.f - 4) < .0001f);
    arena_free(&storage);
    eval_context_free(&context);
    behavior_asset_free(asset);
}

static void visual_culling()
{
    arena_visual visual = { .part = { .primitive = GAME_PRIMITIVE_BOX,
                                      .dimensions = { 1, 1, 1 } },
                            .frame = cf_m4_identity() };
    CF_M4x4 clip = cf_m4_identity();
    assert(render_arena_visual_visible(&visual, clip));
    for (int axis = 0; axis < 3; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            visual.frame = cf_m4_identity();
            visual.frame.elements[12 + axis] = sign * 1.99f;
            assert(render_arena_visual_visible(&visual, clip));
            visual.frame.elements[12 + axis] = sign * 2.01f;
            assert(!render_arena_visual_visible(&visual, clip));
        }
    }
    visual.frame = cf_m4_identity();
    visual.frame.elements[12] = 3;
    visual.part.dimensions.x = 4;
    assert(render_arena_visual_visible(&visual, clip));
    visual.frame.elements[12] = 6;
    assert(!render_arena_visual_visible(&visual, clip));
    /* A bound crossing the eye plane cannot use divided center coordinates. */
    visual.frame = cf_m4_identity();
    clip.elements[11] = 1;
    clip.elements[15] = 0;
    assert(render_arena_visual_visible(&visual, clip));
}

static void influence_capacity()
{
    ecs_world *world = influence_world();
    render_influences *influences = render_influences_new();
    ecs_entity sources[66];
    for (int i = 0; i < 66; i++) {
        sources[i] = influence_source(world, i * 50, 0xff0000ffu, 1, .2f);
    }
    render_influences_collect(influences, world, NULL, 0);
    for (int i = 0; i < 66; i++) {
        assert(activity(influences, i * 50) == (i < 64 ? 1 : 0));
    }
    GAME_GameSurfaceInfluence_get(world, sources[0])->strength = .1f;
    GAME_GameSurfaceInfluence_get(world, sources[64])->strength = 2;
    render_influences_collect(influences, world, NULL, 0);
    assert(activity(influences, 0) == 0);
    assert(activity(influences, 64 * 50) == 2);
    assert(activity(influences, 65 * 50) == 0);
    assert(ecs_world_destroy_entity(world, sources[64]) == ECS_OK);
    render_influences_collect(influences, world, NULL, .2f);
    assert(activity(influences, 64 * 50) == 0);
    assert(activity(influences, 65 * 50) == 1);
    render_influences_free(influences);
    ecs_world_free(world);
}

static void influence_history()
{
    ecs_world *world = influence_world();
    render_influences *influences = render_influences_new();
    ecs_entity entity = influence_source(world, 0, 0xff0000ffu, 2, .2f);
    render_influences_collect(influences, world, NULL, 0);
    assert(activity(influences, 0) == 2 && activity(influences, 20) == 0);
    GAME_GameTransform_get(world, entity)->x = 40;
    render_influences_collect(influences, world, NULL, .1f);
    assert(fabsf(activity(influences, 0) - .5f) < .0001f);
    assert(activity(influences, 40) == 2);
    render_influences_collect(influences, world, NULL, .1f);
    assert(fabsf(activity(influences, 0) - .5f) < .0001f);
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    render_influences_collect(influences, world, NULL, .2f);
    assert(activity(influences, 0) == 0);
    assert(fabsf(activity(influences, 40) - .5f) < .0001f);
    render_influences_collect(influences, world, NULL, .31f);
    assert(activity(influences, 40) == 0);

    entity = influence_source(world, 0, 0xff0000ffu, 2, .2f);
    render_influences_collect(influences, world, NULL, 1);
    GAME_GameTransform_get(world, entity)->x = 40;
    render_influences_collect(influences, world, NULL, 1.1f);
    assert(activity(influences, 0) > 0);
    render_influences_collect(influences, world, NULL, 1.05f);
    assert(activity(influences, 0) == 0 && activity(influences, 40) == 2);
    render_influences_reset(influences);
    assert(activity(influences, 40) == 0);
    GAME_GameSurfaceInfluence_get(world, entity)->linger = 0;
    render_influences_collect(influences, world, NULL, 1.05f);
    assert(activity(influences, 40) == 2);
    GAME_GameSurfaceInfluence_get(world, entity)->strength = 0;
    render_influences_collect(influences, world, NULL, 1.05f);
    assert(activity(influences, 40) == 0);
    GAME_GameSurfaceInfluence_get(world, entity)->strength = 2;
    render_influences_collect(influences, world, NULL, 1.05f);
    assert(activity(influences, 40) == 2);
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    render_influences_collect(influences, world, NULL, 1.05f);
    assert(activity(influences, 40) == 0);

    for (int i = 1; i <= 64; i++) {
        influence_source(world, i * 50, 0xff0000ffu, .1f, .2f);
    }
    influence_source(world, 0, 0xff0000ffu, 2, .2f);
    render_influences_collect(influences, world, NULL, 2);
    assert(activity(influences, 0) == 2);
    int retained = 1;
    for (int i = 1; i <= 64; i++) {
        retained += activity(influences, i * 50) > 0;
    }
    assert(retained == 64);
    ecs_world *replacement = influence_world();
    render_influences_collect(influences, replacement, NULL, 2);
    assert(activity(influences, 0) == 0);
    render_influences_free(influences);
    ecs_world_free(replacement);
    ecs_world_free(world);
}

static void influence_graph()
{
    behavior_asset *asset = material("Surface influence context", true);
    node *influence = add(asset->graph, "Shader Surface Influence");
    assert(graph_connect_data(asset->graph, influence, 0, surface(asset), 1) == 0);
    const char *label;
    assert(shader_node_input_context(influence, 0, &label) == SHADER_WORLD_POSITION);
    assert(!strcmp(label, "World Position"));
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(asset->graph, &ir, &error));
    float result[SHADER_OUTPUT_MAX][4];
    shader_sample_context sample = { .world_position = { 10, 700, 0 } };
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(result[1][0] == 0 && result[1][1] == 0 && result[1][2] == 0);
    ecs_world *world = influence_world();
    render_influences *influences = render_influences_new();
    influence_source(world, 0, 0xff0000ffu, 2, .2f);
    render_influences_collect(influences, world, NULL, 0);
    sample.resolve_influence = render_influences_sample;
    sample.influence_userdata = influences;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(fabsf(result[1][0] - 1.125f) < .0001f);
    /* An explicit panel centroid overrides per-pixel world position. */
    vector(asset->graph, influence, 0, 0, (CF_V3){ 0 });
    assert(shader_graph_from_graph(asset->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(result[1][0] == 2);
    render_influences_free(influences);
    ecs_world_free(world);
    shader_graph_free(&ir);
    behavior_asset_free(asset);
}

static void visual_motion_defaults()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[128];
    game_prototype *prototype = game_prototype_load(
        "name \"Motion defaults\"\n"
        "components {\n"
        "    GameVisualTransform {\n"
        "        pitch 0.3\n"
        "        yaw -0.2\n"
        "        roll 0.7\n"
        "        scale 0.8\n"
        "    }\n"
        "    GameGeometry3D {\n"
        "        primitive 0\n"
        "        width 11\n"
        "        height 11\n"
        "        depth 11\n"
        "        elevation 12\n"
        "    }\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(
        prototype &&
        game_prototype_add_component(prototype, &GAME_COMPONENT_GameVisualMotion) == 0
    );
    CF_V3 velocity = { 1, 0, 0 };
    assert(
        game_prototype_write_field(
            prototype,
            "GameVisualMotion",
            "angular_velocity",
            (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &velocity }
        ) == 0
    );
    char *json = game_prototype_save(prototype);
    game_prototype_free(prototype);
    prototype = game_prototype_load(json, &registry, error, sizeof error);
    free(json);
    assert(prototype);
    ecs_world *world = ecs_world_new();
    assert(acount(prototype->components) == 3);
    for (int i = 0; i < acount(prototype->components); i++) {
        assert(ecs_world_register_component(world, prototype->components[i].desc, NULL) == ECS_OK);
    }
    ecs_entity entity;
    assert(game_prototype_spawn_components(prototype, world, &entity, error, sizeof error) == ECS_OK);
    const GameVisualMotion *motion = GAME_GameVisualMotion_get_const(world, entity);
    assert(motion->speed == 1 && motion->reaction_duration == .32f && motion->reaction_age == .32f);
    GameVisualTransform before = *GAME_GameVisualTransform_get_const(world, entity);
    assert(game_visual_motion_tick(world, 0) == FLOW_OK);
    assert(!memcmp(&before, GAME_GameVisualTransform_get_const(world, entity), sizeof before));
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12);
    assert(game_visual_motion_tick(world, .25f) == FLOW_OK);
    assert(fabsf(GAME_GameVisualTransform_get_const(world, entity)->pitch - .55f) < .00001f);
    assert(GAME_GameVisualTransform_get_const(world, entity)->scale == .8f);
    assert(GAME_GameGeometry3D_get_const(world, entity)->elevation == 12);
    ecs_world_free(world);
    assert(
        game_prototype_write_field(
            prototype,
            "GameVisualMotion",
            "speed",
            (pin_value){ .type = PIN_F32, .f = 2 }
        ) == 0
    );
    assert(game_prototype_detach_component(prototype, "GameVisualMotion") == 0);
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameVisualMotion) == 0);
    pin_value value;
    assert(
        game_prototype_read_field(prototype, "GameVisualMotion", "speed", &value) == 0 &&
        value.f == 2
    );
    game_prototype_free(prototype);
}

static void rhythm_and_defaults()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameRhythm, NULL) == ECS_OK);
    game_beat beat;
    assert(game_rhythm_sample(world, .09, &beat));
    assert(fabs(beat.phase - .18) < .000001 && fabs(beat.pulse - .25) < .000001);
    assert(fabs(beat.period - .5) < .000001 && fabs(beat.duration - .18) < .000001);
    assert(game_rhythm_sample(world, .25, &beat) && beat.pulse == 0);
    assert(game_rhythm_sample(world, .5, &beat) && beat.phase == 0 && beat.pulse == 1);
    ecs_entity clock = ecs_world_create_entity(world);
    GameRhythm settings = { 60, .1f };
    assert(
        ecs_world_record_add(world, clock, GAME_COMPONENT_GameRhythm.id, &settings, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    behavior_asset *asset = behavior_asset_new("Rhythm reader");
    node *read = add(asset->graph, "Scene Rhythm");
    behavior_clock time = { .current_time = .05 };
    behavior_instance instance = { .asset = asset, .clock = &time };
    service_binding *services = NULL;
    apush(services, ((service_binding){ "ecs.world", world }));
    eval_context eval;
    eval_context_init(&eval);
    eval.instance = &instance;
    eval.services = services;
    arena storage;
    arena_init(&storage, 4096);
    pin_value value;
    assert(graph_eval_output(asset->graph, read, 1, &storage, &value, &eval) == 0);
    assert(value.type == PIN_F64 && fabs(value.d - .25) < .000001);
    time.current_time = .5;
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(asset->graph, read, 0, &storage, &value, &eval) == 0);
    assert(fabs(value.d - .5) < .000001);
    settings.bpm = 0;
    *GAME_GameRhythm_get(world, clock) = settings;
    assert(!game_rhythm_sample(world, .5, &beat));
    settings.bpm = 60;
    *GAME_GameRhythm_get(world, clock) = settings;
    ecs_entity second = ecs_world_create_entity(world);
    assert(
        ecs_world_record_add(world, second, GAME_COMPONENT_GameRhythm.id, &settings, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_rhythm_sample(world, .5, &beat));
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(asset->graph, read, 1, &storage, &value, &eval) != 0);
    assert(strstr(eval_context_status(&eval)->message, "one clock"));
    arena_free(&storage);
    eval_context_free(&eval);
    afree(services);
    behavior_asset_free(asset);
    ecs_world_free(world);

    game_prototype_registry registry = { .components = GAME_REGISTRY_COMPONENTS,
                                         .component_count = GAME_REGISTRY_COMPONENT_COUNT };
    char error[128];
    game_prototype *prototype = game_prototype_load(
        "name \"Defaults\"\n"
        "(object)components\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameVisualTransform) == 0);
    assert(
        game_prototype_read_field(prototype, "GameVisualTransform", "scale", &value) == 0 &&
        value.f == 1
    );
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameRhythm) == 0);
    assert(
        game_prototype_read_field(prototype, "GameRhythm", "bpm", &value) == 0 &&
        value.f == 120
    );
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameMeshParts) == 0);
    assert(
        game_prototype_read_field(prototype, "GameMeshParts", "parts", &value) == 0 &&
        !strcmp(value.str, "(array)-\n")
    );
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameParticleEmitter) == 0);
    assert(
        game_prototype_read_field(prototype, "GameParticleEmitter", "rate", &value) == 0 &&
        value.f > 0
    );
    assert(game_prototype_read_field(prototype, "GameParticleEmitter", "direction", &value) == 0);
    assert(((const CF_V3 *)value.ptr)->x == -1 && ((const CF_V3 *)value.ptr)->y == 0);
    assert(
        game_prototype_read_field(prototype, "GameParticleEmitter", "heading_only", &value) ==
            0 &&
        !value.b
    );
    assert(
        game_prototype_write_field(
            prototype,
            "GameVisualTransform",
            "scale",
            (pin_value){ .type = PIN_F32, .f = 2 }
        ) == 0
    );
    assert(game_prototype_detach_component(prototype, "GameVisualTransform") == 0);
    assert(game_prototype_add_component(prototype, &GAME_COMPONENT_GameVisualTransform) == 0);
    assert(
        game_prototype_read_field(prototype, "GameVisualTransform", "scale", &value) == 0 &&
        value.f == 2
    );
    game_prototype_free(prototype);
}

static void annular_influence()
{
    ecs_world *world = influence_world();
    ecs_entity entity = influence_source(world, 0, 0xff0000ffu, 2, 0);
    GAME_GameSurfaceInfluence_get(world, entity)->inner_radius = 10;
    render_influences *influences = render_influences_new();
    render_influences_collect(influences, world, NULL, 0);
    assert(activity(influences, 0) == 0 && activity(influences, 10) == 0);
    assert(fabsf(activity(influences, 15) - 2) < .0001f);
    assert(activity(influences, 20) == 0);
    assert(fabsf(activity(influences, 12.5f) - .5f) < .0001f);
    GAME_GameSurfaceInfluence_get(world, entity)->inner_radius = 20;
    render_influences_collect(influences, world, NULL, .1f);
    assert(activity(influences, 15) == 0);
    render_influences_free(influences);
    ecs_world_free(world);
}

static void authored_floor()
{
    FILE *file = fopen("content/materials/ArenaFloor.kdl", "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *json = malloc((size_t)size + 1);
    assert(json && fread(json, 1, (size_t)size, file) == (size_t)size);
    json[size] = 0;
    fclose(file);
    behavior_asset *asset = behavior_asset_load(json);
    free(json);
    assert(asset);
    float tile = 0;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        const node *n = asset->graph->nodes[i];
        if (n->reference_name && !strcmp(n->reference_name, "Tile Size")) {
            tile = n->output_pins[0].default_value.f;
        }
    }
    assert(tile > 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(asset->graph, &ir, &error));
    float contours[2][161], peaks[2] = { 0 }, center[2];
    for (int beat = 0; beat < 2; beat++) {
        shader_sample_context sample = {
            .beat_pulse = (float)beat,
            .world_position = { tile * .5f / .8660254f, 0, tile / 3 }
        };
        float result[SHADER_OUTPUT_MAX][4];
        assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
        center[beat] = result[1][0] + result[1][1] + result[1][2];
        // Cross the authored triangle at its centroid with no scene activity.
        for (int i = 0; i < 161; i++) {
            sample.world_position[0] = tile * (float)i / 80;
            assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
            contours[beat][i] = result[1][0] + result[1][1] + result[1][2];
            peaks[beat] = fmaxf(peaks[beat], contours[beat][i]);
        }
        assert(peaks[beat] > .001f && center[beat] < peaks[beat] * .01f);
    }
    float motion = 0;
    for (int i = 0; i < 161; i++) {
        motion += fabsf(contours[1][i] - contours[0][i]);
    }
    assert(motion > peaks[0]);
    printf(
        "ArenaFloor CPU: idle contour %.6f, center %.6f; beat contour %.6f, "
        "center %.6f\n",
        peaks[0],
        center[0],
        peaks[1],
        center[1]
    );
    shader_graph_free(&ir);
    behavior_asset_free(asset);
}

void test_surface_cpu()
{
    emitter_motion();
    emitter_cadence();
    emitter_contract();
    visual_offset_node();
    authored_floor();
    visual_motion_defaults();
    rhythm_and_defaults();
    annular_influence();
    parts_and_trails();
    influence_history();
    influence_capacity();
    visual_culling();
    influence_graph();
    shader_graph ir;
    shader_graph_init(&ir);
    const shader_op ops[] = { SHADER_POSITION,
                              SHADER_UV,
                              SHADER_OBJECT_POSITION,
                              SHADER_WORLD_POSITION,
                              SHADER_NORMAL,
                              SHADER_VIEW_DIRECTION,
                              SHADER_TIME };
    const int widths[] = { 2, 2, 3, 3, 3, 3, 1 };
    shader_sample_context sample = { .position = { 3, -4 },
                                     .uv = { .2f, .8f },
                                     .object_position = { -2, 7, 11 },
                                     .world_position = { 103, -41, 9 },
                                     .normal = { 0, .6f, .8f },
                                     .view_direction = { .8f, 0, .6f },
                                     .time = 2.75f };
    const float *expected[] = {
        sample.position,       sample.uv,     sample.object_position,
        sample.world_position, sample.normal, sample.view_direction,
        &sample.time
    };
    ir.output_count = 7;
    for (int i = 0; i < 7; i++) {
        ir.output[i] = shader_graph_add(&ir, ops[i], widths[i]);
    }
    float result[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    for (int i = 0; i < 7; i++) {
        for (int c = 0; c < widths[i]; c++) {
            assert(result[i][c] == expected[i][c]);
        }
    }
    for (int shape = 0; shape < 2; shape++) {
        behavior_asset *asset = behavior_asset_new("Mismatched shader output");
        asset->domain = shape ? BEHAVIOR_DOMAIN_SHAPE : BEHAVIOR_DOMAIN_MATERIAL;
        node *wrong_output = shader_graph_add_output(asset->graph, !shape);
        assert(!shader_graph_from_graph(asset->graph, &ir, &error));
        assert(error.node == wrong_output->id && strstr(error.message, "domain"));
        behavior_asset_free(asset);
    }
    shader_graph_free(&ir);
    puts("surface CPU PASS: distinct sample inputs and mismatched material/shape output rejection");
}

typedef struct
{
    ecs_world *world;
    game_particles *particles;
    render_arena *renderer;
    render_arena_post *post;
    render_arena_camera camera;
    CF_Canvas canvas;
    CF_DrawStats3d stats;
} fixture;

static fixture fixture_new()
{
    fixture f = { .world = ecs_world_new(), .particles = game_particles_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.camera.view = cf_look_at((CF_V3){ 0, 0, 400 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });
    f.camera.projection = cf_perspective(.7853981634f, 1, 1, 1000);
    f.post = render_arena_post_new();
    GameEnvironment environment = render_arena_post_defaults();
    environment.bloom_strength = 0;
    assert(render_arena_post_prepare(f.post, SIDE, SIDE, &environment));
    f.canvas = render_arena_post_target(f.post);
    f.renderer = render_arena_new();
    assert(f.canvas.id && f.renderer);
    cf_canvas_set_clear_color(f.canvas, (CF_Color){ 0, 0, 0, 1 });
    return f;
}

static void fixture_free(fixture *f)
{
    render_shader_clear_world(f->world);
    render_arena_free(f->renderer);
    game_particles_free(f->particles);
    render_arena_post_free(f->post);
    ecs_world_free(f->world);
}

static ecs_entity actor(fixture *f, const char *name, CF_V3 position, int primitive, float size, const char *parameters)
{
    ecs_entity e = ecs_world_create_entity(f->world);
    GameTransform transform = { position.x, position.z };
    GameGeometry3D shape = { primitive, size, size, size, position.y };
    GameShader binding = { .material = name, .parameters = parameters };
    assert(
        ecs_world_record_add(f->world, e, GAME_COMPONENT_GameTransform.id, &transform, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(f->world, e, GAME_COMPONENT_GameGeometry3D.id, &shape, 0) == ECS_OK
    );
    assert(ecs_world_record_add(f->world, e, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return e;
}

static void read_canvas(fixture *f, CF_Pixel pixels[PIXELS])
{
    CF_Readback readback = cf_canvas_readback(render_arena_post_output(f->post));
    assert(readback.id);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    assert(cf_readback_data(readback, pixels, PIXELS * sizeof *pixels) == PIXELS * sizeof *pixels);
    cf_destroy_readback(readback);
}

static void draw(fixture *f, const behavior_asset *preview, float time, CF_Pixel pixels[PIXELS])
{
    cf_draw3d_stats();
    cf_app_update(NULL);
    bool ok = preview
                ? render_arena_preview(f->renderer, preview, &f->camera, NULL, time, &(render_arena_activity){ 0 })
                : render_arena_draw(f->renderer, f->world, f->particles, &f->camera, time, NULL);
    if (!ok) {
        fprintf(stderr, "Surface draw: %s\n", render_arena_error(f->renderer));
    }
    assert(ok);
    cf_render_to(f->canvas, true);
    assert(render_arena_post_process(f->post));
    cf_app_draw_onto_screen(false);
    f->stats = cf_draw3d_stats();
    read_canvas(f, pixels);
}

static void visual_errors()
{
    fixture f = fixture_new();
    behavior_asset *a = material("Visual error retention", true);
    vector(a->graph, surface(a), 0, 0, (CF_V3){ .5f, .3f, .2f });
    publish(a);
    for (int i = 0; i < 3; i++) {
        ecs_entity e = actor(&f, a->name, (CF_V3){ (i - 1) * 70.0f }, GAME_PRIMITIVE_BOX, 50, "");
        GameMeshParts parts = { "(array)-\n" };
        GameVisualTransform transform = { .scale = 1 };
        assert(
            ecs_world_record_add(f.world, e, GAME_COMPONENT_GameMeshParts.id, &parts, 0) == ECS_OK
        );
        assert(
            ecs_world_record_add(f.world, e, GAME_COMPONENT_GameVisualTransform.id, &transform, 0) == ECS_OK
        );
        assert(ecs_world_apply_commands(f.world) == ECS_OK);
    }
    ecs_entity entities[3];
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameGeometry3D.id };
    ecs_query query;
    assert(ecs_query_begin(f.world, &query, required, 2) == ECS_OK);
    int count = 0;
    ecs_entity e;
    while (ecs_query_next(&query, &e)) {
        assert(count < 3);
        entities[count++] = e;
    }
    ecs_query_end(&query);
    assert(count == 3);
    CF_Pixel baseline[PIXELS], after[PIXELS];
    draw(&f, NULL, 0, baseline);
    const char *messages[] = { "Mesh Parts", "Unknown surface material", "nonnegative scale" };
    for (int failure = 0; failure < 3; failure++) {
        GAME_GameMeshParts_get(f.world, entities[0])->parts = failure == 0
                                                                ? "{broken"
                                                                : "(array)-\n";
        GAME_GameMeshParts_get(f.world, entities[1])->parts = failure == 2
                                                                ? "{also broken"
                                                                : "(array)-\n";
        GAME_GameVisualTransform_get(f.world, entities[0])->scale = failure == 2
                                                                      ? -1
                                                                      : 1;
        GAME_GameShader_get(f.world, entities[1])
            ->material = "Missing surface material";
        cf_draw3d_stats();
        cf_app_update(NULL);
        assert(!render_arena_draw(f.renderer, f.world, f.particles, &f.camera, 0, NULL));
        assert(strstr(render_arena_error(f.renderer), messages[failure]));
        cf_app_draw_onto_screen(false);
        assert(cf_draw3d_stats().instances == 0);
        read_canvas(&f, after);
        assert(!memcmp(baseline, after, sizeof baseline));
    }
    GAME_GameMeshParts_get(f.world, entities[1])->parts = "(array)-\n";
    GAME_GameVisualTransform_get(f.world, entities[0])->scale = 1;
    GAME_GameShader_get(f.world, entities[1])->material = a->name;
    draw(&f, NULL, 0, after);
    assert(!*render_arena_error(f.renderer) && !memcmp(baseline, after, sizeof baseline));
    fixture_free(&f);
    behavior_asset_free(a);
}

static CF_Pixel at(fixture *f, const CF_Pixel *pixels, CF_V3 world)
{
    CF_V3 p = render_arena_project(&f->camera, world);
    int x = (int)((p.x * .5f + .5f) * SIDE);
    int y = (int)((.5f - p.y * .5f) * SIDE);
    assert(x >= 0 && x < SIDE && y >= 0 && y < SIDE);
    return pixels[y * SIDE + x];
}

static float difference(const CF_Pixel *a, const CF_Pixel *b)
{
    double sum = 0;
    int count = 0;
    for (int i = 0; i < PIXELS; i++) {
        const uint8_t *left = (const uint8_t *)&a[i],
                      *right = (const uint8_t *)&b[i];
        if (!(left[0] || left[1] || left[2] || right[0] || right[1] || right[2])) {
            continue;
        }
        for (int c = 0; c < 3; c++) {
            sum += abs(left[c] - right[c]);
        }
        count += 3;
    }
    assert(count);
    return (float)(sum / count);
}

static void write_parameter(ecs_world *world, ecs_entity entity, const char *label, pin_value value)
{
    behavior_asset *a = behavior_asset_new("Surface parameter writer");
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Paint") == 0);
    node *write = add(g, "Set Material Parameter");
    pin_value target = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(entity) };
    pin_value name = { .type = PIN_STR, .str = label };
    assert(graph_set_pin_default(g, write, 0, 1, &target) == 0);
    assert(graph_set_pin_default(g, write, 0, 2, &name) == 0);
    assert(graph_set_pin_type(g, write, 0, 3, value.type, value.type_name) == 0);
    assert(graph_set_pin_default(g, write, 0, 3, &value) == 0);
    assert(graph_connect_exec(g, event, 0, write, 0, 0) == 0);
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    flow_error error;
    flow_run *run = behavior_instance_dispatch(instance, "Paint", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    behavior_instance_destroy(instance);
    behavior_asset_free(a);
}

static void instance_checks()
{
    behavior_asset *a = material("Surface instances", true);
    node *tint = parameter(a->graph, "Tint", 0, 3);
    vector(a->graph, tint, 1, 0, (CF_V3){ 1, 1, 1 });
    assert(graph_connect_data(a->graph, tint, 0, surface(a), 0) == 0);
    publish(a);
    fixture f = fixture_new();
    ecs_entity left = actor(&f, a->name, (CF_V3){ -60, 0, 0 }, GAME_PRIMITIVE_BOX, 70, "(array)Tint 1 0 0; \n");
    actor(&f, a->name, (CF_V3){ 60, 0, 0 }, GAME_PRIMITIVE_BOX, 70, "(array)Tint 0 0 1; \n");
    CF_Pixel before[PIXELS], after[PIXELS];
    draw(&f, NULL, 0, before);
    assert(f.stats.instances == 2 && f.stats.commands == 1);
    CF_Pixel l = at(&f, before, (CF_V3){ -60, 0, 35 });
    CF_Pixel r = at(&f, before, (CF_V3){ 60, 0, 35 });
    assert(l.colors.r > 150 && l.colors.g < 3 && r.colors.b > 150 && r.colors.g < 3);
    CF_V3 green = { 0, 1, 0 };
    write_parameter(
        f.world,
        left,
        "Tint",
        (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = &green }
    );
    draw(&f, NULL, 0, after);
    assert(f.stats.instances == 2 && f.stats.commands == 1);
    l = at(&f, after, (CF_V3){ -60, 0, 35 });
    CF_Pixel untouched = at(&f, after, (CF_V3){ 60, 0, 35 });
    assert(l.colors.g > 150 && l.colors.r < 3 && memcmp(&r, &untouched, sizeof r) == 0);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void boolean_checks()
{
    behavior_asset *a = material("Surface boolean", false);
    node *toggle = parameter(a->graph, "Unlit", 0, 1);
    assert(graph_set_pin_type(a->graph, toggle, 1, 0, PIN_BOOL, NULL) == 0);
    assert(graph_connect_data(a->graph, toggle, 0, surface(a), 6) == 0);
    publish(a);
    int id = render_shader_find(a->name);
    float values[SHADER_PARAMETER_MAX][4], result[SHADER_OUTPUT_MAX][4];
    shader_sample_context sample = { 0 };
    shader_error error = { 0 };
    for (int unlit = 0; unlit < 2; unlit++) {
        render_shader_parameters(id, NULL, (ecs_entity){ 0 }, unlit ? "Unlit #true; \n" : "Unlit #false; \n", values);
        assert(render_shader_sample(id, &sample, values, result, &error));
        assert(result[6][0] == unlit);
    }
    fixture f = fixture_new();
    ecs_entity entity = actor(&f, a->name, (CF_V3){ 0 }, GAME_PRIMITIVE_SPHERE, 100, "Unlit #true; \n");
    CF_Pixel unlit[PIXELS], lit[PIXELS], direct[PIXELS];
    draw(&f, NULL, 0, unlit);
    write_parameter(f.world, entity, "Unlit", (pin_value){ .type = PIN_BOOL, .b = false });
    draw(&f, NULL, 0, lit);
    assert(difference(unlit, lit) > 5);
    assert(graph_remove_edge(a->graph, toggle, 0, surface(a), 6) == 0);
    publish(a);
    draw(&f, NULL, 0, direct);
    assert(memcmp(lit, direct, sizeof lit) == 0);
    pin_value enabled = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(a->graph, surface(a), 0, 6, &enabled) == 0);
    publish(a);
    draw(&f, NULL, 0, direct);
    assert(memcmp(unlit, direct, sizeof unlit) == 0);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void material_checks()
{
    behavior_asset *a = material("Surface lighting", false);
    node *out = surface(a);
    vector(a->graph, out, 0, 0, (CF_V3){ .5f, .18f, .06f });
    scalar(a->graph, out, 0, 2, .05f);
    publish(a);
    fixture f = fixture_new();
    actor(&f, a->name, (CF_V3){ 0 }, GAME_PRIMITIVE_SPHERE, 100, "");
    CF_Pixel base[PIXELS], changed[PIXELS], preview[PIXELS];
    draw(&f, NULL, 0, base);
    scalar(a->graph, out, 0, 2, .85f);
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 1);
    memcpy(base, changed, sizeof base);
    scalar(a->graph, out, 0, 3, 1);
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 8);
    memcpy(base, changed, sizeof base);
    vector(a->graph, out, 0, 4, (CF_V3){ 0, -1, 0 });
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 3);
    memcpy(base, changed, sizeof base);
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(a->graph, out, 0, 6, &unlit) == 0);
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 20);
    memcpy(base, changed, sizeof base);
    vector(a->graph, out, 0, 0, (CF_V3){ .05f, .2f, .6f });
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 15);
    memcpy(base, changed, sizeof base);
    vector(a->graph, out, 0, 1, (CF_V3){ 0, 0, 2 });
    publish(a);
    draw(&f, NULL, 0, changed);
    assert(difference(base, changed) > 15);
    draw(&f, a, 0, preview);
    assert(memcmp(changed, preview, sizeof changed) == 0);

    unsigned revision = render_shader_revision();
    node *cycle = add(a->graph, "Shader Add");
    assert(graph_connect_data(a->graph, cycle, 0, cycle, 0) == 0);
    assert(graph_connect_data(a->graph, cycle, 0, out, 2) == 0);
    shader_error error = { 0 };
    assert(!render_shader_upsert(a, &error));
    assert(error.node == cycle->id && error.message[0]);
    assert(render_shader_revision() == revision);
    draw(&f, NULL, 0, base);
    assert(memcmp(changed, base, sizeof base) == 0);
    draw(&f, a, 0, preview);
    assert(render_arena_error(f.renderer)[0]);
    assert(memcmp(changed, preview, sizeof changed) == 0);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void opacity_checks()
{
    behavior_asset *red = material("Surface translucent red", true);
    behavior_asset *green = material("Surface translucent green", true);
    behavior_asset *blue = material("Surface opaque blue", true);
    vector(red->graph, surface(red), 0, 0, (CF_V3){ 1, 0, 0 });
    vector(green->graph, surface(green), 0, 0, (CF_V3){ 0, 1, 0 });
    vector(blue->graph, surface(blue), 0, 0, (CF_V3){ 0, 0, 1 });
    scalar(red->graph, surface(red), 0, 5, .5f);
    scalar(green->graph, surface(green), 0, 5, .5f);
    publish(red);
    publish(green);
    publish(blue);
    CF_Pixel first[PIXELS], second[PIXELS];
    for (int order = 0; order < 2; order++) {
        fixture f = fixture_new();
        const char *names[] = { blue->name, green->name, red->name };
        for (int i = 0; i < 3; i++) {
            int index = order ? 2 - i : i;
            actor(&f, names[index], (CF_V3){ 0, 0, -100 + 100 * index }, GAME_PRIMITIVE_BOX, 80, "");
        }
        draw(&f, NULL, 0, order ? second : first);
        fixture_free(&f);
    }
    assert(memcmp(first, second, sizeof first) == 0);
    CF_Pixel p = first[SIDE / 2 * SIDE + SIDE / 2];
    assert(p.colors.r > p.colors.g + 20 && p.colors.g > 20 && p.colors.b > 20);
    /* Red over green over blue yields linear (.5,.25,.25), then tone mapping.
     * Mapping each surface before blending produces different displayed values. */
    assert(abs(p.r - 156) <= 2 && abs(p.g - 124) <= 2 && abs(p.b - 124) <= 2);
    fixture f = fixture_new();
    actor(&f, blue->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 80, "");
    actor(&f, red->name, (CF_V3){ 0, 0, -100 }, GAME_PRIMITIVE_BOX, 80, "");
    draw(&f, NULL, 0, first);
    p = first[SIDE / 2 * SIDE + SIDE / 2];
    assert(p.colors.b > 150 && p.colors.r < 3);
    fixture_free(&f);
    f = fixture_new();
    actor(&f, blue->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 80, "");
    actor(&f, red->name, (CF_V3){ 0, 0, 100 }, GAME_PRIMITIVE_BOX, 80, "");
    scalar(red->graph, surface(red), 0, 5, 0);
    publish(red);
    draw(&f, NULL, 0, first);
    p = first[SIDE / 2 * SIDE + SIDE / 2];
    assert(p.colors.b > 150 && p.colors.r < 3);
    scalar(red->graph, surface(red), 0, 5, 1);
    publish(red);
    draw(&f, NULL, 0, second);
    p = second[SIDE / 2 * SIDE + SIDE / 2];
    assert(p.colors.r > 150 && p.colors.b < 3);
    fixture_free(&f);
    /* A finite graph result can exceed the attachment's half-float range.
     * The front parameter-driven surface uses blending even at opacity one. */
    vector(blue->graph, surface(blue), 0, 1, (CF_V3){ 1e8f, 0, 0 });
    node *opacity = parameter(green->graph, "Opacity", 0, 1);
    scalar(green->graph, opacity, 1, 0, 1);
    assert(graph_connect_data(green->graph, opacity, 0, surface(green), 5) == 0);
    publish(blue);
    publish(green);
    f = fixture_new();
    actor(&f, blue->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 80, "");
    actor(&f, green->name, (CF_V3){ 0, 0, 100 }, GAME_PRIMITIVE_BOX, 80, "Opacity 1; \n");
    draw(&f, NULL, 0, first);
    p = first[SIDE / 2 * SIDE + SIDE / 2];
    assert(p.r < 2 && abs(p.g - 188) <= 2 && p.b < 2);
    fixture_free(&f);
    behavior_asset_free(red);
    behavior_asset_free(green);
    behavior_asset_free(blue);
}

static behavior_asset *context_material(const char *source_name, int width, float scale, float bias)
{
    behavior_asset *a = material("Surface sample context", true);
    graph *g = a->graph;
    node *source = add(g, source_name);
    if (width < 3) {
        node *rgb = add(g, "Shader Vector3");
        for (int c = 0; c < width; c++) {
            node *channel = source;
            if (width == 2) {
                channel = add(g, c ? "Shader Y" : "Shader X");
                assert(graph_connect_data(g, source, 0, channel, 0) == 0);
            }
            assert(graph_connect_data(g, channel, 0, rgb, c) == 0);
        }
        source = rgb;
    }
    node *multiply = add(g, "Shader Multiply"), *offset = add(g, "Shader Add");
    assert(graph_connect_data(g, source, 0, multiply, 0) == 0);
    vector(g, multiply, 0, 1, (CF_V3){ scale, scale, scale });
    assert(graph_connect_data(g, multiply, 0, offset, 0) == 0);
    vector(g, offset, 0, 1, (CF_V3){ bias, bias, bias });
    assert(graph_connect_data(g, offset, 0, surface(a), 0) == 0);
    return a;
}

static int display_channel(float value)
{
    value = fmaxf(value, 0);
    float mapped = value / (1 + value);
    float srgb = mapped <= .0031308f ? mapped * 12.92f
                                     : 1.055f * powf(mapped, 1 / 2.4f) - .055f;
    return (int)lroundf(srgb * 255);
}

static void mixed_mesh_instances()
{
    behavior_asset *materials[] = { material("Surface mixed Tint", true),
                                    material("Surface mixed Accent", true) };
    const char *labels[] = { "Tint", "Accent" };
    for (int i = 0; i < 2; i++) {
        node *color = parameter(materials[i]->graph, labels[i], i * 3, 3);
        vector(materials[i]->graph, color, 1, 0, (CF_V3){ 1, 1, 1 });
        assert(
            graph_connect_data(materials[i]->graph, color, 0, surface(materials[i]), 0) == 0
        );
        publish(materials[i]);
    }
    fixture f = fixture_new();
    const struct
    {
        int primitive, material;
        CF_V3 position;
        const char *parameters;
    } instances[] = {
        { GAME_PRIMITIVE_BOX, 0, { -65, 60, 0 }, "(array)Tint 1 0 0; \n" },
        { GAME_PRIMITIVE_TRIANGLE_PRISM, 1, { 65, 60, 0 }, "(array)Accent 0 1 0; \n" },
        { GAME_PRIMITIVE_BOX, 1, { -65, -60, 0 }, "(array)Accent 0 0 1; \n" },
        { GAME_PRIMITIVE_TRIANGLE_PRISM, 0, { 65, -60, 0 }, "(array)Tint 1 1 0; \n" },
    };
    ecs_entity entities[4];
    for (int i = 0; i < 4; i++) {
        entities[i] = actor(
            &f,
            materials[instances[i].material]->name,
            instances[i].position,
            instances[i].primitive,
            70,
            instances[i].parameters
        );
    }
    CF_V3 colors[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0 } };
    CF_Pixel pixels[PIXELS], before[4];
    for (int update = 0; update < 2; update++) {
        if (update) {
            colors[1] = (CF_V3){ 1, 0, 1 };
            write_parameter(
                f.world,
                entities[1],
                "Accent",
                (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = &colors[1] }
            );
        }
        draw(&f, NULL, 0, pixels);
        assert(f.stats.instances == 4 && f.stats.commands == 2);
        for (int i = 0; i < 4; i++) {
            CF_V3 point = instances[i].position;
            point.z = instances[i].primitive == GAME_PRIMITIVE_BOX ? 35 : 17.5f;
            CF_Pixel pixel = at(&f, pixels, point);
            int actual[] = { pixel.colors.r, pixel.colors.g, pixel.colors.b };
            float expected[] = { colors[i].x, colors[i].y, colors[i].z };
            for (int channel = 0; channel < 3; channel++) {
                int want = display_channel(expected[channel]);
                if (abs(actual[channel] - want) > 3) {
                    fprintf(
                        stderr,
                        "Surface mixed instance %d update %d channel %d: GPU "
                        "%d, expected %d\n",
                        i,
                        update,
                        channel,
                        actual[channel],
                        want
                    );
                    assert(false);
                }
            }
            if (update && i != 1) {
                assert(memcmp(&pixel, &before[i], sizeof pixel) == 0);
            }
            before[i] = pixel;
        }
    }
    fixture_free(&f);
    for (int i = 0; i < 2; i++) {
        behavior_asset_free(materials[i]);
    }
}

static void context_pixels()
{
    fixture f = fixture_new();
    actor(&f, "Surface sample context", (CF_V3){ 20, 10, 0 }, GAME_PRIMITIVE_BOX, 100, "");
    f.camera.view = cf_look_at((CF_V3){ 20, 10, 400 }, (CF_V3){ 20, 10, 0 }, (CF_V3){ 0, 1, 0 });
    int x = 70, y = 59;
    CF_V2 ndc = { 2 * (x + .5f) / SIDE - 1, 1 - 2 * (y + .5f) / SIDE };
    CF_Ray3 ray = render_arena_ray(&f.camera, ndc);
    float distance = (50 - ray.p.z) / ray.d.z;
    CF_V3 world = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, distance));
    CF_V3 local = cf_sub_v3(world, (CF_V3){ 20, 10, 0 });
    CF_V3 view = cf_norm_v3(cf_sub_v3((CF_V3){ 20, 10, 400 }, world));
    shader_sample_context sample = {
        .position = { local.x, local.z },
        .uv = { local.x / 100 + .5f, .5f - local.y / 100 },
        .object_position = { local.x, local.y, local.z },
        .world_position = { world.x, world.y, world.z },
        .normal = { 0, 0, 1 },
        .view_direction = { view.x, view.y, view.z },
        .time = 1.25f
    };
    struct
    {
        const char *name;
        int width;
        float scale, bias;
    } cases[] = { { "Shader UV", 2, 1, 0 },
                  { "Shader Position", 2, .005f, .4f },
                  { "Shader ObjectPosition", 3, .005f, .4f },
                  { "Shader WorldPosition", 3, .005f, .4f },
                  { "Shader Normal", 3, .3f, .4f },
                  { "Shader ViewDirection", 3, .3f, .4f },
                  { "Shader Time", 1, .1f, .1f } };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        behavior_asset *a = context_material(
            cases[i].name,
            cases[i].width,
            cases[i].scale,
            cases[i].bias
        );
        publish(a);
        shader_graph ir;
        shader_graph_init(&ir);
        shader_error error;
        assert(shader_graph_from_graph(a->graph, &ir, &error));
        float expected[SHADER_OUTPUT_MAX][4];
        assert(shader_graph_eval_outputs(&ir, &sample, NULL, expected, &error));
        CF_Pixel pixels[PIXELS];
        draw(&f, NULL, sample.time, pixels);
        uint8_t *pixel = (uint8_t *)&pixels[y * SIDE + x];
        for (int c = 0; c < 3; c++) {
            int want = display_channel(expected[0][c]);
            if (abs((int)pixel[c] - want) > 3) {
                fprintf(stderr, "%s channel %d: GPU %d, CPU %d\n", cases[i].name, c, pixel[c], want);
                assert(false);
            }
        }
        shader_graph_free(&ir);
        behavior_asset_free(a);
    }
    fixture_free(&f);
}

static void image_png(const char *path, const CF_Pixel pixels[4])
{
    CF_Image image = { .w = 2, .h = 2, .pix = (CF_Pixel *)pixels };
    void *bytes;
    int size;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &bytes, &size)));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    cf_free(bytes);
}

static behavior_asset *image_material(const char *name, const char *key, bool alpha)
{
    behavior_asset *a = material(name, true);
    graph *g = a->graph;
    node *image = add(g, "Shader Image Texture");
    pin_value path = { .type = PIN_STR, .str = key },
              linear = { .type = PIN_I32, .i = SHADER_IMAGE_LINEAR },
              nearest = { .type = PIN_I32, .i = SHADER_IMAGE_NEAREST },
              clamp = { .type = PIN_I32, .i = SHADER_IMAGE_CLAMP };
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_IMAGE, &path) == 0);
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_COLOR_SPACE, &linear) == 0);
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_FILTER, &nearest) == 0);
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_WRAP, &clamp) == 0);
    node *tint = parameter(g, "Tint", 0, 3), *multiply = add(g, "Shader Multiply");
    vector(g, tint, 1, 0, (CF_V3){ 1, 1, 1 });
    assert(graph_connect_data(g, image, 0, multiply, 0) == 0);
    assert(graph_connect_data(g, tint, 0, multiply, 1) == 0);
    assert(graph_connect_data(g, multiply, 0, surface(a), 0) == 0);
    if (alpha) {
        assert(graph_connect_data(g, image, 1, surface(a), 5) == 0);
    }
    publish(a);
    return a;
}

static void image_pixels_match(CF_Pixel pixel, CF_Pixel source, CF_V3 tint)
{
    const unsigned char *actual = (const unsigned char *)&pixel;
    const unsigned char *channels = (const unsigned char *)&source;
    float scale[] = { tint.x, tint.y, tint.z };
    for (int c = 0; c < 3; c++) {
        int want = display_channel((float)channels[c] / 255 * scale[c]);
        if (abs((int)actual[c] - want) > 3) {
            fprintf(stderr, "Surface image channel%d: GPU %d, expected %d\n", c, actual[c], want);
            assert(false);
        }
    }
}

static void image_batches(const char *directory)
{
    behavior_asset *materials[9];
    CF_Pixel colors[9];
    CF_V3 positions[9][6];
    ecs_entity first = { 0 };
    fixture f = fixture_new();
    for (int i = 0; i < 9; i++) {
        char file[256], key[128], name[64];
        snprintf(file, sizeof file, "%s/Image%d.png", directory, i);
        snprintf(key, sizeof key, "surface-image-test/Image%d.png", i);
        snprintf(name, sizeof name, "Surface image batch %d", i);
        colors[i] = (CF_Pixel){ .colors = { 64 + 19 * i, 224 - 17 * i, 128 + 13 * i, 255 } };
        CF_Pixel image[] = { colors[i], colors[i], colors[i], colors[i] };
        image_png(file, image);
        materials[i] = image_material(name, key, false);
        for (int j = 0; j < 6; j++) {
            CF_V3 position = { (i % 3 - 1) * 100 + (j % 3 - 1) * 24,
                               (i / 3 - 1) * 100 + (j < 3 ? 15 : -15),
                               0 };
            positions[i][j] = position;
            positions[i][j].z = j < 3 ? 8 : 4;
            ecs_entity entity = actor(
                &f,
                materials[i]->name,
                position,
                j < 3 ? GAME_PRIMITIVE_BOX : GAME_PRIMITIVE_TRIANGLE_PRISM,
                16,
                j == 0 ? "(array)Tint 1 0.5 0.25; \n" : ""
            );
            if (!i && !j) {
                first = entity;
            }
        }
    }
    CF_Pixel pixels[PIXELS];
    CF_V3 runtime = { .25f, 1, .5f };
    for (int update = 0; update < 2; update++) {
        if (update) {
            write_parameter(
                f.world,
                first,
                "Tint",
                (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = &runtime }
            );
        }
        draw(&f, NULL, 0, pixels);
        assert(f.stats.instances == 54 && f.stats.commands == 18);
        for (int i = 0; i < 9; i++) {
            for (int j = 0; j < 6; j++) {
                CF_V3 tint = j ? (CF_V3){ 1, 1, 1 } : (CF_V3){ 1, .5f, .25f };
                if (update && !i && !j) {
                    tint = runtime;
                }
                image_pixels_match(at(&f, pixels, positions[i][j]), colors[i], tint);
            }
        }
    }
    fixture_free(&f);
    for (int i = 0; i < 9; i++) {
        behavior_asset_free(materials[i]);
    }
}

static void image_faces_and_reload(const char *directory)
{
    char file[256];
    snprintf(file, sizeof file, "%s/Faces.png", directory);
    const char *key = "surface-image-test/Faces.png";
    CF_Pixel pattern[] = { { .colors = { 255, 0, 0, 255 } },
                           { .colors = { 0, 255, 0, 255 } },
                           { .colors = { 0, 0, 255, 255 } },
                           { .colors = { 255, 255, 0, 255 } } };
    image_png(file, pattern);
    behavior_asset *a = image_material("Surface image coordinates", key, false);
    fixture f = fixture_new();
    actor(&f, a->name, (CF_V3){ -65, 0, 0 }, GAME_PRIMITIVE_BOX, 100, "");
    actor(&f, a->name, (CF_V3){ 65, 0, 0 }, GAME_PRIMITIVE_TRIANGLE_PRISM, 100, "");
    CF_Pixel before[PIXELS], after[PIXELS];
    draw(&f, NULL, 0, before);
    for (int primitive = 0; primitive < 2; primitive++) {
        for (int quadrant = 0; quadrant < 4; quadrant++) {
            float x = quadrant % 2 ? 22 : -22;
            float y = quadrant < 2 ? 22 : -22;
            float z = primitive ? (50 - x) * .5f : 50;
            CF_V3 point = { (primitive ? 65 : -65) + x, y, z };
            image_pixels_match(at(&f, before, point), pattern[quadrant], (CF_V3){ 1, 1, 1 });
        }
    }
    FILE *broken = fopen(file, "wb");
    assert(broken && fputs("not a PNG", broken) >= 0 && fclose(broken) == 0);
    char message[256];
    assert(!render_image_reload(key, message, sizeof message) && *render_image_error(key));
    draw(&f, NULL, 0, after);
    assert(!memcmp(before, after, sizeof before));
    CF_Pixel green = { .colors = { 0, 255, 0, 255 } };
    for (int i = 0; i < 4; i++) {
        pattern[i] = green;
    }
    image_png(file, pattern);
    assert(render_image_reload(key, message, sizeof message) && !*render_image_error(key));
    draw(&f, NULL, 0, after);
    assert(difference(before, after) > 20);
    image_pixels_match(at(&f, after, (CF_V3){ -65, 0, 50 }), green, (CF_V3){ 1, 1, 1 });
    fixture_free(&f);
    behavior_asset_free(a);

    for (int i = 0; i < 4; i++) {
        pattern[i].colors.a = 128;
    }
    image_png(file, pattern);
    assert(render_image_reload(key, message, sizeof message));
    a = image_material("Surface image alpha", key, true);
    behavior_asset *backdrop = material("Surface image backdrop", true);
    vector(backdrop->graph, surface(backdrop), 0, 0, (CF_V3){ 0, 0, 1 });
    publish(backdrop);
    for (int order = 0; order < 2; order++) {
        f = fixture_new();
        for (int j = 0; j < 2; j++) {
            bool front = (j == order);
            actor(
                &f,
                front ? a->name : backdrop->name,
                (CF_V3){ 0, 0, front ? 100 : 0 },
                GAME_PRIMITIVE_BOX,
                80,
                ""
            );
        }
        draw(&f, NULL, 0, order ? after : before);
        fixture_free(&f);
    }
    assert(!memcmp(before, after, sizeof before));
    CF_Pixel center = before[SIDE / 2 * SIDE + SIDE / 2];
    assert(center.colors.r < 3 && center.colors.g > 80 && center.colors.b > 80);
    behavior_asset_free(a);
    behavior_asset_free(backdrop);
}

static void image_surfaces()
{
    char directory[] = "/tmp/chibi-surface-images-XXXXXX";
    assert(mkdtemp(directory));
    assert(!cf_is_error(cf_fs_mount(directory, "/surface-image-test", true)));
    image_batches(directory);
    image_faces_and_reload(directory);
    assert(!cf_is_error(cf_fs_dismount(directory)));
    for (int i = 0; i < 10; i++) {
        char file[256];
        if (i < 9) {
            snprintf(file, sizeof file, "%s/Image%d.png", directory, i);
        } else {
            snprintf(file, sizeof file, "%s/Faces.png", directory);
        }
        assert(unlink(file) == 0);
    }
    assert(rmdir(directory) == 0);
}

static ecs_entity point_light(fixture *f, CF_V3 position, unsigned color, float intensity, float radius)
{
    ecs_entity entity = ecs_world_create_entity(f->world);
    GameTransform transform = { position.x, position.z };
    GamePointLight light = { color, intensity, radius, position.y };
    assert(
        ecs_world_record_add(f->world, entity, GAME_COMPONENT_GameTransform.id, &transform, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(f->world, entity, GAME_COMPONENT_GamePointLight.id, &light, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return entity;
}

static void point_lighting()
{
    fixture f = fixture_new();
    behavior_asset *a = material("Point light surface", false);
    vector(a->graph, surface(a), 0, 0, (CF_V3){ .2f, .2f, .2f });
    scalar(a->graph, surface(a), 0, 2, .8f);
    publish(a);
    actor(&f, a->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 100, "");
    CF_Pixel baseline[PIXELS], lit[PIXELS], moved[PIXELS];
    draw(&f, NULL, 0, baseline);
    ecs_entity entity = point_light(&f, (CF_V3){ 0, 0, 100 }, 0xff0000ffu, 6, 90);
    draw(&f, NULL, 0, lit);
    CF_Pixel dark = at(&f, baseline, (CF_V3){ 0, 0, 50 });
    CF_Pixel bright = at(&f, lit, (CF_V3){ 0, 0, 50 });
    assert(bright.colors.r > dark.colors.r + 30);
    assert(
        abs(bright.colors.g - dark.colors.g) <= 1 &&
        abs(bright.colors.b - dark.colors.b) <= 1
    );

    GAME_GameTransform_get(f.world, entity)->x = 200;
    draw(&f, NULL, 0, moved);
    assert(!memcmp(baseline, moved, sizeof moved));
    GAME_GameTransform_get(f.world, entity)->x = 0;
    GAME_GamePointLight_get(f.world, entity)->radius = 40;
    draw(&f, NULL, 0, moved);
    assert(!memcmp(baseline, moved, sizeof moved));
    GAME_GamePointLight_get(f.world, entity)->radius = 90;
    GAME_GamePointLight_get(f.world, entity)->color = 0x00ff00ffu;
    draw(&f, NULL, 0, moved);
    bright = at(&f, moved, (CF_V3){ 0, 0, 50 });
    assert(
        bright.colors.g > dark.colors.g + 30 &&
        abs(bright.colors.r - dark.colors.r) <= 1
    );
    GAME_GamePointLight_get(f.world, entity)->intensity = 0;
    draw(&f, NULL, 0, moved);
    assert(!memcmp(baseline, moved, sizeof moved));
    GAME_GamePointLight_get(f.world, entity)->intensity = NAN;
    draw(&f, NULL, 0, moved);
    assert(!memcmp(baseline, moved, sizeof moved));

    pin_value unlit = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(a->graph, surface(a), 0, 6, &unlit) == 0);
    publish(a);
    draw(&f, NULL, 0, baseline);
    GAME_GamePointLight_get(f.world, entity)->intensity = 6;
    draw(&f, NULL, 0, moved);
    assert(!memcmp(baseline, moved, sizeof moved));
    fixture_free(&f);
    behavior_asset_free(a);

    f = fixture_new();
    a = material("Point light budget surface", false);
    vector(a->graph, surface(a), 0, 0, (CF_V3){ .2f, .2f, .2f });
    scalar(a->graph, surface(a), 0, 2, .8f);
    publish(a);
    actor(&f, a->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 100, "");
    for (int i = 0; i < 32; i++) {
        point_light(&f, (CF_V3){ 0, 0, 100 }, 0x0000ffffu, .01f, 90);
    }
    draw(&f, NULL, 0, baseline);
    point_light(&f, (CF_V3){ 0, 0, 100 }, 0xff0000ffu, 6, 90);
    draw(&f, NULL, 0, lit);
    draw(&f, NULL, 0, moved);
    assert(!memcmp(lit, moved, sizeof moved));
    dark = at(&f, baseline, (CF_V3){ 0, 0, 50 });
    bright = at(&f, lit, (CF_V3){ 0, 0, 50 });
    assert(bright.colors.r > dark.colors.r + 30);
    assert(bright.colors.b <= dark.colors.b);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void surface_activity()
{
    fixture f = fixture_new();
    behavior_asset *a = material("Authored panel activity", true);
    vector(a->graph, surface(a), 0, 0, (CF_V3){ 0 });
    node *influence = add(a->graph, "Shader Surface Influence");
    vector(a->graph, influence, 0, 0, (CF_V3){ 0 });
    assert(graph_connect_data(a->graph, influence, 0, surface(a), 1) == 0);
    publish(a);
    actor(&f, a->name, (CF_V3){ 0 }, GAME_PRIMITIVE_BOX, 100, "");
    ecs_entity source = influence_source(f.world, 0, 0xff0000ffu, 2, .2f);
    CF_Pixel active[PIXELS], faded[PIXELS], repeated[PIXELS];
    draw(&f, NULL, 0, active);
    CF_Pixel left = at(&f, active, (CF_V3){ -25, 0, 50 });
    CF_Pixel right = at(&f, active, (CF_V3){ 25, 0, 50 });
    assert(abs(left.r - display_channel(2)) <= 2 && left.g < 2 && left.b < 2);
    assert(left.val == right.val);
    point_light(&f, (CF_V3){ 0, 0, 100 }, 0x00ff00ffu, 20, 200);
    draw(&f, NULL, 0, repeated);
    assert(!memcmp(active, repeated, sizeof active));

    GAME_GameTransform_get(f.world, source)->x = 60;
    draw(&f, NULL, .1f, faded);
    left = at(&f, faded, (CF_V3){ -25, 0, 50 });
    assert(abs(left.r - display_channel(.5f)) <= 2 && left.g < 2 && left.b < 2);
    draw(&f, NULL, .1f, repeated);
    assert(!memcmp(faded, repeated, sizeof faded));
    draw(&f, NULL, .31f, faded);
    left = at(&f, faded, (CF_V3){ -25, 0, 50 });
    assert(left.r == 0 && left.g == 0 && left.b == 0);
    GAME_GameTransform_get(f.world, source)->x = 0;
    draw(&f, NULL, .4f, active);
    assert(ecs_world_destroy_entity(f.world, source) == ECS_OK);
    render_arena_reset(f.renderer);
    draw(&f, NULL, .4f, faded);
    left = at(&f, faded, (CF_V3){ -25, 0, 50 });
    assert(left.r == 0 && left.g == 0 && left.b == 0);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void rendered_trail()
{
    fixture f = fixture_new();
    f.camera = render_arena_camera_make(0, 0, 160, 160);
    behavior_asset *a = material("Trail source", true);
    publish(a);
    ecs_entity entity = actor(&f, a->name, (CF_V3){ -30, 10, -20 }, GAME_PRIMITIVE_TRIANGLE_PRISM, 5, "");
    GameTrail trail = { .color = 0x40ffffffu, .width = 5, .lifetime = .4f, .emission = 2 };
    assert(ecs_world_record_add(f.world, entity, GAME_COMPONENT_GameTrail.id, &trail, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    ecs_entity second = actor(&f, a->name, (CF_V3){ -60, 10, -30 }, GAME_PRIMITIVE_TRIANGLE_PRISM, 5, "");
    GameTrail red = trail;
    red.color = 0xff0000ffu;
    assert(ecs_world_record_add(f.world, second, GAME_COMPONENT_GameTrail.id, &red, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    CF_Pixel pixels[PIXELS], paused[PIXELS];
    draw(&f, NULL, 0, pixels);
    *GAME_GameTransform_get(f.world, entity) = (GameTransform){ 0, 20 };
    GAME_GameTransform_get(f.world, second)->y = 0;
    draw(&f, NULL, .1f, pixels);
    *GAME_GameTransform_get(f.world, entity) = (GameTransform){ 30, -20 };
    GAME_GameTransform_get(f.world, second)->y = 30;
    draw(&f, NULL, .2f, pixels);
    assert(ecs_world_destroy_entity(f.world, entity) == ECS_OK);
    assert(ecs_world_destroy_entity(f.world, second) == ECS_OK);
    draw(&f, NULL, .25f, pixels);
    assert(f.stats.commands == 2 && f.stats.instances == 2);
    CF_Pixel red_path = at(&f, pixels, (CF_V3){ -60, 10, 0 });
    assert(red_path.r > 50 && red_path.g == 0 && red_path.b == 0);
    CF_Pixel turn = at(&f, pixels, (CF_V3){ 0, 10, 20 });
    CF_Pixel off_path = at(&f, pixels, (CF_V3){ 0, 10, -20 });
    assert(turn.g > 50 && turn.b > 50 && off_path.r == 0 && off_path.g == 0 && off_path.b == 0);
    draw(&f, NULL, .25f, paused);
    assert(!memcmp(pixels, paused, sizeof pixels));
    behavior_asset *blocker_material = material("Trail blocker", true);
    vector(blocker_material->graph, surface(blocker_material), 0, 0, (CF_V3){ 1, 0, 0 });
    publish(blocker_material);
    CF_M4x4 inverse = cf_m4_invert(f.camera.view);
    CF_V3 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14] };
    CF_V3 point = { 0, 10, 20 };
    CF_V3 front = cf_add_v3(point, cf_mul_v3_f(cf_norm_v3(cf_sub_v3(eye, point)), 25));
    ecs_entity blocker = actor(&f, blocker_material->name, front, GAME_PRIMITIVE_BOX, 18, "");
    draw(&f, NULL, .25f, pixels);
    CF_Pixel occluded = at(&f, pixels, point);
    assert(occluded.r > 150 && occluded.g < 2 && occluded.b < 2);
    assert(ecs_world_destroy_entity(f.world, blocker) == ECS_OK);
    draw(&f, NULL, .25f, pixels);
    assert(!memcmp(pixels, paused, sizeof pixels));
    behavior_asset_free(blocker_material);
    draw(&f, NULL, .61f, pixels);
    turn = at(&f, pixels, (CF_V3){ 0, 10, 20 });
    assert(turn.r == 0 && turn.g == 0 && turn.b == 0);
    render_arena_reset(f.renderer);
    draw(&f, NULL, .61f, pixels);
    assert(f.stats.commands == 0);
    fixture_free(&f);
    behavior_asset_free(a);
}

static void trail_particle_crossing()
{
    fixture f = fixture_new();
    f.camera = render_arena_camera_make(0, 0, 160, 160);
    CF_M4x4 inverse = cf_m4_invert(f.camera.view);
    CF_V3 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14] };
    CF_V3 crossing = { 0, 5, 0 };
    CF_V3 above = cf_add_v3(eye, cf_mul_v3_f(cf_sub_v3(crossing, eye), (12 - eye.y) / (5 - eye.y)));
    game_particle_spray spray = { .position = { above.x, above.z },
                                  .antialias = 1.5f,
                                  .elevation = 12,
                                  .direction = { 1, 0 },
                                  .count = 1,
                                  .color = 0xff2020ffu,
                                  .end_color = 0xff2020ffu,
                                  .lifetime_min = 1,
                                  .lifetime_max = 1,
                                  .size_min = 20,
                                  .size_max = 20,
                                  .shape = GAME_PARTICLE_TRIANGLE };
    assert(game_particles_emit_directed(f.particles, &spray) == 1);
    CF_Pixel pixels[PIXELS];
    draw(&f, NULL, 0, pixels);
    CF_Pixel unobstructed = at(&f, pixels, crossing);
    assert(unobstructed.colors.r > 100);
    behavior_asset *a = material("Crossing trail source", true);
    publish(a);
    ecs_entity sources[2];
    for (int i = 0; i < 2; i++) {
        sources[i] = actor(&f, a->name, (CF_V3){ -20, 5, i * 160 }, GAME_PRIMITIVE_TRIANGLE, 2, "");
        GameTrail trail = { .color = 0x00ffffffu, .width = 10, .lifetime = 1, .emission = .5f };
        assert(
            ecs_world_record_add(f.world, sources[i], GAME_COMPONENT_GameTrail.id, &trail, 0) == ECS_OK
        );
    }
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    draw(&f, NULL, 0, pixels);
    for (int i = 0; i < 2; i++) {
        GAME_GameTransform_get(f.world, sources[i])->x = 20;
    }
    draw(&f, NULL, .1f, pixels);
    for (int i = 0; i < 2; i++) {
        assert(ecs_world_destroy_entity(f.world, sources[i]) == ECS_OK);
    }
    draw(&f, NULL, .1f, pixels);
    assert(f.stats.instances == 3 && f.stats.commands == 3);
    CF_Pixel result = at(&f, pixels, crossing);
    /* A distant trail must not pull the local elevation-5 ribbon in front of
     * the opaque core of a real transparent particle at elevation 12. */
    assert(
        abs(result.colors.r - unobstructed.colors.r) <= 1 &&
        abs(result.colors.g - unobstructed.colors.g) <= 1 &&
        abs(result.colors.b - unobstructed.colors.b) <= 1
    );
    fixture_free(&f);
    behavior_asset_free(a);
}

static void particle_appearance()
{
    fixture f = fixture_new();
    game_particle_spray spray = { .direction = { 1, 0 },
                                  .antialias = 1.5f,
                                  .count = 1,
                                  .color = 0xff0000ffu,
                                  .end_color = 0x0000ffffu,
                                  .lifetime_min = 1,
                                  .lifetime_max = 1,
                                  .size_min = 30,
                                  .size_max = 30,
                                  .size_exponent = 2,
                                  .emission_min = 1,
                                  .emission_max = 1 };
    assert(game_particles_emit_directed(f.particles, &spray) == 1);
    assert(game_particles_tick(f.particles, .5) == 0);
    CF_Pixel pixels[PIXELS];
    draw(&f, NULL, 0, pixels);
    CF_Pixel center = at(&f, pixels, (CF_V3){ 0 });
    CF_Pixel edge = at(&f, pixels, (CF_V3){ -14.6f, 4, 0 });
    assert(edge.colors.r > edge.colors.g + 30 && edge.colors.b > edge.colors.g + 30);
    assert(center.colors.g > edge.colors.g + 30);
    CF_Pixel outside = at(&f, pixels, (CF_V3){ 24, 0, 0 });
    assert(outside.colors.r == 0 && outside.colors.g == 0 && outside.colors.b == 0);

    /* Rotating the camera retains the same flat footprint and color bands. */
    CF_Pixel turned[PIXELS];
    f.camera.view = cf_look_at((CF_V3){ 400, 0, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });
    draw(&f, NULL, 0, turned);
    assert(difference(pixels, turned) < 2);
    f.camera.view = cf_look_at((CF_V3){ 0, 0, 400 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });

    game_particles_clear(f.particles);
    spray.shape = GAME_PARTICLE_RING;
    assert(game_particles_emit_directed(f.particles, &spray) == 1);
    assert(game_particles_tick(f.particles, .5) == 0);
    draw(&f, NULL, 0, pixels);
    center = at(&f, pixels, (CF_V3){ 0 });
    edge = at(&f, pixels, (CF_V3){ 15.7f, 0, 0 });
    assert(center.colors.r == 0 && center.colors.g == 0 && center.colors.b == 0);
    assert(edge.colors.r > 30 && edge.colors.b > 30);

    game_particles_clear(f.particles);
    spray.shape = GAME_PARTICLE_ROUND;
    spray.color = 0xff000000u;
    spray.end_color = 0x0000ff00u;
    assert(game_particles_emit_directed(f.particles, &spray) == 1);
    assert(game_particles_tick(f.particles, .5) == 0);
    draw(&f, NULL, 0, pixels);
    center = at(&f, pixels, (CF_V3){ 0 });
    assert(center.colors.r == 0 && center.colors.g == 0 && center.colors.b == 0);
    fixture_free(&f);
}

int test_surface_gpu()
{
    visual_errors();
    context_pixels();
    instance_checks();
    mixed_mesh_instances();
    boolean_checks();
    material_checks();
    opacity_checks();
    image_surfaces();
    point_lighting();
    surface_activity();
    rendered_trail();
    trail_particle_crossing();
    particle_appearance();
    render_shader_catalog_clear();
    puts("surface GPU PASS: sample contexts, material inputs, instance batching, transparency, images, bounded point lighting, particle appearance, preview parity and last-valid diagnostics");
    return 0;
}
