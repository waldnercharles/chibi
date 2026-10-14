#include "render/fog.h"

#include "platform/cute.h"
#include "render/draw_textures.h"
#include "render/shader_program.h"
#include "render/view.h"
#include "support/xalloc.h"

#include <stdlib.h>

struct render_fog
{
    CF_Texture mask;
    CF_Shader shader;
    int width, height;
};

render_fog *render_fog_new()
{
    return xcalloc(1, sizeof(render_fog));
}

void render_fog_free(render_fog *fog)
{
    if (fog->mask.id) {
        cf_destroy_texture(fog->mask);
    }
    if (fog->shader.id) {
        cf_destroy_shader(fog->shader);
    }
    free(fog);
}

bool render_fog_draw(
    render_fog *fog,
    const visibility *visibility,
    const engine_viewport_camera *frame,
    const render_scene3d_camera *camera,
    float time
)
{
    Fog settings;
    const unsigned char *cells = visibility ? visibility_cells(visibility, &settings)
                                            : NULL;
    if (!cells) {
        return true;
    }
    if (!fog->shader.id) {
        char *source = render_shader_source("fog.shd");
        if (!source) {
            return false;
        }
        fog->shader = cf_make_draw_shader_from_source(source);
        free(source);
        if (!fog->shader.id) {
            return false;
        }
    }
    if (fog->width != settings.columns || fog->height != settings.rows) {
        if (fog->mask.id) {
            cf_destroy_texture(fog->mask);
        }
        CF_TextureParams params = cf_texture_defaults(settings.columns, settings.rows);
        params.filter = CF_FILTER_LINEAR;
        params.stream = true;
        fog->mask = cf_make_texture(params);
        if (!fog->mask.id) {
            return false;
        }
        fog->width = settings.columns;
        fog->height = settings.rows;
    }
    cf_texture_update(fog->mask, (void *)cells, settings.columns * settings.rows * 4);
    CF_M4x4 rays = camera ? render_scene3d_eye_rays(camera) : cf_m4_identity();
    CF_V3 eye = camera ? camera->eye : (CF_V3){ 0 };
    unsigned rgba = settings.color ? settings.color : 0x07111AFF;
    CF_Color color = { (rgba >> 24) / 255.0f,
                       ((rgba >> 16) & 255) / 255.0f,
                       ((rgba >> 8) & 255) / 255.0f,
                       1 };
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_push_shader(fog->shader);
    cf_draw_set_texture("visibility_mask", fog->mask);
    cf_draw_set_uniform("eye_rays", &rays, CF_UNIFORM_TYPE_MAT4, 1);
    cf_draw_set_uniform("eye", &eye, CF_UNIFORM_TYPE_FLOAT3, 1);
    cf_draw_set_uniform_v2("grid_origin", settings.origin);
    cf_draw_set_uniform_v2(
        "grid_extent",
        (CF_V2){ settings.columns * settings.cell_size, settings.rows * settings.cell_size }
    );
    cf_draw_set_uniform_v2("view_center", (CF_V2){ frame->center_x, -frame->center_y });
    cf_draw_set_uniform_v2("view_span", (CF_V2){ frame->span_width, frame->span_height });
    cf_draw_set_uniform_int("perspective", camera != NULL);
    cf_draw_set_uniform_float("fog_time", time * settings.drift);
    cf_draw_set_uniform_color("fog_color", color);
    cf_draw_quad_fill((CF_Aabb){ { -1, -1 }, { 1, 1 } }, 0);
    RENDER_DRAW_UNBIND_TEXTURES("visibility_mask");
    cf_draw_pop_shader();
    cf_draw_pop();
    return true;
}
