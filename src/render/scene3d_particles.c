#include "render/scene3d_particles.h"

#include "render/fullscreen.h"
#include "render/scene3d_geometry.h"
#include "render/scene3d_visual.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "render/shader_text_backend.h"
#include "render/shader_text_create.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/rhythm.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int asset;
    CF_Shader shader, update;
} particle_program;

typedef struct
{
    ecs_entity entity;
    CF_Mesh mesh;
    int primitive, capacity, count, program;
    CF_M4x4 frame, normal_frame;
    CF_V4 dimensions, clock, rhythm, step;
    CF_Canvas state[2];
    int state_count, state_seed, state_latest, asset;
    float state_time;
    bool state_valid;
    float parameters[32][4];
    bool additive, active;
} particle_field;

struct scene3d_particles
{
    const ecs_world *world;
    unsigned revision;
    particle_program *programs;
    particle_field *fields;
    render_shader_parameter_cache *parameters;
    CF_Mesh update_mesh;
    CF_Material update_material;
    int count, draws;
};

scene3d_particles *render_scene3d_particles_new()
{
    return xcalloc(1, sizeof(scene3d_particles));
}

static void clear_programs(scene3d_particles *particles)
{
    for (int i = 0; i < acount(particles->programs); i++) {
        cf_destroy_shader(particles->programs[i].shader);
        if (particles->programs[i].update.id) {
            cf_destroy_shader(particles->programs[i].update);
        }
    }
    aclear(particles->programs);
}

static void clear_state(particle_field *field)
{
    for (int i = 0; i < 2; i++) {
        if (field->state[i].id) {
            cf_destroy_canvas(field->state[i]);
            field->state[i] = (CF_Canvas){ 0 };
        }
    }
    field->state_valid = false;
    field->state_count = 0;
    field->step = (CF_V4){ 0 };
}

void render_scene3d_particles_reset(scene3d_particles *particles)
{
    for (int i = 0; i < acount(particles->fields); i++) {
        clear_state(&particles->fields[i]);
        if (particles->fields[i].mesh.id) {
            cf_destroy_mesh(particles->fields[i].mesh);
        }
    }
    aclear(particles->fields);
    particles->world = NULL;
    particles->count = particles->draws = 0;
}

void render_scene3d_particles_free(scene3d_particles *particles)
{
    render_scene3d_particles_reset(particles);
    clear_programs(particles);
    if (particles->update_mesh.id) {
        cf_destroy_mesh(particles->update_mesh);
        cf_destroy_material(particles->update_material);
    }
    render_shader_parameter_cache_free(particles->parameters);
    afree(particles->fields);
    afree(particles->programs);
    free(particles);
}

static int program_for(scene3d_particles *particles, int asset, char *error, size_t size)
{
    for (int i = 0; i < acount(particles->programs); i++) {
        if (particles->programs[i].asset == asset) {
            return i;
        }
    }
    shader_error diagnostic = { 0 };
    char *graph = render_shader_particle_glsl(asset, &diagnostic);
    const shader_text *text = render_shader_particle_text(asset);
    char *vertex = graph
                     ? render_shader_particle_vertex_source(graph, text && text->update)
                     : NULL;
    char *fragment = render_shader_source("scene3d_particles.frag");
    CF_Shader shader = { 0 }, update = { 0 };
    if (vertex && fragment) {
        shader = text ? render_shader_text_create(text, vertex, fragment, &diagnostic)
                      : cf_make_shader_from_source(vertex, fragment);
    }
    if (shader.id && text && text->update) {
        char *adapter = render_shader_particle_update_glsl(asset, &diagnostic);
        char *source = adapter ? render_shader_particle_update_source(adapter) : NULL;
        if (source) {
            update = render_shader_text_create(
                text,
                render_shader_particle_update_vertex(),
                source,
                &diagnostic
            );
        }
        free(source);
        free(adapter);
        if (!update.id) {
            cf_destroy_shader(shader);
            shader = (CF_Shader){ 0 };
        }
    }
    if (!shader.id) {
        snprintf(
            error,
            size,
            "Particle field shader: %s",
            diagnostic.message[0] ? diagnostic.message
            : vertex && fragment  ? cf_shader_compile_error()
                                  : "cannot read shader sources"
        );
    }
    free(graph);
    free(vertex);
    free(fragment);
    if (!shader.id) {
        return -1;
    }
    apush(particles->programs, ((particle_program){ asset, shader, update }));
    return acount(particles->programs) - 1;
}

static particle_field *field_for(scene3d_particles *particles, ecs_entity entity)
{
    for (int i = 0; i < acount(particles->fields); i++) {
        if (ecs_entity_to_u64(particles->fields[i].entity) == ecs_entity_to_u64(entity)) {
            return &particles->fields[i];
        }
    }
    if (acount(particles->fields) == SCENE3D_PARTICLE_FIELD_SOURCES) {
        return NULL;
    }
    apush(particles->fields, ((particle_field){ .entity = entity, .primitive = -1 }));
    return &particles->fields[acount(particles->fields) - 1];
}

/* Draw3d's explicit instance-buffer path retains the instance count. Metal
 * requires an attribute for that buffer, so it holds a zero position offset.
 * Upload only when count changes; there is no per-frame particle CPU work. */
static void prepare_mesh(particle_field *field, int primitive, int count)
{
    if (field->primitive != primitive) {
        if (field->mesh.id) {
            cf_destroy_mesh(field->mesh);
        }
        field->mesh = primitive == PRIMITIVE_TETRAHEDRON
                        ? render_scene3d_tetrahedron_mesh()
                        : render_scene3d_octahedron_mesh();
        CF_VertexAttribute offset = { .name = "in_instance_offset",
                                      .format = CF_VERTEX_FORMAT_FLOAT,
                                      .per_instance = true };
        cf_mesh_append_attributes(field->mesh, &offset, 1);
        field->primitive = primitive;
        field->capacity = field->count = 0;
    }
    if (count == field->count) {
        return;
    }
    if (!field->capacity) {
        cf_mesh_set_instance_buffer(field->mesh, count * (int)sizeof(float), sizeof(float));
    }
    float *instances = xcalloc((size_t)count, sizeof *instances);
    cf_mesh_update_instance_data(field->mesh, instances, count);
    free(instances);
    if (count > field->capacity) {
        field->capacity = count;
    }
    field->count = count;
}

enum
{
    PARTICLE_STATE_WIDTH = 1024
};
static const float particle_step_limit = .05f;

static bool prepare_state(particle_field *field, char *error, size_t size)
{
    if (field->state_count == field->count) {
        return true;
    }
    clear_state(field);
    CF_PixelFormat format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    if (!cf_query_pixel_format(format, CF_PIXELFORMAT_OP_RENDER_TARGET) ||
        !cf_query_pixel_format(format, CF_PIXELFORMAT_OP_NEAREST_FILTER)) {
        snprintf(error, size, "Stateful particles need sampled RGBA32F render targets.");
        return false;
    }
    int width = field->count < PARTICLE_STATE_WIDTH ? field->count : PARTICLE_STATE_WIDTH;
    int height = (field->count + width - 1) / width;
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target_count = 2;
    for (int i = 0; i < 2; i++) {
        params.targets[i].pixel_format = format;
        params.targets[i].filter = CF_FILTER_NEAREST;
        params.targets[i].wrap_u = params.targets[i].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    }
    for (int i = 0; i < 2; i++) {
        field->state[i] = cf_make_canvas(params);
        if (!field->state[i].id) {
            clear_state(field);
            snprintf(error, size, "Cannot allocate particle state targets.");
            return false;
        }
    }
    field->state_count = field->count;
    field->state_latest = 0;
    return true;
}

static bool update_state(scene3d_particles *particles, particle_field *field, int seed, char *error, size_t size)
{
    CF_Shader shader = particles->programs[field->program].update;
    if (!shader.id) {
        clear_state(field);
        return true;
    }
    if (!prepare_state(field, error, size)) {
        return false;
    }
    float time = field->clock.x;
    bool reset = !field->state_valid || seed != field->state_seed ||
                 time < field->state_time;
    if (!reset && time == field->state_time) {
        return true;
    }
    if (!particles->update_mesh.id) {
        particles->update_mesh = render_make_pass_mesh();
        particles->update_material = cf_make_material();
        CF_RenderState state = cf_render_state_defaults();
        state.blend.enabled = false;
        cf_material_set_render_state(particles->update_material, state);
    }
    int width, height;
    cf_canvas_get_size(field->state[0], &width, &height);
    field->step = (CF_V4){ reset ? 0 : fminf(time - field->state_time, particle_step_limit),
                           reset ? 1 : 0,
                           (float)width,
                           0 };
    CF_Canvas previous = field->state[field->state_latest];
    CF_Canvas target = field->state[field->state_latest ^ 1];
    CF_Material material = particles->update_material;
    cf_material_set_texture_fs(material, "particle_positions", cf_canvas_get_target(previous));
    cf_material_set_texture_fs(material, "particle_velocities", cf_canvas_get_target2(previous, 1));
    cf_material_set_uniform_fs(material, "particle_clock", &field->clock, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "particle_step", &field->step, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "particle_rhythm", &field->rhythm, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "particle_parameters", field->parameters, CF_UNIFORM_TYPE_FLOAT4, 32);
    cf_apply_canvas(target, false);
    cf_apply_mesh(particles->update_mesh);
    cf_apply_shader(shader, material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
    field->state_latest ^= 1;
    field->state_seed = seed;
    field->state_time = time;
    field->state_valid = true;
    return true;
}

static bool prepare_field(
    scene3d_particles *particles,
    ecs_world *world,
    ecs_entity entity,
    const engine *runtime,
    char *error,
    size_t size
)
{
    const ParticleField3D *style = ParticleField3D_get_const(world, entity);
    if (!style) {
        return true;
    }
    const Geometry3D *shape = Geometry3D_get_const(world, entity);
    assert(shape); /* The scene's geometry query supplies these entities. */
    const Shader *binding = Shader_get_const(world, entity);
    if (style->count < 0 || style->count > SCENE3D_PARTICLE_FIELD_LIMIT ||
        !isfinite(style->time) || style->time < 0 ||
        (shape->primitive != PRIMITIVE_TETRAHEDRON &&
         shape->primitive != PRIMITIVE_OCTAHEDRON) ||
        !isfinite(shape->width) || !isfinite(shape->height) || !isfinite(shape->depth) ||
        shape->width <= 0 || shape->height <= 0 || shape->depth <= 0) {
        snprintf(error, size, "Particle field needs a bounded count, finite clock and positive tetrahedron or octahedron dimensions.");
        return false;
    }
    int asset = binding ? render_shader_find(binding->material) : 0;
    if (!render_shader_is_particle(asset)) {
        snprintf(error, size, "Particle field Shader.material must name a particle shader-library function.");
        return false;
    }
    particle_field *field = field_for(particles, entity);
    if (!field || style->count > SCENE3D_PARTICLE_FIELD_LIMIT - particles->count) {
        snprintf(
            error,
            size,
            "Particle fields exceed the %d-source or %d-particle scene budget.",
            SCENE3D_PARTICLE_FIELD_SOURCES,
            SCENE3D_PARTICLE_FIELD_LIMIT
        );
        return false;
    }
    if (!style->count) {
        return true;
    }
    bool spatial = Transform3D_has(world, entity);
    Transform position = { 0 };
    GlobalTransform3D transform = { 0 };
    if (spatial) {
        transform = engine_render_transform_3d(runtime, world, entity);
        if (!engine_entity_visible_3d(runtime, world, entity, transform.position)) {
            return true;
        }
    } else {
        position = engine_render_transform(runtime, world, entity).position;
        if (!engine_entity_visible(runtime, world, entity)) {
            return true;
        }
    }
    CF_M4x4 frame;
    scene3d_frame_state state = render_scene3d_visual_frame(
        world,
        entity,
        &position,
        spatial ? &transform : NULL,
        &frame
    );
    if (state == SCENE3D_FRAME_FLAT) {
        return true;
    }
    if (state != SCENE3D_FRAME_WHOLE) {
        snprintf(error, size, "Particle field transform is invalid.");
        return false;
    }
    int program = program_for(particles, asset, error, size);
    if (program < 0) {
        return false;
    }
    prepare_mesh(field, shape->primitive, style->count);
    field->active = true;
    if (field->asset != asset) {
        field->state_valid = false;
        field->asset = asset;
    }
    field->program = program;
    field->frame = frame;
    CF_M4x4 inverse = cf_m4_invert(frame);
    field->normal_frame = (CF_M4x4){ 0 };
    for (int column = 0; column < 3; column++) {
        for (int row = 0; row < 3; row++) {
            field->normal_frame.elements[column * 4 + row] = inverse.elements[row * 4 + column];
        }
    }
    field->dimensions = (CF_V4){ shape->width, shape->height, shape->depth, 0 };
    field->clock = (CF_V4){ style->time,
                            (float)style->count,
                            (float)style->seed,
                            style->additive ? 1 : 0 };
    field->additive = style->additive;
    beat beat = { 0 };
    rhythm_sample(world, style->time, &beat);
    field->rhythm = (CF_V4){ (float)beat.phase, (float)beat.pulse, 0, 0 };
    shader_error diagnostic = { 0 };
    if (!render_shader_material_parameters_scoped_cached(
            asset,
            world,
            entity,
            binding->parameters,
            "",
            field->parameters,
            &particles->parameters,
            &diagnostic
        )) {
        snprintf(error, size, "%s", diagnostic.message);
        return false;
    }
    if (!update_state(particles, field, style->seed, error, size)) {
        return false;
    }
    particles->count += field->count;
    particles->draws++;
    return true;
}

bool render_scene3d_particles_prepare(
    scene3d_particles *particles,
    ecs_world *world,
    const ecs_entity *entities,
    int count,
    const engine *runtime,
    char *error,
    size_t size
)
{
    if (particles->world != world) {
        render_scene3d_particles_reset(particles);
        particles->world = world;
    }
    if (particles->revision != render_shader_revision()) {
        clear_programs(particles);
        particles->revision = render_shader_revision();
        for (int i = 0; i < acount(particles->fields); i++) {
            particles->fields[i].state_valid = false;
        }
    }
    particles->count = particles->draws = 0;
    for (int i = acount(particles->fields) - 1; i >= 0; i--) {
        particle_field *field = &particles->fields[i];
        field->active = false;
        if (!ecs_entity_is_alive(world, field->entity) ||
            !ParticleField3D_has(world, field->entity) ||
            !Geometry3D_has(world, field->entity)) {
            if (field->mesh.id) {
                cf_destroy_mesh(field->mesh);
            }
            clear_state(field);
            adel(particles->fields, i);
        }
    }
    for (int i = 0; i < count; i++) {
        if (!prepare_field(particles, world, entities[i], runtime, error, size)) {
            return false;
        }
    }
    for (int i = 0; i < acount(particles->fields); i++) {
        if (!particles->fields[i].active) {
            clear_state(&particles->fields[i]);
        }
    }
    return true;
}

void render_scene3d_particles_draw(const scene3d_particles *particles, const render_scene3d_camera *camera)
{
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    for (int i = 0; i < acount(particles->fields); i++) {
        const particle_field *field = &particles->fields[i];
        if (!field->active) {
            continue;
        }
        CF_RenderState state = cf_render_state_3d_defaults();
        state.cull_mode = render_scene3d_frame_mirrors(field->frame)
                            ? CF_CULL_MODE_FRONT
                            : CF_CULL_MODE_BACK;
        state.depth_write_enabled = !field->additive;
        state.blend.enabled = field->additive;
        state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
        state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE;
        state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ZERO;
        state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE;
        cf_draw3d_push_render_state(state);
        cf_draw3d_push_shader(particles->programs[field->program].shader);
        cf_draw3d_set_uniform("particle_frame", (void *)&field->frame, CF_UNIFORM_TYPE_MAT4, 1);
        cf_draw3d_set_uniform("particle_normal_frame", (void *)&field->normal_frame, CF_UNIFORM_TYPE_MAT4, 1);
        cf_draw3d_set_uniform("particle_dimensions", (void *)&field->dimensions, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw3d_set_uniform("particle_clock", (void *)&field->clock, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw3d_set_uniform("particle_parameters", (void *)field->parameters, CF_UNIFORM_TYPE_FLOAT4, 32);
        cf_draw3d_set_uniform("particle_rhythm", (void *)&field->rhythm, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw3d_set_uniform("particle_step", (void *)&field->step, CF_UNIFORM_TYPE_FLOAT4, 1);
        if (field->state_valid) {
            CF_Canvas state = field->state[field->state_latest];
            cf_draw3d_set_texture("particle_positions", cf_canvas_get_target(state));
            cf_draw3d_set_texture("particle_velocities", cf_canvas_get_target2(state, 1));
        }
        cf_draw3d_push();
        cf_draw3d_transform(field->frame);
        cf_draw3d_mesh(field->mesh);
        cf_draw3d_pop();
        cf_draw3d_pop_shader();
        cf_draw3d_pop_render_state();
        if (field->state_valid) {
            cf_draw3d_set_texture("particle_positions", (CF_Texture){ 0 });
            cf_draw3d_set_texture("particle_velocities", (CF_Texture){ 0 });
        }
    }
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
}

int render_scene3d_particles_count(const scene3d_particles *particles)
{
    return particles->count;
}

int render_scene3d_particles_draws(const scene3d_particles *particles)
{
    return particles->draws;
}
