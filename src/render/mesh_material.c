#include "render/mesh_material.h"

#include "render/material.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/shader_program.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int primitive;
    uint64_t asset_identity;
    CF_Mesh mesh;
} primitive_mesh;
struct render_mesh_material
{
    render_programs *programs;
    render_shader_parameter_cache *cache;
    CF_Material material;
    CF_Canvas canvas;
    primitive_mesh *meshes;
    int width, height, scratch_width, scratch_height;
    float rect[4];
    char error[256];
};
render_mesh_material *render_mesh_material_new()
{
    render_mesh_material *pass = xcalloc(1, sizeof *pass);
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
void render_mesh_material_free(render_mesh_material *pass)
{
    if (!pass) {
        return;
    }
    for (int i = 0; i < acount(pass->meshes); i++) {
        cf_destroy_mesh(pass->meshes[i].mesh);
    }
    afree(pass->meshes);
    if (pass->canvas.id) {
        cf_destroy_canvas(pass->canvas);
    }
    if (pass->material.id) {
        cf_destroy_material(pass->material);
    }
    render_programs_free(pass->programs);
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
    width = (width * 2 + 63) / 64 * 64;
    height = (height * 2 + 63) / 64 * 64;
    if (pass->canvas.id && pass->scratch_width >= width &&
        pass->scratch_height >= height) {
        return true;
    }
    width = width > pass->scratch_width ? width : pass->scratch_width;
    height = height > pass->scratch_height ? height : pass->scratch_height;
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target_count = 5;
    params.depth_stencil_enable = true;
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
    pass->scratch_width = width;
    pass->scratch_height = height;
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0 });
    cf_canvas_set_clear_depth_stencil(canvas, 1, 0);
    return true;
}
static CF_Mesh mesh(render_mesh_material *pass, const render_mesh_surface_part *part)
{
    for (int i = 0; i < acount(pass->meshes); i++) {
        if (pass->meshes[i].primitive == part->visual.part.primitive &&
            pass->meshes[i].asset_identity == part->asset_identity) {
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
          .offset = offsetof(render_mesh_surface_vertex, uv) }
    };
    int count = acount(part->vertices);
    CF_Mesh result = cf_make_mesh(
        count * sizeof *part->vertices,
        attributes,
        3,
        sizeof *part->vertices
    );
    if (result.id) {
        cf_mesh_update_vertex_data(result, part->vertices, count);
        apush(pass->meshes, ((primitive_mesh){ part->visual.part.primitive, part->asset_identity, result }));
    }
    return result;
}
static bool draw_actor(
    render_mesh_material *pass,
    ecs_world *world,
    ecs_entity entity,
    render_mesh_surface *surface,
    const float rect[4],
    const float depth[2],
    render_material_view view,
    float time,
    bool *clear
)
{
    const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
    const GameMaterial *material = GAME_GameMaterial_get_const(world, entity);
    GameMaterial fallback = { .scene = appearance ? appearance->color : 0xffffffffu,
                              .albedo = appearance ? appearance->color : 0xffffffffu,
                              .edge_light = 1 };
    game_material_coefficients color;
    if (!game_material_extract(material ? material : &fallback, &color)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh material coefficients.");
        return false;
    }
    const GameShader *binding = GAME_GameShader_get_const(world, entity);
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
        int program = render_programs_prepare(pass->programs, &set);
        if (!program) {
            snprintf(pass->error, sizeof pass->error, "%s", render_programs_error(pass->programs));
            ok = false;
            break;
        }
        CF_Mesh geometry = mesh(pass, part);
        if (!geometry.id) {
            snprintf(pass->error, sizeof pass->error, "Cannot allocate mesh vertices.");
            ok = false;
            break;
        }
        CF_M4x4 model = render_arena_visual_model(&part->visual);
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
        render_shader_parameters_cached(
            id,
            world,
            entity,
            binding ? binding->parameters : NULL,
            parameters,
            &pass->cache
        );
        float properties[] = { id,
                               time,
                               material ? material->edge_light : 1,
                               part->asset_identity ? 1 : 0 };
        CF_V3 dimensions = part->visual.part.dimensions;
        float size[] = { dimensions.x,
                         dimensions.y,
                         dimensions.z,
                         part->visual.part.primitive };
        cf_material_set_uniform_vs(pass->material, "u_model", rows, CF_UNIFORM_TYPE_FLOAT4, 3);
        cf_material_set_uniform_vs(pass->material, "u_rect", (void *)rect, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_vs(pass->material, "u_depth", (void *)depth, CF_UNIFORM_TYPE_FLOAT2, 1);
        float eye[] = { view.center_x,
                        view.focal_distance,
                        -view.center_y,
                        view.focal_distance > 0 };
        cf_material_set_uniform_vs(pass->material, "u_eye", eye, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_eye", eye, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_fallback", coefficients, CF_UNIFORM_TYPE_FLOAT4, 4);
        cf_material_set_uniform_fs(pass->material, "u_surface", properties, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_dimensions", size, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(pass->material, "u_parameters", parameters, CF_UNIFORM_TYPE_FLOAT4, 32);
        CF_Shader shader = render_programs_bind_raster(pass->programs, program, pass->material);
        // SDL GPU starts the render pass in apply_shader. Uploads above may
        // end a prior pass, so select the canvas after resource preparation.
        cf_apply_canvas(pass->canvas, *clear);
        *clear = false;
        cf_apply_mesh(geometry);
        cf_apply_shader(shader, pass->material);
        cf_apply_viewport(0, 0, pass->scratch_width, pass->scratch_height);
        cf_apply_scissor(0, 0, pass->scratch_width, pass->scratch_height);
        cf_draw_elements();
    }
    return ok;
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
    assert(pass && world && actors && count > 0);
    pass->error[0] = 0;
    if (pass->width < 1 || pass->height < 1 || !isfinite(view.min_x) ||
        !isfinite(view.max_y) || !isfinite(view.width) || !isfinite(view.height) ||
        view.width <= 0 || view.height <= 0 || !isfinite(time) ||
        !isfinite(view.focal_distance) || view.focal_distance < 0 ||
        (view.focal_distance > 0 && view.focal_distance <= RENDER_MESH_NEAR) ||
        !isfinite(view.center_x) || !isfinite(view.center_y)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh material view.");
        return false;
    }
    render_mesh_surface *surfaces = xcalloc(count, sizeof *surfaces);
    CF_V3 min = { INFINITY, INFINITY, INFINITY },
          max = { -INFINITY, -INFINITY, -INFINITY };
    bool ok = true;
    for (int i = 0; i < count; i++) {
        if (!render_mesh_surface_build(
                world,
                actors[i].entity,
                &actors[i].position,
                &surfaces[i],
                pass->error,
                sizeof pass->error
            )) {
            ok = false;
            goto done;
        }
        min = cf_min_v3(min, surfaces[i].min);
        max = cf_max_v3(max, surfaces[i].max);
    }
    if (!isfinite(min.x + min.y + min.z + max.x + max.y + max.z)) {
        snprintf(pass->error, sizeof pass->error, "Invalid mesh group bounds.");
        ok = false;
        goto done;
    }
    CF_V2 lo, hi;
    if (!render_mesh_project_bounds(view, min, max, &lo, &hi)) {
        // Submit the clipped geometry to clear the canvas through a real GPU
        // render pass. Selecting a canvas alone does not execute its clear.
        lo = hi = (CF_V2){ view.min_x, -view.max_y };
    }
    float sx = pass->width / view.width, sy = pass->height / view.height;
    int x = (int)floorf(fminf(pass->width, fmaxf(0, (lo.x - view.min_x) * sx))) - 1;
    int y = (int)floorf(fminf(pass->height, fmaxf(0, (view.max_y + lo.y) * sy))) - 1;
    int right = (int)ceilf(fminf(pass->width, fmaxf(0, (hi.x - view.min_x) * sx))) + 1;
    int bottom = (int)ceilf(fminf(pass->height, fmaxf(0, (view.max_y + hi.y) * sy))) + 1;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    right = right > pass->width ? pass->width : right;
    bottom = bottom > pass->height ? pass->height : bottom;
    if (right <= x || bottom <= y) {
        snprintf(pass->error, sizeof pass->error, "Mesh lies outside the material view.");
        ok = false;
        goto done;
    }
    if (!scratch(pass, right - x, bottom - y)) {
        ok = false;
        goto done;
    }
    pass->rect[0] = x;
    pass->rect[1] = y;
    pass->rect[2] = pass->scratch_width * .5f;
    pass->rect[3] = pass->scratch_height * .5f;
    float rect[] = { view.min_x + x / sx,
                     view.max_y - y / sy,
                     pass->rect[2] / sx,
                     pass->rect[3] / sy };
    float depth[] = { min.y - 1, max.y + 1 };
    if (view.focal_distance > 0) {
        depth[0] = RENDER_MESH_NEAR;
        depth[1] = fmaxf(RENDER_MESH_NEAR + 1, view.focal_distance - min.y + 1);
    }
    bool clear = true;
    for (int i = 0; i < count && ok; i++) {
        ok = draw_actor(pass, world, actors[i].entity, &surfaces[i], rect, depth, view, time, &clear);
    }
done:
    for (int i = 0; i < count; i++) {
        render_mesh_surface_free(&surfaces[i]);
    }
    free(surfaces);
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
