#include "render/medium.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

struct render_medium
{
    CF_Texture density, optical;
    CF_ComputeShader shader;
    CF_Material material;
    int columns, rows, width, height;
};

render_medium *render_medium_new()
{
    render_medium *m = xcalloc(1, sizeof *m);
    m->shader = cf_make_compute_shader("hrc/medium.shd");
    m->material = cf_make_material();
    return m;
}

void render_medium_free(render_medium *m)
{
    if (!m) {
        return;
    }
    if (m->density.id) {
        cf_destroy_texture(m->density);
    }
    if (m->optical.id) {
        cf_destroy_texture(m->optical);
    }
    if (m->shader.id) {
        cf_destroy_compute_shader(m->shader);
    }
    if (m->material.id) {
        cf_destroy_material(m->material);
    }
    free(m);
}

static bool resize(render_medium *m, GameMedium settings, int width, int height)
{
    if (m->columns != settings.columns || m->rows != settings.rows) {
        CF_TextureParams p = cf_texture_defaults(settings.columns, settings.rows);
        p.pixel_format = CF_PIXEL_FORMAT_R32_FLOAT;
        p.filter = CF_FILTER_LINEAR;
        p.stream = true;
        CF_Texture next = cf_make_texture(p);
        if (!next.id) {
            return false;
        }
        if (m->density.id) {
            cf_destroy_texture(m->density);
        }
        m->density = next;
        m->columns = settings.columns;
        m->rows = settings.rows;
    }
    if (m->width != width || m->height != height) {
        CF_TextureParams p = cf_texture_defaults(width, height);
        p.pixel_format = CF_PIXEL_FORMAT_R16_FLOAT;
        p.filter = CF_FILTER_LINEAR;
        p.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                  CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
        CF_Texture next = cf_make_texture(p);
        if (!next.id) {
            return false;
        }
        if (m->optical.id) {
            cf_destroy_texture(m->optical);
        }
        m->optical = next;
        m->width = width;
        m->height = height;
    }
    return true;
}

CF_Texture render_medium_draw(
    render_medium *m,
    const game_medium *field,
    render_material_textures textures,
    render_material_view view,
    int width,
    int height
)
{
    GameMedium settings;
    const float *density = game_medium_cells(field, &settings);
    assert(density && width > 0 && height > 0);
    if (!m->shader.id || !m->material.id || !resize(m, settings, width, height)) {
        return (CF_Texture){ 0 };
    }
    cf_texture_update(m->density, (void *)density, settings.columns * settings.rows * (int)sizeof(float));
    float camera[] = { view.min_x, -view.max_y, view.width / width, view.height / height };
    float domain[] = { settings.origin.x,
                       settings.origin.y,
                       settings.columns * settings.cell_size,
                       settings.rows * settings.cell_size };
    cf_material_set_uniform_cs(m->material, "u_view", camera, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_material_set_uniform_cs(m->material, "u_domain", domain, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_material_set_uniform_cs(m->material, "u_extinction", &settings.extinction, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_texture_cs(m->material, "u_density", m->density);
    CF_Texture images[] = { textures.absorption, m->optical, textures.emission };
    CF_ComputeDispatch
        dispatch = cf_compute_dispatch_defaults((width + 15) / 16, (height + 15) / 16, 1);
    dispatch.rw_textures = images;
    dispatch.rw_texture_count = 3;
    cf_push_gpu_label("Persistent medium raster");
    cf_dispatch_compute(m->shader, m->material, dispatch);
    cf_pop_gpu_label();
    return m->optical;
}
