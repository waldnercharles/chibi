#include "render/mesh_material.h"

#include "render/material.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/shader_program.h"
#include "render/spatial_groups.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int primitive;
    uint64_t asset_identity;
    uint64_t dynamic_identity, dynamic_revision;
    int first_index;
    CF_Mesh mesh;
} primitive_mesh;
typedef struct
{
    float model[3][4], inverse[3][4], dimensions[4], colors[4][4], surface[4],
        center[4];
} sdf_instance;
_Static_assert(sizeof(sdf_instance) == 208, "SDF instance stride");
typedef struct
{
    int shape, material, preview_part;
    bool preview;
    float preview_plane[4], preview_options[4], preview_uv_bounds[2][4];
    float materials[MODEL_MATERIAL_SLOT_MAX + 1][4];
} sdf_batch_key;
typedef struct
{
    float shape[32][4], material[32][4];
} sdf_parameters;
_Static_assert(sizeof(sdf_parameters) == 1024, "SDF parameter table stride");
typedef struct
{
    sdf_batch_key key;
    int offset, program;
    render_programs *programs;
} sdf_staged;
typedef struct
{
    sdf_batch_key key;
    int program;
} sdf_prepared_program;
typedef struct
{
    render_mesh_material_actor actor;
    const render_mesh_surface *surface;
    float near, far;
    int index;
} depth_actor;
/* Covers D16 quantization and float reconstruction. Hardware depth remains final. */
#define DEPTH_MARGIN (4.0f / 65535.0f)
struct render_mesh_material
{
    render_programs *programs, *sdf_programs, *preview_programs;
    render_model_inspection inspection;
    render_shader_parameter_cache *cache;
    CF_Material material;
    CF_Canvas canvas, depth_snapshot;
    primitive_mesh *meshes;
    CF_Mesh sdf_mesh;
    sdf_instance *instances;
    render_mesh_surface *owned_surfaces;
    const render_mesh_surface **surfaces;
    sdf_staged *staged;
    sdf_prepared_program *prepared_programs;
    depth_actor *depth_order;
    render_mesh_material_actor *ordered_actors;
    render_pixel_rect *actor_rects;
    int *pair_slots, *parameter_slots;
    sdf_parameters *parameter_rows;
    uint64_t *parameter_hashes;
    CF_StorageBuffer parameter_buffer;
    int parameter_capacity;
    SurfaceQuality quality;
    int sdf_trace_debug;
    bool hidden_rejection, has_depth;
    uint64_t instance_buffer;
    int sdf_batch_limit;
    render_mesh_material_work work;
    int width, height, scratch_width, scratch_height;
    int active_width, active_height;
    float rect[4];
    char error[256];
};
render_mesh_material *render_mesh_material_new()
{
    render_mesh_material *pass = xcalloc(1, sizeof *pass);
    pass->hidden_rejection = true;
    pass->programs = render_programs_new(RENDER_PROGRAM_MESH_MATERIAL);
    pass->material = cf_make_material();
    if (!pass->material.id) {
        render_mesh_material_free(pass);
        return NULL;
    }
    CF_RenderState state = cf_render_state_defaults();
    state.depth_write_enabled = true;
    state.depth_compare = CF_COMPARE_FUNCTION_LESS_THAN;
    cf_material_set_render_state(pass->material, state);
    return pass;
}
void render_mesh_material_set_model_inspection(render_mesh_material *pass, const render_model_inspection *inspection)
{
    pass->inspection = *inspection;
}
static bool inspected(const render_mesh_material *pass, ecs_entity entity)
{
    return ecs_entity_to_u64(pass->inspection.entity) != 0 &&
           ecs_entity_to_u64(entity) == ecs_entity_to_u64(pass->inspection.entity) &&
           (pass->inspection.part >= 0 || pass->inspection.section);
}
void render_mesh_material_free(render_mesh_material *pass)
{
    if (!pass) {
        return;
    }
    for (int i = 0; i < acount(pass->meshes); i++) {
        cf_destroy_mesh(pass->meshes[i].mesh);
    }
    afree(pass->meshes);
    afree(pass->instances);
    for (int i = 0; i < acount(pass->owned_surfaces); i++) {
        render_mesh_surface_free(&pass->owned_surfaces[i]);
    }
    afree(pass->owned_surfaces);
    afree(pass->surfaces);
    afree(pass->staged);
    afree(pass->prepared_programs);
    afree(pass->depth_order);
    afree(pass->ordered_actors);
    afree(pass->actor_rects);
    afree(pass->pair_slots);
    afree(pass->parameter_slots);
    afree(pass->parameter_rows);
    afree(pass->parameter_hashes);
    if (pass->parameter_buffer.id) {
        cf_destroy_storage_buffer(pass->parameter_buffer);
    }
    if (pass->instance_buffer) {
        cf_destroy_instance_buffer(pass->instance_buffer);
    }
    if (pass->sdf_mesh.id) {
        cf_destroy_mesh(pass->sdf_mesh);
    }
    if (pass->canvas.id) {
        cf_destroy_canvas(pass->canvas);
    }
    if (pass->depth_snapshot.id) {
        cf_destroy_canvas(pass->depth_snapshot);
    }
    if (pass->material.id) {
        cf_destroy_material(pass->material);
    }
    render_programs_free(pass->programs);
    render_programs_free(pass->sdf_programs);
    render_programs_free(pass->preview_programs);
    render_shader_parameter_cache_free(pass->cache);
    free(pass);
}
bool render_mesh_material_resize(render_mesh_material *pass, int width, int height)
{
    assert(pass);
    if (width < 1 || height < 1) {
        return false;
    }
    pass->width = width;
    pass->height = height;
    return true;
}
static bool scratch(render_mesh_material *pass, int width, int height)
{
    int samples = pass->quality.resolution == SURFACE_FULL ? 1 : 2;
    pass->active_width = width * samples;
    pass->active_height = height * samples;
    width = (pass->active_width + 63) / 64 * 64;
    height = (pass->active_height + 63) / 64 * 64;
    if (pass->canvas.id && pass->scratch_width >= width &&
        pass->scratch_height >= height) {
        return true;
    }
    width = width > pass->scratch_width ? width : pass->scratch_width;
    height = height > pass->scratch_height ? height : pass->scratch_height;
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target_count = 5;
    params.depth_stencil_enable = true;
    params.depth_stencil_target.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.depth_stencil_target.filter = CF_FILTER_NEAREST;
    params.depth_stencil_target.mip_filter = CF_MIP_FILTER_NEAREST;
    for (int i = 0; i < 5; i++) {
        params.targets[i].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.targets[i].filter = CF_FILTER_LINEAR;
        params.targets[i].wrap_u = params.targets[i].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    }
    CF_Canvas canvas = cf_make_canvas(params);
    if (!canvas.id) {
        snprintf(pass->error, sizeof pass->error, "Cannot allocate mesh material targets.");
        return false;
    }
    if (pass->canvas.id) {
        cf_destroy_canvas(pass->canvas);
    }
    pass->canvas = canvas;
    if (pass->depth_snapshot.id) {
        cf_destroy_canvas(pass->depth_snapshot);
        pass->depth_snapshot = (CF_Canvas){ 0 };
    }
    pass->scratch_width = width;
    pass->scratch_height = height;
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0 });
    cf_canvas_set_clear_depth_stencil(canvas, 1, 0);
    return true;
}
static bool depth_snapshot(render_mesh_material *pass)
{
    if (pass->depth_snapshot.id) {
        return true;
    }
    CF_CanvasParams params = cf_canvas_defaults(pass->scratch_width, pass->scratch_height);
    params.target.pixel_format = CF_PIXEL_FORMAT_R8_UNORM;
    params.depth_stencil_enable = true;
    params.depth_stencil_target.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.depth_stencil_target.filter = CF_FILTER_NEAREST;
    params.depth_stencil_target.mip_filter = CF_MIP_FILTER_NEAREST;
    pass->depth_snapshot = cf_make_canvas(params);
    if (!pass->depth_snapshot.id) {
        snprintf(pass->error, sizeof pass->error, "Cannot allocate mesh depth snapshot.");
        return false;
    }
    return true;
}
static bool overlaps(render_pixel_rect a, render_pixel_rect b)
{
    return a.x < b.right && b.x < a.right && a.y < b.bottom && b.y < a.bottom;
}
static render_pixel_rect joined(render_pixel_rect a, render_pixel_rect b)
{
    return (render_pixel_rect){ a.x < b.x ? a.x : b.x,
                                a.y < b.y ? a.y : b.y,
                                a.right > b.right ? a.right : b.right,
                                a.bottom > b.bottom ? a.bottom : b.bottom };
}
static int compare_depth(const void *a, const void *b)
{
    const depth_actor *x = a, *y = b;
    if (x->near != y->near) {
        return (x->near > y->near) - (x->near < y->near);
    }
    return (x->index > y->index) - (x->index < y->index);
}
static int compare_order(const void *a, const void *b)
{
    const depth_actor *x = a, *y = b;
    return (x->index > y->index) - (x->index < y->index);
}
static void camera_uniforms(CF_Material material, render_material_view view)
{
    CF_V3 basis[3], target = render_mesh_camera_target(view);
    render_mesh_camera_basis(view, basis);
    CF_V3 eye_point = render_mesh_camera_orbit(view)
                        ? cf_add_v3(target, cf_mul_v3_f(basis[1], view.focal_distance))
                        : (CF_V3){ view.center_x, view.focal_distance, -view.center_y };
    float camera[4][4] = { { basis[0].x, basis[0].y, basis[0].z, target.x },
                           { basis[1].x, basis[1].y, basis[1].z, target.y },
                           { basis[2].x, basis[2].y, basis[2].z, target.z },
                           { view.focal_distance, 0, 0, render_mesh_camera_orbit(view) } };
    float eye[] = { eye_point.x, eye_point.y, eye_point.z, view.focal_distance > 0 };
    cf_material_set_uniform_vs(material, "u_camera", camera, CF_UNIFORM_TYPE_FLOAT4, 4);
    cf_material_set_uniform_fs(material, "u_camera", camera, CF_UNIFORM_TYPE_FLOAT4, 4);
    cf_material_set_uniform_vs(material, "u_eye", eye, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "u_eye", eye, CF_UNIFORM_TYPE_FLOAT4, 1);
}

static float surface_depth(float y, const float depth[2], render_material_view view)
{
    if (view.focal_distance <= 0) {
        return (depth[1] - y) / (depth[1] - depth[0]);
    }
    float d = fmaxf(depth[0], view.focal_distance - y);
    return depth[1] / (depth[1] - depth[0]) * (d - depth[0]) / d;
}
static const render_mesh_material_actor *order_actors(
    render_mesh_material *pass,
    const render_mesh_material_actor *actors,
    int count,
    const float depth[2],
    render_material_view view
)
{
    if (!pass->hidden_rejection) {
        return actors;
    }
    asetlen(pass->depth_order, count);
    asetlen(pass->ordered_actors, count);
    for (int i = 0; i < count; i++) {
        const render_mesh_surface *surface = pass->surfaces[i];
        CF_V3 lo, hi;
        render_mesh_camera_bounds(view, surface->min, surface->max, &lo, &hi);
        pass->depth_order[i] = (depth_actor){ actors[i],
                                              surface,
                                              surface_depth(hi.y, depth, view),
                                              surface_depth(lo.y, depth, view),
                                              i };
    }
    qsort(pass->depth_order, count, sizeof *pass->depth_order, compare_depth);
    // Only reorder strictly separated depth intervals. Connected intervals
    // retain authored order, including surfaces that tie after quantization.
    for (int first = 0; first < count;) {
        float far = pass->depth_order[first].far;
        int end = first + 1;
        while (end < count && pass->depth_order[end].near <= far + DEPTH_MARGIN) {
            far = fmaxf(far, pass->depth_order[end++].far);
        }
        qsort(pass->depth_order + first, end - first, sizeof *pass->depth_order, compare_order);
        first = end;
    }
    for (int i = 0; i < count; i++) {
        pass->ordered_actors[i] = pass->depth_order[i].actor;
        pass->surfaces[i] = pass->depth_order[i].surface;
    }
    return pass->ordered_actors;
}
void render_mesh_material_forget(render_mesh_material *pass, uint64_t identity)
{
    for (int i = 0; i < acount(pass->meshes); i++) {
        if (pass->meshes[i].dynamic_identity == identity && identity) {
            cf_destroy_mesh(pass->meshes[i].mesh);
            adel(pass->meshes, i);
            return;
        }
    }
}

static CF_Mesh mesh(render_mesh_material *pass, const render_mesh_surface_part *part)
{
    for (int i = 0; i < acount(pass->meshes); i++) {
        if (part->dynamic_identity) {
            if (pass->meshes[i].dynamic_identity != part->dynamic_identity) {
                continue;
            }
            if (pass->meshes[i].dynamic_revision == part->dynamic_revision) {
                return pass->meshes[i].mesh;
            }
            break;
        }
        if (pass->meshes[i].dynamic_identity) {
            continue;
        }
        if (pass->meshes[i].primitive == part->visual.part.primitive &&
            pass->meshes[i].asset_identity == part->asset_identity &&
            pass->meshes[i].first_index == part->first_index) {
            return pass->meshes[i].mesh;
        }
    }
    CF_VertexAttribute attributes[] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, normal) },
        { .name = "in_uv",
          .format = CF_VERTEX_FORMAT_FLOAT2,
          .offset = offsetof(render_mesh_surface_vertex, uv) },
        { .name = "in_part_position",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, part_position) },
        { .name = "in_part_frame0",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, part_from_object[0]) },
        { .name = "in_part_frame1",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, part_from_object[1]) },
        { .name = "in_part_frame2",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, part_from_object[2]) }
    };
    int count = render_mesh_surface_corner_count(part);
    render_mesh_surface_vertex *corners = NULL;
    asetlen(corners, count);
    for (int i = 0; i < count; i++) {
        corners[i] = *render_mesh_surface_corner(part, i);
    }
    CF_Mesh result = cf_make_mesh(count * sizeof *corners, attributes, 7, sizeof *corners);
    if (result.id) {
        cf_mesh_update_vertex_data(result, corners, count);
        pass->work.mesh_uploads++;
        if (part->dynamic_identity) {
            render_mesh_material_forget(pass, part->dynamic_identity);
        }
        apush(
            pass->meshes,
            ((primitive_mesh){ .primitive = part->visual.part.primitive,
                               .asset_identity = part->asset_identity,
                               .dynamic_identity = part->dynamic_identity,
                               .dynamic_revision = part->dynamic_revision,
                               .first_index = part->first_index,
                               .mesh = result })
        );
    }
    afree(corners);
    return result;
}
static bool draw_actor(
    render_mesh_material *pass,
    ecs_world *world,
    ecs_entity entity,
    const render_mesh_surface *surface,
    const float rect[4],
    const float depth[2],
    render_material_view view,
    float time,
    bool *clear
)
{
    const Appearance *appearance = Appearance_get_const(world, entity);
    const Material *material = Material_get_const(world, entity);
    Material fallback = { .scene = appearance ? appearance->color : 0xffffffffu,
                          .albedo = appearance ? appearance->color : 0xffffffffu,
                          .edge_light = 1 };
    material_coefficients color;
    if (!material_extract(material ? material : &fallback, &color)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh material coefficients.");
        return false;
    }
    const Shader *binding = Shader_get_const(world, entity);
    render_programs *programs = pass->programs;
    bool ok = true;
    for (int i = 0; i < acount(surface->parts); i++) {
        render_mesh_surface_part *part = &surface->parts[i];
        const char *name = part->visual.part.material[0] ? part->visual.part.material
                         : binding ? binding->material
                                   : NULL;
        int id = render_shader_find(name);
        if (name && *name && (!id || render_shader_is_shape(id))) {
            snprintf(pass->error, sizeof pass->error, "Unknown mesh surface material: %s", name);
            ok = false;
            break;
        }
        render_shader_set set = { 0 };
        shader_error error = { 0 };
        if (!render_shader_set_add(&set, id, &error)) {
            snprintf(pass->error, sizeof pass->error, "%s", error.message);
            ok = false;
            break;
        }
        int program = render_programs_prepare(programs, &set);
        if (!program) {
            snprintf(pass->error, sizeof pass->error, "%s", render_programs_error(programs));
            ok = false;
            break;
        }
        CF_Mesh geometry = mesh(pass, part);
        if (!geometry.id) {
            snprintf(pass->error, sizeof pass->error, "Cannot allocate mesh vertices.");
            ok = false;
            break;
        }
        CF_M4x4 model = render_scene3d_visual_model(&part->visual);
        float rows[3][4];
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                rows[row][col] = model.elements[col * 4 + row];
            }
        }
        float coefficients[4][4];
        memcpy(coefficients[0], color.scene, 16);
        memcpy(coefficients[1], color.emission, 16);
        memcpy(coefficients[2], color.albedo, 16);
        memcpy(coefficients[3], color.absorption, 16);
        float parameters[32][4];
        if (!render_shader_material_parameters_scoped_cached(
                id,
                world,
                entity,
                binding ? binding->parameters : NULL,
                part->material_scope,
                parameters,
                &pass->cache,
                &error
            )) {
            snprintf(pass->error, sizeof pass->error, "%s", error.message);
            ok = false;
            break;
        }
        float properties[] = { id,
                               time,
                               material ? material->edge_light : 1,
                               part->part_coordinates ? 2
                               : part->asset_identity ? 1
                                                      : 0 };
        /* Low two bits select UV/part coordinates; bit 2 consumes Height. */
        if (part->height_consumed) {
            properties[3] += 4;
        }
        CF_V3 dimensions = part->visual.part.dimensions;
        float size[] = { dimensions.x,
                         dimensions.y,
                         dimensions.z,
                         part->visual.part.primitive };
        CF_M4x4 inverse = cf_m4_invert(model);
        float inverse_rows[3][4];
        for (int row = 0; row < 3; row++) {
            float factor = part->part_coordinates
                             ? 1
                             : (&part->visual.part.dimensions.x)[row];
            for (int col = 0; col < 4; col++) {
                inverse_rows[row][col] = inverse.elements[col * 4 + row] * factor;
            }
        }
        cf_material_set_uniform_fs(pass->material, "u_inverse", inverse_rows, CF_UNIFORM_TYPE_FLOAT4, 3);
        float uv_bounds[2][4] = {
            { part->uv_min.x, part->uv_min.y, part->uv_min.z, part->uv_bounds },
            { part->uv_max.x, part->uv_max.y, part->uv_max.z, 0 }
        };
        cf_material_set_uniform_fs(pass->material, "u_uv_bounds", uv_bounds, CF_UNIFORM_TYPE_FLOAT4, 2);
        float part_coordinates = part->part_coordinates ? 1 : 0;
        cf_material_set_uniform_vs(pass->material, "u_part_coordinates", &part_coordinates, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_vs(pass->material, "u_model", rows, CF_UNIFORM_TYPE_FLOAT4, 3);
        cf_material_set_uniform_vs(pass->material, "u_rect", (void *)rect, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_vs(pass->material, "u_depth", (void *)depth, CF_UNIFORM_TYPE_FLOAT2, 1);
        camera_uniforms(pass->material, view);
        cf_material_set_uniform_fs(pass->material, "u_fallback", coefficients, CF_UNIFORM_TYPE_FLOAT4, 4);
        cf_material_set_uniform_fs(pass->material, "u_surface", properties, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_dimensions", size, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_parameters", parameters, CF_UNIFORM_TYPE_FLOAT4, 32);
        CF_Shader shader = render_programs_bind_raster(programs, program, pass->material);
        // SDL GPU starts the render pass in apply_shader. Uploads above may
        // end a prior pass, so select the canvas after resource preparation.
        cf_apply_canvas(pass->canvas, *clear);
        *clear = false;
        cf_apply_mesh(geometry);
        cf_apply_shader(shader, pass->material);
        cf_apply_viewport(0, 0, pass->active_width, pass->active_height);
        cf_apply_scissor(0, 0, pass->active_width, pass->active_height);
        cf_draw_elements();
        pass->work.draw_calls++;
    }
    return ok;
}
static bool is_sdf(ecs_world *world, ecs_entity entity)
{
    const SdfSurface3D *sdf = SdfSurface3D_get_const(world, entity);
    return sdf && sdf->enabled;
}
static bool sdf_record(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actor,
    const render_mesh_surface *surface,
    float time,
    sdf_instance *record,
    sdf_batch_key *key,
    sdf_parameters *parameters
)
{
    ecs_entity entity = actor->entity;
    if (actor->sdf_shape) {
        pass->work.sdf_lod_instances++;
    }
    const Shader *binding = Shader_get_const(world, entity);
    key->shape = binding ? render_shader_find(
                               actor->sdf_shape ? actor->sdf_shape : binding->shape
                           )
                         : 0;
    if (!key->shape || !render_shader_is_shape(key->shape)) {
        snprintf(pass->error, sizeof pass->error, "SDF surface requires a valid Shape graph.");
        return false;
    }
    key->preview = inspected(pass, entity);
    key->preview_part = key->preview ? pass->inspection.part : -1;
    if (key->preview) {
        memcpy(key->preview_plane, pass->inspection.normal, 3 * sizeof(float));
        key->preview_plane[3] = pass->inspection.offset;
        key->preview_options[0] = 1;
        key->preview_options[1] = pass->inspection.section;
    }
    const char *name = binding->material;
    key->material = render_shader_find(name);
    if (name && *name && (!key->material || render_shader_is_shape(key->material))) {
        snprintf(pass->error, sizeof pass->error, "Unknown mesh surface material: %s", name);
        return false;
    }
    const Appearance *appearance = Appearance_get_const(world, entity);
    const Material *material = Material_get_const(world, entity);
    Material fallback = { .scene = appearance ? appearance->color : 0xffffffffu,
                          .albedo = appearance ? appearance->color : 0xffffffffu,
                          .edge_light = 1 };
    material_coefficients color;
    if (!material_extract(material ? material : &fallback, &color)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh material coefficients.");
        return false;
    }
    memcpy(record->colors[0], color.scene, 16);
    memcpy(record->colors[1], color.emission, 16);
    memcpy(record->colors[2], color.albedo, 16);
    memcpy(record->colors[3], color.absorption, 16);
    const render_mesh_surface_part *part = &surface->parts[0];
    CF_M4x4 model = render_scene3d_visual_model(&part->visual),
            frame = part->visual.frame;
    for (int row = 0; row < 3; row++) {
        float norm = 0;
        for (int col = 0; col < 3; col++) {
            norm += frame.elements[row * 4 + col] * frame.elements[row * 4 + col];
        }
        for (int col = 0; col < 4; col++) {
            record->model[row][col] = model.elements[col * 4 + row];
        }
        for (int col = 0; col < 3; col++) {
            record->inverse[row][col] = frame.elements[row * 4 + col] / norm;
        }
        record->inverse[row][3] = -record->inverse[row][0] * frame.elements[12] -
                                  record->inverse[row][1] * frame.elements[13] -
                                  record->inverse[row][2] * frame.elements[14];
    }
    memcpy(
        record->center,
        (float[4]){ part->sdf_center.x, part->sdf_center.y, part->sdf_center.z, 0 },
        16
    );
    CF_V3 dims = part->visual.part.dimensions;
    memcpy(record->dimensions, (float[4]){ dims.x, dims.y, dims.z, PRIMITIVE_BOX }, 16);
    memcpy(
        record->surface,
        (float[4]){ key->material, time, material ? material->edge_light : 1, 0 },
        16
    );
    render_shader_parameters_cached(
        key->material,
        world,
        entity,
        binding->parameters,
        parameters->material,
        &pass->cache
    );
    if (render_shader_is_model(key->shape)) {
        float min[3], max[3];
        bool empty;
        shader_error diagnostic = { 0 };
        if (!render_shader_model_snapshot(
                key->shape,
                world,
                entity,
                binding->parameters,
                parameters->shape,
                min,
                max,
                &empty,
                &diagnostic
            )) {
            snprintf(pass->error, sizeof pass->error, "%s", diagnostic.message);
            return false;
        }
        if (key->preview) {
            bool valid_uv = !empty && max[0] > min[0] && max[1] > min[1] &&
                            max[2] > min[2];
            for (int axis = 0; axis < 3; axis++) {
                key->preview_uv_bounds[0][axis] = valid_uv
                                                    ? (min[axis] + max[axis]) * .5f
                                                    : record->center[axis];
                key->preview_uv_bounds[1][axis] = valid_uv
                                                    ? max[axis] - min[axis]
                                                    : record->dimensions[axis];
            }
        }
        if (key->preview && key->preview_part >= 0 &&
            !render_shader_model_preview_factor(
                key->shape,
                parameters->shape,
                key->preview_part,
                &key->preview_options[0],
                &diagnostic
            )) {
            snprintf(pass->error, sizeof pass->error, "%s", diagnostic.message);
            return false;
        }
        render_shader_set resources = { 0 };
        if (!render_shader_model_material_snapshot(
                key->shape,
                world,
                entity,
                binding->parameters,
                key->material,
                key->materials,
                parameters->material,
                &resources,
                &diagnostic
            )) {
            snprintf(pass->error, sizeof pass->error, "%s", diagnostic.message);
            return false;
        }
    } else {
        key->materials[0][0] = key->material;
        key->materials[0][2] = 32;
        render_shader_parameters_cached(
            key->shape,
            world,
            entity,
            binding->parameters,
            parameters->shape,
            &pass->cache
        );
    }
    return true;
}
static CF_Mesh sdf_mesh(render_mesh_material *pass, const render_mesh_surface_part *part)
{
    if (pass->sdf_mesh.id) {
        return pass->sdf_mesh;
    }
    CF_VertexAttribute attrs[15] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(render_mesh_surface_vertex, normal) }
    };
    static const char *names[] = {
        "in_model0",   "in_model1",   "in_model2",     "in_inverse0",
        "in_inverse1", "in_inverse2", "in_dimensions", "in_color0",
        "in_color1",   "in_color2",   "in_color3",     "in_surface",
        "in_center"
    };
    for (int i = 0; i < 13; i++) {
        attrs[i + 2] = (CF_VertexAttribute){ .name = names[i],
                                             .format = CF_VERTEX_FORMAT_FLOAT4,
                                             .offset = i * 4 * sizeof(float),
                                             .per_instance = true };
    }
    int count = render_mesh_surface_corner_count(part);
    CF_Mesh result = cf_make_mesh(
        count * sizeof *part->vertices,
        attrs,
        15,
        sizeof *part->vertices
    );
    if (result.id) {
        cf_mesh_update_vertex_data(result, part->vertices, count);
        cf_mesh_set_instance_buffer(result, sizeof(sdf_instance), sizeof(sdf_instance));
        pass->sdf_mesh = result;
    }
    return result;
}
static uint64_t pair_hash(int shape, int material)
{
    uint64_t value = ((uint64_t)(uint32_t)shape << 32) | (uint32_t)material;
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
static void reset_slots(int **slots, int count)
{
    int size = 2;
    while (size < count * 2) {
        size *= 2;
    }
    asetlen(*slots, size);
    memset(*slots, 0, size * sizeof **slots);
}
static int prepare_sdf_program(render_mesh_material *pass, sdf_batch_key key)
{
    int mask = acount(pass->pair_slots) - 1;
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *bytes = (const unsigned char *)&key;
    for (size_t i = 0; i < sizeof key; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    int slot = (int)(hash & (uint64_t)mask);
    for (;;) {
        pass->work.sdf_pair_probes++;
        int index = pass->pair_slots[slot] - 1;
        if (index < 0) {
            break;
        }
        sdf_prepared_program *entry = &pass->prepared_programs[index];
        if (!memcmp(&entry->key, &key, sizeof key)) {
            return entry->program;
        }
        slot = (slot + 1) & mask;
    }
    render_shader_set set = { 0 };
    if (!render_shader_is_model(key.shape)) {
        set.shape = key.shape;
        set.material = key.material;
    }
    shader_error error = { 0 };
    if (!render_shader_set_add(&set, key.material, &error) ||
        !render_shader_set_add(&set, key.shape, &error)) {
        snprintf(pass->error, sizeof pass->error, "%s", error.message);
        return 0;
    }
    for (int slot = 1; slot <= MODEL_MATERIAL_SLOT_MAX; slot++) {
        if (!render_shader_set_add(&set, (int)key.materials[slot][0], &error)) {
            snprintf(pass->error, sizeof pass->error, "%s", error.message);
            return 0;
        }
    }
    render_programs *programs = pass->sdf_programs;
    if (key.preview) {
        if (!pass->preview_programs) {
            pass->preview_programs = render_programs_new(RENDER_PROGRAM_SDF_PREVIEW);
        }
        programs = pass->preview_programs;
        render_programs_set_model_preview(programs, key.shape, key.preview_part);
    }
    int program = render_programs_prepare(programs, &set);
    pass->work.sdf_program_prepares++;
    if (!program) {
        snprintf(pass->error, sizeof pass->error, "%s", render_programs_error(programs));
        return 0;
    }
    apush(pass->prepared_programs, ((sdf_prepared_program){ key, program }));
    pass->pair_slots[slot] = acount(pass->prepared_programs);
    return program;
}
static int sdf_parameter_row(render_mesh_material *pass, const sdf_parameters *parameters)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *bytes = (const unsigned char *)parameters;
    for (size_t i = 0; i < sizeof *parameters; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    hash = pair_hash((int)(hash >> 32), (int)(uint32_t)hash);
    int mask = acount(pass->parameter_slots) - 1;
    int slot = (int)(hash & (uint64_t)mask);
    for (;;) {
        pass->work.sdf_parameter_probes++;
        int index = pass->parameter_slots[slot] - 1;
        if (index < 0) {
            break;
        }
        if (pass->parameter_hashes[index] == hash &&
            !memcmp(&pass->parameter_rows[index], parameters, sizeof *parameters)) {
            return index;
        }
        slot = (slot + 1) & mask;
    }
    int index = acount(pass->parameter_rows);
    apush(pass->parameter_rows, *parameters);
    apush(pass->parameter_hashes, hash);
    pass->parameter_slots[slot] = index + 1;
    return index;
}
static bool upload_sdf_parameters(render_mesh_material *pass)
{
    int count = acount(pass->parameter_rows);
    if (count > pass->parameter_capacity) {
        CF_StorageBuffer buffer = cf_make_storage_buffer(
            cf_storage_buffer_defaults(count * sizeof(sdf_parameters))
        );
        if (!buffer.id) {
            snprintf(pass->error, sizeof pass->error, "Cannot allocate SDF parameter table.");
            return false;
        }
        if (pass->parameter_buffer.id) {
            cf_destroy_storage_buffer(pass->parameter_buffer);
        }
        pass->parameter_buffer = buffer;
        pass->parameter_capacity = count;
    }
    int bytes = count * sizeof(sdf_parameters);
    cf_update_storage_buffer(pass->parameter_buffer, pass->parameter_rows, bytes);
    pass->work.sdf_parameter_uploads++;
    pass->work.sdf_parameter_rows = count;
    pass->work.sdf_parameter_upload_bytes = bytes;
    return true;
}
static void schedule_sdfs(render_mesh_material *pass, sdf_staged *staged, int count)
{
    if (!pass->hidden_rejection) {
        return;
    }
    int limit = RENDER_SDF_BATCH_LIMIT;
    for (int first = 0; first < count;) {
        if (!staged[first].program) {
            first++;
            continue;
        }
        int n = 1;
        while (first + n < count && n < limit && staged[first + n].program &&
               !memcmp(
                   &staged[first].key,
                   &staged[first + n].key,
                   sizeof staged[first].key
               )) {
            n++;
        }
        // A bounded dependency graph preserves every overlapping pair's order.
        // Ready actors do not overlap, so one instanced draw shares a snapshot.
        // Chunking caps pair tests at 256 per actor rather than N per actor.
        int degree[RENDER_SDF_BATCH_LIMIT] = { 0 }, order[RENDER_SDF_BATCH_LIMIT];
        render_pixel_rect bounds[RENDER_SDF_BATCH_LIMIT];
        sdf_instance instances[RENDER_SDF_BATCH_LIMIT];
        for (int i = 0; i < n; i++) {
            bounds[i] = pass->actor_rects[first + i];
            instances[i] = pass->instances[staged[first + i].offset];
            for (int j = 0; j < i; j++) {
                degree[i] += overlaps(bounds[j], bounds[i]);
            }
        }
        int used = 0;
        while (used < n) {
            int begin = used;
            for (int i = 0; i < n; i++) {
                if (!degree[i]) {
                    order[used++] = i;
                    degree[i] = -1;
                }
            }
            assert(used > begin);
            for (int i = begin; i < used; i++) {
                int completed = order[i];
                for (int j = completed + 1; j < n; j++) {
                    if (degree[j] > 0 && overlaps(bounds[completed], bounds[j])) {
                        degree[j]--;
                    }
                }
            }
        }
        int offset = staged[first].offset;
        for (int i = 0; i < n; i++) {
            pass->instances[offset + i] = instances[order[i]];
            pass->actor_rects[first + i] = bounds[order[i]];
            staged[first + i].offset = offset + i;
        }
        first += n;
    }
}
static void sphere_coverage_bounds(
    render_mesh_material *pass,
    int actor,
    const sdf_instance *instance,
    const sdf_staged *staged,
    const sdf_parameters *parameters,
    render_material_view view
)
{
    float radius;
    if (!render_shader_sphere_radius(staged->key.shape, parameters->shape, &radius)) {
        return;
    }
    const float *size = instance->dimensions;
    // Include the marcher residual and float transform error. This only narrows
    // scheduling/copy coverage; authored proxy geometry and material UVs stay intact.
    radius += fmaxf(0.00002f, fmaxf(size[0], fmaxf(size[1], size[2])) * .0004f);
    CF_V3 lo, hi;
    float *a = &lo.x, *b = &hi.x;
    for (int row = 0; row < 3; row++) {
        float squared = 0, center = instance->model[row][3];
        for (int axis = 0; axis < 3; axis++) {
            float value = instance->model[row][axis] / size[axis];
            squared += value * value;
            center -= value * instance->center[axis];
        }
        float extent = radius * sqrtf(squared);
        a[row] = center - extent;
        b[row] = center + extent;
    }
    CF_V2 projected_lo, projected_hi;
    if (!render_mesh_project_bounds(view, lo, hi, &projected_lo, &projected_hi)) {
        return;
    }
    render_pixel_rect tight = render_mesh_pixel_bounds(
        view,
        projected_lo,
        projected_hi,
        pass->width,
        pass->height
    );
    render_pixel_rect *old = &pass->actor_rects[actor];
    if (tight.x > old->x) {
        old->x = tight.x;
    }
    if (tight.y > old->y) {
        old->y = tight.y;
    }
    if (tight.right < old->right) {
        old->right = tight.right;
    }
    if (tight.bottom < old->bottom) {
        old->bottom = tight.bottom;
    }
}
static bool stage_sdfs(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    const render_mesh_surface *const *surfaces,
    int count,
    render_material_view view,
    float time,
    sdf_staged *staged
)
{
    aclear(pass->instances);
    aclear(pass->prepared_programs);
    aclear(pass->parameter_rows);
    aclear(pass->parameter_hashes);
    reset_slots(&pass->pair_slots, count);
    reset_slots(&pass->parameter_slots, count);
    for (int i = 0; i < count; i++) {
        if (!acount(surfaces[i]->parts)) {
            continue;
        }
        if (!is_sdf(world, actors[i].entity)) {
            // Vertex uploads must also precede the group's surface render pass.
            for (int j = 0; j < acount(surfaces[i]->parts); j++) {
                if (!mesh(pass, &surfaces[i]->parts[j]).id) {
                    snprintf(pass->error, sizeof pass->error, "Cannot allocate mesh vertices.");
                    return false;
                }
            }
            continue;
        }
        sdf_instance record;
        sdf_parameters parameters;
        if (!sdf_record(
                pass,
                world,
                &actors[i],
                surfaces[i],
                time,
                &record,
                &staged[i].key,
                &parameters
            )) {
            return false;
        }
        record.dimensions[3] = sdf_parameter_row(pass, &parameters);
        sphere_coverage_bounds(pass, i, &record, &staged[i], &parameters, view);
        staged[i].offset = acount(pass->instances);
        apush(pass->instances, record);
        if (!pass->sdf_programs) {
            pass->sdf_programs = render_programs_new(RENDER_PROGRAM_SDF_MATERIAL);
        }
        staged[i].programs = pass->sdf_programs;
        staged[i].program = prepare_sdf_program(pass, staged[i].key);
        if (!staged[i].program) {
            return false;
        }
        if (staged[i].key.preview) {
            staged[i].programs = pass->preview_programs;
        }
        if (!sdf_mesh(pass, &surfaces[i]->parts[0]).id) {
            snprintf(pass->error, sizeof pass->error, "Cannot allocate SDF instance mesh.");
            return false;
        }
    }
    int n = acount(pass->instances);
    if (!n) {
        return true;
    }
    schedule_sdfs(pass, staged, count);
    if (!upload_sdf_parameters(pass)) {
        return false;
    }
    if (!pass->instance_buffer) {
        pass->instance_buffer = cf_make_instance_buffer(n * sizeof(sdf_instance), sizeof(sdf_instance));
    }
    cf_update_instance_buffer(pass->instance_buffer, pass->instances, n);
    pass->work.sdf_uploads++;
    pass->work.sdf_upload_bytes += (size_t)n * sizeof(sdf_instance);
    return true;
}
static int draw_sdfs(
    render_mesh_material *pass,
    const sdf_staged *staged,
    const render_pixel_rect *bounds,
    int count,
    const float rect[4],
    const float depth[2],
    render_material_view view,
    bool *clear
)
{
    const sdf_batch_key *key = &staged[0].key;
    int limit = pass->sdf_batch_limit ? pass->sdf_batch_limit : RENDER_SDF_BATCH_LIMIT;
    int n = 1;
    for (; n < count && n < limit && staged[n].program; n++) {
        if (memcmp(&staged[n].key, key, sizeof *key)) {
            break;
        }
        bool overlap = false;
        for (int j = 0; pass->hidden_rejection && j < n; j++) {
            overlap |= overlaps(bounds[j], bounds[n]);
        }
        if (overlap) {
            break;
        }
    }
    CF_Mesh geometry = pass->sdf_mesh;
    int program = staged[0].program;
    float shape[] = { key->shape, 0, 0, 0 };
    float quality[] = { pass->quality.sdf_pixel_error,
                        pass->quality.sdf_normal_mode == SDF_NORMAL_ANALYTIC ? 0
                        : pass->quality.sdf_normal_mode == SDF_NORMAL_TETRAHEDRAL
                            ? 4
                            : 6,
                        pass->sdf_trace_debug,
                        0 };
    cf_material_set_uniform_fs(pass->material, "u_sdf_quality", quality, CF_UNIFORM_TYPE_FLOAT4, 1);
    float occlusion[] = { pass->hidden_rejection && pass->has_depth, DEPTH_MARGIN };
    cf_material_set_uniform_fs(pass->material, "u_occlusion", occlusion, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_texture_fs(
        pass->material,
        "u_geometric_depth",
        cf_canvas_get_depth_stencil_target(pass->depth_snapshot)
    );
    cf_material_set_uniform_vs(pass->material, "u_rect", (void *)rect, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_vs(pass->material, "u_depth", (void *)depth, CF_UNIFORM_TYPE_FLOAT2, 1);
    camera_uniforms(pass->material, view);
    cf_material_set_uniform_fs(pass->material, "u_depth", (void *)depth, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(pass->material, "u_shape", shape, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(
        pass->material,
        "u_model_materials",
        (void *)key->materials,
        CF_UNIFORM_TYPE_FLOAT4,
        MODEL_MATERIAL_SLOT_MAX + 1
    );
    if (key->preview) {
        cf_material_set_uniform_fs(
            pass->material,
            "u_preview_uv_bounds",
            (void *)key->preview_uv_bounds,
            CF_UNIFORM_TYPE_FLOAT4,
            2
        );
        cf_material_set_uniform_fs(
            pass->material,
            "u_preview_plane",
            (void *)key->preview_plane,
            CF_UNIFORM_TYPE_FLOAT4,
            1
        );
        cf_material_set_uniform_fs(
            pass->material,
            "u_preview_options",
            (void *)key->preview_options,
            CF_UNIFORM_TYPE_FLOAT4,
            1
        );
    }
    CF_Shader shader = render_programs_bind_raster(staged[0].programs, program, pass->material);
    cf_apply_canvas(pass->canvas, *clear);
    *clear = false;
    cf_apply_mesh(geometry);
    cf_apply_instance_buffer_override(pass->instance_buffer, n, staged[0].offset * sizeof(sdf_instance));
    cf_apply_shader(shader, pass->material);
    cf_apply_fs_storage_buffers(&pass->parameter_buffer, 1);
    cf_apply_viewport(0, 0, pass->active_width, pass->active_height);
    cf_apply_scissor(0, 0, pass->active_width, pass->active_height);
    cf_draw_elements();
    pass->work.draw_calls++;
    pass->work.sdf_draw_calls++;
    pass->work.sdf_instances += n;
    return n;
}
void render_mesh_material_set_sdf_batch_limit(render_mesh_material *pass, int limit)
{
    assert(pass && limit >= 0 && limit <= RENDER_SDF_BATCH_LIMIT);
    pass->sdf_batch_limit = limit;
}
void render_mesh_material_set_quality(render_mesh_material *pass, SurfaceQuality quality)
{
    assert(quality.resolution >= SURFACE_SUPERSAMPLED && quality.resolution <= SURFACE_FULL);
    assert(
        isfinite(quality.sdf_pixel_error) && quality.sdf_pixel_error >= 0 &&
        quality.sdf_pixel_error <= 1
    );
    assert(
        quality.sdf_normal_mode >= SDF_NORMAL_CENTRAL && quality.sdf_normal_mode <= SDF_NORMAL_ANALYTIC
    );
    if (quality.resolution != pass->quality.resolution && pass->canvas.id) {
        cf_destroy_canvas(pass->canvas);
        pass->canvas = (CF_Canvas){ 0 };
        pass->scratch_width = pass->scratch_height = 0;
    }
    pass->quality = quality;
}
void render_mesh_material_set_sdf_trace_debug(render_mesh_material *pass, int mode)
{
    assert(mode >= 0 && mode <= 2);
    pass->sdf_trace_debug = mode;
}
void render_mesh_material_set_hidden_rejection(render_mesh_material *pass, bool enabled)
{
    pass->hidden_rejection = enabled;
}
render_mesh_material_work render_mesh_material_last_work(const render_mesh_material *pass)
{
    render_mesh_material_work work = pass->work;
    for (int i = 0; i < acount(pass->meshes); i++) {
        work.dynamic_meshes += pass->meshes[i].dynamic_identity != 0;
    }
    if (pass->sdf_programs) {
        work.sdf_program_count = render_programs_count(pass->sdf_programs);
        work.sdf_program_compilations = render_programs_successful_compilations(
            pass->sdf_programs
        );
    }
    if (pass->preview_programs) {
        work.sdf_program_count += render_programs_count(pass->preview_programs);
        work.sdf_program_compilations += render_programs_successful_compilations(
            pass->preview_programs
        );
    }
    return work;
}
bool render_mesh_material_draw(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    float time
)
{
    assert(pass && world && actors && count > 0 && count <= INT_MAX / (int)sizeof(sdf_parameters));
    pass->error[0] = 0;
    pass->work = (render_mesh_material_work){ 0 };
    if (pass->width < 1 || pass->height < 1 || !isfinite(view.min_x) ||
        !isfinite(view.max_y) || !isfinite(view.width) || !isfinite(view.height) ||
        view.width <= 0 || view.height <= 0 || !isfinite(time) ||
        !isfinite(view.focal_distance) || view.focal_distance < 0 ||
        (view.focal_distance > 0 && view.focal_distance <= RENDER_MESH_NEAR) ||
        !isfinite(view.center_x) || !isfinite(view.center_y) ||
        !isfinite(view.yaw) || !isfinite(view.pitch) || !isfinite(view.roll) ||
        !isfinite(view.target_elevation)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh material view.");
        return false;
    }
    while (acount(pass->owned_surfaces) < count) {
        apush(pass->owned_surfaces, ((render_mesh_surface){ 0 }));
    }
    asetlen(pass->surfaces, count);
    asetlen(pass->staged, count);
    for (int i = 0; i < count; i++) {
        pass->staged[i] = (sdf_staged){ 0 };
    }
    const render_mesh_surface **surfaces = pass->surfaces;
    sdf_staged *staged = pass->staged;
    CF_V3 min = { INFINITY, INFINITY, INFINITY },
          max = { -INFINITY, -INFINITY, -INFINITY };
    bool ok = true;
    for (int i = 0; i < count; i++) {
        model_bounds selected_bounds, *override = NULL;
        if (ecs_entity_to_u64(actors[i].entity) ==
                ecs_entity_to_u64(pass->inspection.entity) &&
            pass->inspection.part >= 0) {
            const Shader *binding = Shader_get_const(world, actors[i].entity);
            shader_error diagnostic = { 0 };
            if (!binding || !render_shader_model_preview_snapshot(
                                render_shader_find(binding->shape),
                                world,
                                actors[i].entity,
                                binding->parameters,
                                pass->inspection.part,
                                &selected_bounds,
                                &diagnostic
                            )) {
                snprintf(pass->error, sizeof pass->error, "%s", diagnostic.message);
                ok = false;
                goto done;
            }
            override = &selected_bounds;
        }
        surfaces[i] = override ? NULL : actors[i].surface;
        if (surfaces[i]) {
            pass->work.surface_reuses++;
        } else {
            surfaces[i] = &pass->owned_surfaces[i];
            pass->work.surface_builds++;
            if (!render_mesh_surface_build_with_bounds(
                    world,
                    actors[i].entity,
                    &actors[i].position,
                    NULL,
                    override,
                    &pass->owned_surfaces[i],
                    pass->error,
                    sizeof pass->error
                )) {
                ok = false;
                goto done;
            }
        }
        CF_V3 lo, hi;
        render_mesh_camera_bounds(view, surfaces[i]->min, surfaces[i]->max, &lo, &hi);
        min = cf_min_v3(min, lo);
        max = cf_max_v3(max, hi);
    }
    if (!isfinite(min.x + min.y + min.z + max.x + max.y + max.z)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh group bounds.");
        ok = false;
        goto done;
    }
    float depth[] = { min.y - 1, max.y + 1 };
    if (view.focal_distance > 0) {
        depth[0] = RENDER_MESH_NEAR;
        depth[1] = fmaxf(RENDER_MESH_NEAR + 1, view.focal_distance - min.y + 1);
    }
    actors = order_actors(pass, actors, count, depth, view);
    asetlen(pass->actor_rects, count);
    render_pixel_rect bounds = { pass->width, pass->height, 0, 0 };
    for (int i = 0; i < count; i++) {
        CF_V2 lo, hi;
        if (!render_mesh_project_bounds(view, surfaces[i]->min, surfaces[i]->max, &lo, &hi)) {
            // Submit clipped geometry so a real render pass executes the clear.
            lo = hi = (CF_V2){ view.min_x, -view.max_y };
        }
        pass->actor_rects[i] = render_mesh_pixel_bounds(view, lo, hi, pass->width, pass->height);
        bounds = joined(bounds, pass->actor_rects[i]);
    }
    float sx = pass->width / view.width, sy = pass->height / view.height;
    int x = bounds.x, y = bounds.y, right = bounds.right, bottom = bounds.bottom;
    if (right <= x || bottom <= y) {
        snprintf(pass->error, sizeof pass->error, "Mesh lies outside the material view.");
        ok = false;
        goto done;
    }
    if (!scratch(pass, right - x, bottom - y)) {
        ok = false;
        goto done;
    }
    pass->work.surface_pixels = pass->active_width * pass->active_height;
    pass->work.surface_capacity_pixels = pass->scratch_width * pass->scratch_height;
    pass->rect[0] = x;
    pass->rect[1] = y;
    float scale = pass->quality.resolution == SURFACE_FULL ? 1 : .5f;
    pass->rect[2] = pass->active_width * scale;
    pass->rect[3] = pass->active_height * scale;
    float rect[] = { view.min_x + x / sx,
                     view.max_y - y / sy,
                     pass->rect[2] / sx,
                     pass->rect[3] / sy };
    if (!stage_sdfs(pass, world, actors, surfaces, count, view, time, staged)) {
        ok = false;
        goto done;
    }
    bool has_sdfs = acount(pass->instances) > 0;
    if (has_sdfs && !depth_snapshot(pass)) {
        ok = false;
        goto done;
    }
    pass->has_depth = false;
    bool clear = true;
    int last_sdf = -1;
    for (int i = 0; i < count; i++) {
        if (staged[i].program) {
            last_sdf = i;
        }
    }
    for (int i = 0; i < count && ok;) {
        if (!acount(surfaces[i]->parts)) {
            i++;
            continue;
        }
        int n = 1;
        if (is_sdf(world, actors[i].entity)) {
            n = draw_sdfs(pass, staged + i, pass->actor_rects + i, count - i, rect, depth, view, &clear);
            ok = n > 0;
        } else {
            ok = draw_actor(pass, world, actors[i].entity, surfaces[i], rect, depth, view, time, &clear);
        }
        if (!ok) {
            break;
        }
        if (pass->hidden_rejection && i + n <= last_sdf) {
            // Depth copies are whole-canvas: D3D12 and WebGPU copy depth only as a whole texture.
            cf_canvas_copy_depth(pass->depth_snapshot, pass->canvas);
            pass->work.depth_copies++;
            pass->work.depth_copy_pixels += (size_t)pass->scratch_width *
                                            pass->scratch_height;
            pass->has_depth = true;
        }
        i += n;
    }
done:
    return ok;
}

const char *render_mesh_material_error(const render_mesh_material *pass)
{
    return pass->error;
}
render_mesh_material_textures render_mesh_material_get_textures(const render_mesh_material *pass)
{
    return (render_mesh_material_textures){ cf_canvas_get_target2(pass->canvas, 0),
                                            cf_canvas_get_target2(pass->canvas, 1),
                                            cf_canvas_get_target2(pass->canvas, 2),
                                            cf_canvas_get_target2(pass->canvas, 3),
                                            cf_canvas_get_target2(pass->canvas, 4) };
}
void render_mesh_material_rect(const render_mesh_material *pass, float rect[4])
{
    memcpy(rect, pass->rect, sizeof pass->rect);
}

void render_mesh_material_extent(const render_mesh_material *pass, int active[2], int capacity[2])
{
    if (active) {
        active[0] = pass->active_width;
        active[1] = pass->active_height;
    }
    if (capacity) {
        capacity[0] = pass->scratch_width;
        capacity[1] = pass->scratch_height;
    }
}
