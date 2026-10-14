#include "authoring/visual_model_preview.h"

#include "authoring/shader_prepare.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "runtime/world.h"
#include "support/array.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct visual_model_preview
{
    world *world;
    ecs_entity object;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
    char *name;
    char *parameter_text;
    int width, height;
    bool drawn;
    int part, section_axis;
    float section_offset;
    float parameters[32][4];
    float pan_x, pan_y, reference_opacity;
    visual_model_reference reference;
    int reference_sampler;
    bool reference_visible;
    CF_Shader reference_shader;
    char reference_error[256];
};
static bool refuse(char *error, size_t size, const char *text)
{
    snprintf(error, size, "%s", text);
    return false;
}
bool visual_model_reference_corners(
    const visual_model_reference *r,
    int width,
    int height,
    const engine_viewport_camera *frame,
    CF_V2 out[4]
)
{
    if (!r || !frame || !out || width <= 0 || height <= 0 ||
        !isfinite(r->height) || r->height <= 0 || !isfinite(r->center_x) ||
        !isfinite(r->center_y) || !isfinite(r->angle) ||
        !isfinite(frame->center_x) || !isfinite(frame->center_y) ||
        !isfinite(frame->span_width) || frame->span_width <= 0 ||
        !isfinite(frame->span_height) || frame->span_height <= 0) {
        return false;
    }
    float w = r->height * (float)width / (float)height, c = cosf(r->angle),
          sn = sinf(r->angle);
    CF_V2 points[4];
    for (int i = 0; i < 4; i++) {
        float x = (i == 0 || i == 3 ? -.5f : .5f) * w,
              y = (i < 2 ? .5f : -.5f) * r->height;
        points[i] = (CF_V2){
            2 * (r->center_x + c * x - sn * y - frame->center_x) / frame->span_width,
            2 * (r->center_y + sn * x + c * y + frame->center_y) / frame->span_height
        };
        if (!isfinite(points[i].x) || !isfinite(points[i].y)) {
            return false;
        }
    }
    memcpy(out, points, sizeof points);
    return true;
}
static bool draw_reference(visual_model_preview *p, char *error, size_t size)
{
    if (!p->reference_visible || !p->reference.image ||
        !p->reference.image[0] || p->reference_opacity <= 0) {
        p->reference_sampler = 0;
        return true;
    }
    char key[1040];
    snprintf(key, sizeof key, "content/%s", p->reference.image);
    shader_image_source source = { .key = key,
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_BILINEAR,
                                   .wrap = SHADER_IMAGE_CLAMP };
    shader_error problem = { 0 };
    int sampler = render_image_variant(&source, &problem), width = 0, height = 0;
    CF_Texture texture = sampler ? render_image_texture(sampler, &problem)
                                 : (CF_Texture){ 0 };
    if (!texture.id || !render_image_info(key, &width, &height)) {
        return refuse(error, size, problem.message[0] ? problem.message : "Reference image is unavailable");
    }
    engine_viewport_camera frame;
    engine_viewport_get_camera(p->viewport, &frame);
    CF_V2 corners[4];
    if (!visual_model_reference_corners(&p->reference, width, height, &frame, corners)) {
        return refuse(error, size, "Reference alignment cannot be projected");
    }
    if (!p->reference_shader.id) {
        const char
            *shader = "layout(set=2,binding=1) uniform sampler2D "
                      "reference_image;\n"
                      "layout(set=3,binding=1) uniform shd_uniforms { vec4 "
                      "reference_rect; vec4 reference_view; vec2 "
                      "reference_turn; float reference_opacity; };\n"
                      "vec4 shader(vec4 color,ShaderParams params) {\n"
                      "vec2 "
                      "at=(params.screen_uv-vec2(.5))*reference_view.zw*vec2(1,"
                      "-1)+reference_view.xy-reference_rect.xy;\n"
                      "vec2 "
                      "local=vec2(reference_turn.x*at.x+reference_turn.y*at.y,-"
                      "reference_turn.y*at.x+reference_turn.x*at.y);\n"
                      "vec2 uv=local/reference_rect.zw*vec2(1,-1)+vec2(.5);\n"
                      "if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1)"
                      "))) return vec4(0);\n"
                      "vec4 pixel=texture(reference_image,uv); return "
                      "vec4(pixel.rgb,pixel.a*reference_opacity); }\n";
        p->reference_shader = cf_make_draw_shader_from_source(shader);
        if (!p->reference_shader.id) {
            return refuse(error, size, "Reference image shader could not compile");
        }
    }
    float rect[] = { p->reference.center_x,
                     p->reference.center_y,
                     p->reference.height * (float)width / height,
                     p->reference.height };
    float view[] = { frame.center_x, -frame.center_y, frame.span_width, frame.span_height };
    float turn[] = { cosf(p->reference.angle), sinf(p->reference.angle) };
    cf_draw_push_shader(p->reference_shader);
    cf_draw_set_texture("reference_image", texture);
    cf_draw_set_uniform("reference_rect", rect, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("reference_view", view, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("reference_turn", turn, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_draw_set_uniform("reference_opacity", &p->reference_opacity, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_quad_fill(
        (CF_Aabb){ .min = { frame.center_x - frame.span_width * .5f,
                            frame.center_y - frame.span_height * .5f },
                   .max = { frame.center_x + frame.span_width * .5f,
                            frame.center_y + frame.span_height * .5f } },
        0
    );
    cf_draw_pop_shader();
    p->reference_sampler = sampler;
    return true;
}
static void render(void *user)
{
    visual_model_preview *p = user;
    render_scene3d_camera camera;
    engine_viewport_get_camera3d(p->viewport, &camera);
    p->drawn = app_scene_renderer_draw3d(
        p->renderer,
        world_ecs(p->world),
        NULL,
        &camera,
        p->viewport,
        world_engine(p->world),
        (render_skip){ 0 }
    );
    if (p->drawn) {
        p->reference_error[0] = 0;
        p->drawn = draw_reference(p, p->reference_error, sizeof p->reference_error);
    }
}
visual_model_preview *visual_model_preview_new(const char *name, int width, int height, char *error, size_t size)
{
    if (!name || !*name || width < 16 || height < 16 || width > 512 ||
        height > 512 || width * height > 640 * 360) {
        refuse(error, size, "Model preview requires a name and bounded canvas dimensions");
        return NULL;
    }
    visual_model_preview *p = xcalloc(1, sizeof *p);
    p->name = xstrdup(name);
    p->part = -1;
    p->width = width;
    p->height = height;
    p->world = world_new(&(world_options){ 0 }, error, size);
    if (!p->world) {
        visual_model_preview_free(p);
        return NULL;
    }
    ecs_world *ecs = world_ecs(p->world);
    p->object = ecs_world_record_create(ecs, 0);
#define ADD(T, ...)                                                                    \
    do {                                                                               \
        T value = { __VA_ARGS__ };                                                     \
        int added = ecs_world_record_add(ecs, p->object, COMPONENT_##T.id, &value, 0); \
        assert(added == ECS_OK);                                                       \
        (void)added;                                                                   \
    } while (0)
    ADD(Transform, 0);
    ADD(Appearance, .width = 1, .height = 1, .color = 0xb7c4d6ff);
    ADD(Geometry3D, .primitive = PRIMITIVE_BOX, .width = 1, .height = 1, .depth = 1);
    ADD(SdfSurface3D, .enabled = true);
    ADD(VisualTransform, .scale = 1);
    ADD(Shader, .shape = p->name);
#undef ADD
    int applied = ecs_world_apply_commands(ecs);
    assert(applied == ECS_OK);
    (void)applied;
    p->renderer = app_scene_renderer_new();
    p->viewport = engine_viewport_new(width, height);
    engine_viewport_set_perspective(p->viewport, true);
    engine_viewport_set_runtime(p->viewport, world_engine(p->world));
    engine_viewport_set_render_callback(p->viewport, render, p);
    return p;
}
void visual_model_preview_free(visual_model_preview *p)
{
    if (!p) {
        return;
    }
    engine_viewport_free(p->viewport);
    if (p->renderer) {
        app_scene_renderer_free(p->renderer);
    }
    if (p->world) {
        world_free(p->world);
    }
    if (p->reference_shader.id) {
        cf_destroy_shader(p->reference_shader);
    }
    visual_model_reference_free(&p->reference);
    free(p->parameter_text);
    free(p->name);
    free(p);
}
bool visual_model_preview_draw(
    visual_model_preview *p,
    float time,
    float yaw,
    float pitch,
    float zoom,
    char *error,
    size_t size
)
{
    if (!p || !isfinite(time) || fabsf(time) > 3600 || !isfinite(yaw) ||
        fabsf(yaw) > 3.14159266f || !isfinite(pitch) ||
        fabsf(pitch) > 1.57079633f || !isfinite(zoom) || zoom < .25f || zoom > 4) {
        return refuse(error, size, "Model preview requires bounded finite time, orbit and zoom");
    }
    float values[32][4], lo[3], hi[3];
    bool empty;
    shader_error shader = { 0 };
    int id = render_shader_find(p->name);
    if (!id || !render_shader_is_model(id) ||
        !render_shader_model_snapshot(
            id,
            world_ecs(p->world),
            p->object,
            p->parameter_text,
            values,
            lo,
            hi,
            &empty,
            &shader
        )) {
        return refuse(error, size, shader.message[0] ? shader.message : "Applied model is unavailable");
    }
    memcpy(p->parameters, values, sizeof p->parameters);
    if (empty) {
        return refuse(error, size, "Model has no visible volume");
    }
    CF_V3 center = { (lo[0] + hi[0]) * .5f, (lo[1] + hi[1]) * .5f, (lo[2] + hi[2]) * .5f };
    float radius = .5f * hypotf(hypotf(hi[0] - lo[0], hi[1] - lo[1]), hi[2] - lo[2]);
    if (!(radius > 0) || !isfinite(radius)) {
        return refuse(error, size, "Model bounds cannot be framed");
    }
    float distance = radius * 4 / zoom;
    CF_V3 direction = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    CF_V3 right = { cosf(yaw), 0, -sinf(yaw) };
    CF_V3 vertical = { -sinf(yaw) * sinf(pitch), cosf(pitch), -cosf(yaw) * sinf(pitch) };
    center = cf_add_v3(
        center,
        cf_add_v3(cf_mul_v3_f(right, p->pan_x), cf_mul_v3_f(vertical, p->pan_y))
    );
    CF_V3 eye = cf_add_v3(center, cf_mul_v3_f(direction, distance));
    CF_V3 up = { -sinf(yaw) * sinf(pitch), cosf(pitch), -cosf(yaw) * sinf(pitch) };
    engine_viewport_view view = { .center_x = cf_dot_v3(center, right),
                                  .center_y = -cf_dot_v3(center, vertical),
                                  .width = radius * 2.5f / zoom,
                                  .height = radius * 2.5f / zoom,
                                  .lens = { .eye = eye,
                                            .forward = cf_neg_v3(direction),
                                            .up = up,
                                            .near_clip = radius * .001f,
                                            .far_clip = radius * 100 } };
    engine_viewport_set_view(p->viewport, &view);
    engine_viewport_set_time(p->viewport, time);
    app_scene_renderer_set_time(p->renderer, time);
    float normal[3] = { 0 };
    if (p->section_axis) {
        normal[p->section_axis - 1] = 1;
    }
    app_scene_renderer_set_model_inspection(
        p->renderer,
        p->object,
        p->part,
        p->section_axis > 0,
        normal,
        p->section_offset
    );
    p->drawn = false;
    if (engine_viewport_render(p->viewport, p->width, p->height) < 0 || !p->drawn) {
        return refuse(
            error,
            size,
            p->reference_error[0] ? p->reference_error
                                  : app_scene_renderer_error(p->renderer)
        );
    }
    error[0] = 0;
    return true;
}
uint64_t visual_model_preview_texture(const visual_model_preview *p)
{
    return p->drawn ? engine_viewport_texture(p->viewport) : 0;
}

bool visual_model_preview_inspection(
    visual_model_preview *p,
    int part,
    int axis,
    float offset,
    char *error,
    size_t size
)
{
    int id = render_shader_find(p->name);
    const model_inspection *info = render_shader_model_inspection(id);
    if (!info || part < -1 || part >= acount(info->parts) || axis < 0 ||
        axis > 3 || !isfinite(offset) || fabsf(offset) > 1e6f) {
        return refuse(error, size, "Model inspection requires an existing part, section axis0..3 and bounded finite offset");
    }
    p->part = part;
    p->section_axis = axis;
    p->section_offset = offset;
    error[0] = 0;
    return true;
}
bool visual_model_preview_images(visual_model_preview *p, render_shader_set *images, char *error, size_t size)
{
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4], values[32][4];
    shader_error problem = { 0 };
    int id = render_shader_find(p->name);
    *images = (render_shader_set){ 0 };
    bool ok = render_shader_set_add(images, id, &problem) &&
              render_shader_model_material_snapshot(
                  id,
                  world_ecs(p->world),
                  p->object,
                  p->parameter_text,
                  0,
                  bindings,
                  values,
                  images,
                  &problem
              );
    if (!ok) {
        return refuse(error, size, problem.message);
    }
    error[0] = 0;
    return true;
}
uint64_t visual_model_preview_take_canvas(visual_model_preview *p)
{
    p->drawn = false;
    return engine_viewport_take_canvas(p->viewport);
}

void visual_model_preview_parameters(const visual_model_preview *p, float out[32][4])
{
    memcpy(out, p->parameters, sizeof p->parameters);
}

bool visual_model_preview_parameter_text(visual_model_preview *p, const char *text, char *error, size_t size)
{
    if (!p || !text || strlen(text) > 1024 * 1024) {
        return refuse(error, size, "Model parameters require bounded JSON text");
    }
    int id = render_shader_find(p->name);
    shader_error problem = { 0 };
    char *candidate = xstrdup(text);
    if (!id || !render_shader_is_model(id) ||
        !render_shader_parameters_validate(id, candidate, &problem)) {
        free(candidate);
        return refuse(error, size, problem.message[0] ? problem.message : "Applied model is unavailable");
    }
    Shader *binding = ecs_component_data(
        world_ecs(p->world),
        p->object,
        COMPONENT_Shader.id
    );
    if (!binding) {
        free(candidate);
        return refuse(error, size, "Model preview binding is unavailable");
    }
    binding->parameters = candidate;
    free(p->parameter_text);
    p->parameter_text = candidate;
    p->drawn = false;
    error[0] = 0;
    return true;
}

bool visual_model_preview_pan(visual_model_preview *p, float x, float y, char *error, size_t size)
{
    if (!p || !isfinite(x) || !isfinite(y) || fabsf(x) > 1000000 || fabsf(y) > 1000000) {
        return refuse(error, size, "Model pan requires finite coordinates within +/-1000000");
    }
    p->pan_x = x;
    p->pan_y = y;
    p->drawn = false;
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool visual_model_preview_reference(
    visual_model_preview *p,
    const visual_model_reference *r,
    bool visible,
    float opacity,
    char *error,
    size_t size
)
{
    if (!p || !r || !r->image || strlen(r->image) > 1024 ||
        (r->image[0] && !rooted_files_path_valid(r->image)) ||
        !isfinite(opacity) || opacity < 0 || opacity > 1 ||
        !isfinite(r->height) || r->height <= 0 || r->height > 1000000 ||
        !isfinite(r->center_x) || !isfinite(r->center_y) || !isfinite(r->angle)) {
        return refuse(error, size, "Reference overlay requires finite alignment and opacity0..1");
    }
    visual_model_reference next = *r;
    next.image = xstrdup(r->image);
    visual_model_reference_free(&p->reference);
    p->reference = next;
    p->reference_visible = visible;
    p->reference_opacity = opacity;
    p->reference_sampler = 0;
    p->drawn = false;
    if (size) {
        error[0] = 0;
    }
    return true;
}
int visual_model_preview_reference_sampler(const visual_model_preview *p)
{
    return p->reference_sampler;
}
