#include "render/scene3d_cascades.h"

#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/shader_program.h"
#include "runtime/geometry/terrain_volume.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* Frame pixels between the first cascade's probes, and the side of each
     * probe's octahedral map of directions there. Each cascade above doubles
     * both, so every cascade holds as many rays. */
    PROBE_SPACING = 8,
    PROBE_SIDE = 4,
    CASCADES = 5,
    /* Samples along each ray's interval. */
    STEPS = 8,
};

/* The share of each new frame in the probes' accumulated light: about the
 * last ten frames count. */
static const float new_share = .1f;

/* The frame number seeds each ray's jitter; it wraps well inside a float's
 * exact integers. */
#define FRAME_PERIOD (1 << 20)

/* World tracing's emission grid: cells per side in rock cells, and cells
 * either side of the play plane. */
enum
{
    LIGHT_CELL_SCALE = 2,
    LIGHT_REACH = 4
};

typedef struct
{
    CF_V3 position;
    float radius;
    float emission[3];
} light_source;

/* A volume laid out as tiles of its layers: cells across, down and deep, and
 * tiles across. */
typedef struct
{
    CF_Texture texture;
    int across, down, deep, tiles;
} atlas;

struct render_scene3d_cascades
{
    ScreenLight mode;
    int width, height;
    /* Cascades trace and merge top-down, each reading the one above; the
     * probes' light accumulates in turns between two targets. */
    CF_Canvas cascade[2], probes, accumulated[2], result;
    int turn, frame;
    bool restart;
    CF_Texture black;
    CF_Shader trace, gather, filter, resolve;
    CF_Material material;
    CF_Sampler point;
    CF_Mesh triangle;
    CF_M4x4 last_clip;
    bool has_last;
    /* World tracing: the frame's terrain volume and where it stands, its
     * carved density on its coarse lattice, and the frame's emitters. */
    materials_depth_volume view;
    float elevation;
    CF_V2 origin;
    uint64_t rock_identity, rock_revision;
    atlas rock, light;
    /* The rock atlas's texels and the carve they were laid from. */
    float *rock_texels, *rock_removed;
    float *light_cells; /* array.h vector */
    light_source *sources;
    const CF_M4x4 *solids;
    int solid_count;
    CF_Sampler linear;
};

render_scene3d_cascades *render_scene3d_cascades_new()
{
    return xcalloc(1, sizeof(render_scene3d_cascades));
}

static void free_targets(render_scene3d_cascades *cascades)
{
    if (!cascades->width) {
        return;
    }
    cf_destroy_canvas(cascades->cascade[0]);
    cf_destroy_canvas(cascades->cascade[1]);
    cf_destroy_canvas(cascades->probes);
    cf_destroy_canvas(cascades->accumulated[0]);
    cf_destroy_canvas(cascades->accumulated[1]);
    cf_destroy_canvas(cascades->result);
    cascades->width = cascades->height = 0;
}

void render_scene3d_cascades_free(render_scene3d_cascades *cascades)
{
    free_targets(cascades);
    if (cascades->trace.id) {
        cf_destroy_shader(cascades->trace);
        cf_destroy_shader(cascades->gather);
        cf_destroy_shader(cascades->filter);
        cf_destroy_shader(cascades->resolve);
        cf_destroy_material(cascades->material);
        cf_destroy_sampler(cascades->point);
        cf_destroy_mesh(cascades->triangle);
        cf_destroy_texture(cascades->black);
        cf_destroy_sampler(cascades->linear);
    }
    if (cascades->rock.texture.id) {
        cf_destroy_texture(cascades->rock.texture);
    }
    if (cascades->light.texture.id) {
        cf_destroy_texture(cascades->light.texture);
    }
    afree(cascades->light_cells);
    free(cascades->rock_texels);
    free(cascades->rock_removed);
    afree(cascades->sources);
    free(cascades);
}

static CF_Canvas make_target(int width, int height)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = CF_FILTER_NEAREST;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_canvas(params);
}

/* Probes along an axis of size pixels at cascade i. */
static int probes(int size, int i)
{
    int spacing = PROBE_SPACING << i;
    return (size + spacing - 1) / spacing;
}

/* Texels of cascade i along an axis of size pixels: a map per probe. */
static int texels(int size, int i)
{
    return probes(size, i) * (PROBE_SIDE << i);
}

static void resize(render_scene3d_cascades *cascades, int width, int height)
{
    if (cascades->width == width && cascades->height == height) {
        return;
    }
    free_targets(cascades);
    int across = 0, down = 0;
    for (int i = 0; i < CASCADES; i++) {
        across = texels(width, i) > across ? texels(width, i) : across;
        down = texels(height, i) > down ? texels(height, i) : down;
    }
    cascades->cascade[0] = make_target(across, down);
    cascades->cascade[1] = make_target(across, down);
    cascades->probes = make_target(probes(width, 0), probes(height, 0));
    cascades->accumulated[0] = make_target(probes(width, 0), probes(height, 0));
    cascades->accumulated[1] = make_target(probes(width, 0), probes(height, 0));
    cascades->result = make_target(width, height);
    cascades->width = width;
    cascades->height = height;
    cascades->has_last = false;
    cascades->restart = true;
}

bool render_scene3d_cascades_prepare(
    render_scene3d_cascades *cascades,
    const Environment *environment,
    int width,
    int height,
    char *error,
    size_t size
)
{
    ScreenLight mode = environment->screen_light;
    if ((unsigned)mode > SCREEN_LIGHT_WORLD) {
        cascades->mode = SCREEN_LIGHT_OFF;
        snprintf(error, size, "Environment screen light must be Off, Screen or World.");
        return false;
    }
    if (mode != SCREEN_LIGHT_OFF && environment->occlusion == OCCLUSION_OFF) {
        cascades->mode = SCREEN_LIGHT_OFF;
        snprintf(error, size, "Environment screen light needs occlusion on: its probes stand on the occlusion's depth.");
        return false;
    }
    /* Light gathered in another mode no longer holds. */
    cascades->restart |= mode != cascades->mode;
    cascades->mode = mode;
    if (mode == SCREEN_LIGHT_OFF) {
        free_targets(cascades);
        return true;
    }
    if (width > 0 && height > 0) {
        resize(cascades, width, height);
    }
    return true;
}

void render_scene3d_cascades_reset(render_scene3d_cascades *cascades)
{
    cascades->restart = true;
    cascades->has_last = false;
}

bool render_scene3d_cascades_active(const render_scene3d_cascades *cascades)
{
    return cascades->mode != SCREEN_LIGHT_OFF;
}

bool render_scene3d_cascades_traces_world(const render_scene3d_cascades *cascades)
{
    return cascades->mode == SCREEN_LIGHT_WORLD;
}

void render_scene3d_cascades_clear_world(render_scene3d_cascades *cascades)
{
    cascades->view = (materials_depth_volume){ 0 };
    aclear(cascades->sources);
    cascades->solids = NULL;
    cascades->solid_count = 0;
}

void render_scene3d_cascades_set_solids(render_scene3d_cascades *cascades, const CF_M4x4 *boxes, int count)
{
    cascades->solids = boxes;
    cascades->solid_count = count;
}

void render_scene3d_cascades_set_rock(
    render_scene3d_cascades *cascades,
    materials_depth_volume view,
    float elevation,
    CF_V2 origin
)
{
    cascades->view = view;
    cascades->elevation = elevation;
    cascades->origin = origin;
}

void render_scene3d_cascades_add_emitter(
    render_scene3d_cascades *cascades,
    CF_V3 position,
    float radius,
    const float emission[3]
)
{
    if (!render_scene3d_cascades_traces_world(cascades) ||
        emission[0] + emission[1] + emission[2] <= 0) {
        return;
    }
    apush(
        cascades->sources,
        ((light_source){ position, radius, { emission[0], emission[1], emission[2] } })
    );
}

static CF_V3 transform_point(CF_M4x4 m, CF_V3 p)
{
    CF_V4 q = cf_mul_m4_v4(m, (CF_V4){ p.x, p.y, p.z, 1 });
    return (CF_V3){ q.x, q.y, q.z };
}

/* Remakes an atlas's texture when its shape changes. */
static void shape_atlas(atlas *a, int across, int down, int deep, CF_PixelFormat format)
{
    int tiles = (int)ceilf(sqrtf((float)deep));
    if (a->texture.id && a->across == across && a->down == down && a->deep == deep) {
        return;
    }
    if (a->texture.id) {
        cf_destroy_texture(a->texture);
    }
    CF_TextureParams params = cf_texture_defaults(across * tiles, down * ((deep + tiles - 1) / tiles));
    params.pixel_format = format;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.stream = true;
    a->texture = cf_make_texture(params);
    *a = (atlas){ a->texture, across, down, deep, tiles };
}

static int atlas_width(const atlas *a)
{
    return a->across * a->tiles;
}

static int atlas_height(const atlas *a)
{
    return a->down * ((a->deep + a->tiles - 1) / a->tiles);
}

/* The world position of the volume's first coarse lattice point, and the
 * lattice's spacing. */
static CF_V4 rock_corner(const render_scene3d_cascades *cascades)
{
    const terrain_volume *v = cascades->view.volume;
    const field_grid *p = &v->plane;
    return (
        CF_V4
    ){ cascades->origin.x + p->center_x + (.5f - p->width * .5f) * p->cell_size,
       cascades->elevation + v->front * p->cell_size,
       cascades->origin.y + p->center_y + (.5f - p->height * .5f) * p->cell_size,
       p->cell_size * v->refine };
}

/* Lays the carved density on the volume's coarse lattice points
 * [x0, x1) x [y0, y1) x [z0, z1) into the rock atlas's texels. */
static void lay_rock(render_scene3d_cascades *cascades, terrain_volume_bricks points)
{
    const terrain_volume *v = cascades->view.volume;
    const atlas *rock = &cascades->rock;
    int r = v->refine, width = atlas_width(rock);
    for (int k = points.z0; k < points.z1; k++) {
        int x0 = k % rock->tiles * rock->across, y0 = k / rock->tiles * rock->down;
        for (int j = points.y0; j < points.y1; j++) {
            for (int i = points.x0; i < points.x1; i++) {
                cascades->rock_texels[(size_t)(y0 + j) * width + x0 + i] = terrain_volume_density(
                    v,
                    cascades->view.removed,
                    i * r,
                    j * r,
                    k * r
                );
            }
        }
    }
}

/* Keeps the rock atlas on the carve: the whole lattice for a new volume,
 * then only the points a change of the carve can flip. */
static void update_rock(render_scene3d_cascades *cascades)
{
    const terrain_volume *v = cascades->view.volume;
    if (cascades->rock_identity == cascades->view.identity &&
        cascades->rock_revision == cascades->view.revision) {
        return;
    }
    int r = v->refine;
    int across = (v->plane.width - 1) / r + 1,
        down = (v->plane.height - 1) / r + 1, deep = (v->layers - 1) / r + 1;
    size_t samples = (size_t)v->plane.width * v->plane.height;
    terrain_volume_bricks points;
    if (cascades->rock_identity != cascades->view.identity) {
        shape_atlas(&cascades->rock, across, down, deep, CF_PIXEL_FORMAT_R32_FLOAT);
        free(cascades->rock_texels);
        free(cascades->rock_removed);
        cascades->rock_texels = xcalloc(
            (size_t)atlas_width(&cascades->rock) * atlas_height(&cascades->rock),
            sizeof *cascades->rock_texels
        );
        cascades->rock_removed = xmalloc(samples * sizeof *cascades->rock_removed);
        lay_rock(cascades, (terrain_volume_bricks){ 0, 0, 0, across, down, deep });
    } else if (
        terrain_volume_changed_lattice(v, cascades->rock_removed, cascades->view.removed, &points)
    ) {
        lay_rock(cascades, points);
    }
    memcpy(
        cascades->rock_removed,
        cascades->view.removed,
        samples * sizeof *cascades->rock_removed
    );
    cf_texture_update(
        cascades->rock.texture,
        cascades->rock_texels,
        (int)((size_t)atlas_width(&cascades->rock) *
              atlas_height(&cascades->rock) * sizeof *cascades->rock_texels)
    );
    cascades->rock_identity = cascades->view.identity;
    cascades->rock_revision = cascades->view.revision;
}

/* The emission grid's first cell's world position and its spacing, over the
 * rock's plane and LIGHT_REACH cells either side of the play plane. */
static CF_V4 light_corner(const render_scene3d_cascades *cascades)
{
    CF_V4 rock = rock_corner(cascades);
    float cell = rock.w * LIGHT_CELL_SCALE;
    return (CF_V4){ rock.x, cascades->elevation + LIGHT_REACH * cell, rock.z, cell };
}

/* Spreads each emitter's light over the eight cells around it: a sphere of
 * radius r whose chord shows its emission holds e * 2/3 pi r^2 of light. */
static void update_light(render_scene3d_cascades *cascades)
{
    const atlas *rock = &cascades->rock;
    int across = (rock->across - 1) / LIGHT_CELL_SCALE + 2,
        down = (rock->down - 1) / LIGHT_CELL_SCALE + 2, deep = 2 * LIGHT_REACH + 1;
    shape_atlas(&cascades->light, across, down, deep, CF_PIXEL_FORMAT_R32G32B32A32_FLOAT);
    int width = atlas_width(&cascades->light), height = atlas_height(&cascades->light);
    asetlen(cascades->light_cells, width * height * 4);
    memset(
        cascades->light_cells,
        0,
        (size_t)acount(cascades->light_cells) * sizeof *cascades->light_cells
    );
    CF_V4 corner = light_corner(cascades);
    float volume = corner.w * corner.w * corner.w;
    for (int n = 0; n < acount(cascades->sources); n++) {
        const light_source *s = &cascades->sources[n];
        float g[3] = { (s->position.x - corner.x) / corner.w,
                       (s->position.z - corner.z) / corner.w,
                       (corner.y - s->position.y) / corner.w };
        int base[3] = { (int)floorf(g[0]), (int)floorf(g[1]), (int)floorf(g[2]) };
        float f[3] = { g[0] - base[0], g[1] - base[1], g[2] - base[2] };
        float amount = 2.0f / 3 * CF_PI * s->radius * s->radius / volume;
        for (int c = 0; c < 8; c++) {
            int i = base[0] + (c & 1), j = base[1] + (c >> 1 & 1),
                k = base[2] + (c >> 2);
            if (i < 0 || j < 0 || k < 0 || i >= across || j >= down || k >= deep) {
                continue;
            }
            float w = (c & 1 ? f[0] : 1 - f[0]) * (c >> 1 & 1 ? f[1] : 1 - f[1]) *
                      (c >> 2 ? f[2] : 1 - f[2]);
            int x = k % cascades->light.tiles * across + i,
                y = k / cascades->light.tiles * down + j;
            float *cell = cascades->light_cells + ((size_t)y * width + x) * 4;
            for (int ch = 0; ch < 3; ch++) {
                cell[ch] += s->emission[ch] * amount * w;
            }
        }
    }
    /* Solids fill the cells whose centers they hold, in alpha. */
    for (int n = 0; n < cascades->solid_count; n++) {
        CF_M4x4 box = cascades->solids[n];
        CF_M4x4 local = cf_m4_invert(box);
        CF_V3 low = { INFINITY, INFINITY, INFINITY },
              high = { -INFINITY, -INFINITY, -INFINITY };
        for (int c = 0; c < 8; c++) {
            CF_V3 p = transform_point(
                box,
                (CF_V3){ c & 1 ? .5f : -.5f, c & 2 ? .5f : -.5f, c & 4 ? .5f : -.5f }
            );
            low = cf_min_v3(low, p);
            high = cf_max_v3(high, p);
        }
        int i0 = (int)ceilf((low.x - corner.x) / corner.w),
            i1 = (int)floorf((high.x - corner.x) / corner.w);
        int j0 = (int)ceilf((low.z - corner.z) / corner.w),
            j1 = (int)floorf((high.z - corner.z) / corner.w);
        int k0 = (int)ceilf((corner.y - high.y) / corner.w),
            k1 = (int)floorf((corner.y - low.y) / corner.w);
        for (int k = k0 < 0 ? 0 : k0; k <= k1 && k < deep; k++) {
            for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < down; j++) {
                for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < across; i++) {
                    CF_V3 center = { corner.x + i * corner.w,
                                     corner.y - k * corner.w,
                                     corner.z + j * corner.w };
                    CF_V3 q = transform_point(local, center);
                    if (fabsf(q.x) > .5f || fabsf(q.y) > .5f || fabsf(q.z) > .5f) {
                        continue;
                    }
                    int x = k % cascades->light.tiles * across + i,
                        y = k / cascades->light.tiles * down + j;
                    cascades->light_cells[((size_t)y * width + x) * 4 + 3] = 1;
                }
            }
        }
    }
    cf_texture_update(
        cascades->light.texture,
        cascades->light_cells,
        (int)((size_t)width * height * 4 * sizeof *cascades->light_cells)
    );
}

static bool load_passes(render_scene3d_cascades *cascades, char *error, size_t size)
{
    if (cascades->trace.id) {
        return true;
    }
    const char *names[4] = { "scene3d_cascades_trace.frag",
                             "scene3d_cascades_gather.frag",
                             "scene3d_cascades_filter.frag",
                             "scene3d_cascades_resolve.frag" };
    CF_Shader shaders[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        shaders[i] = render_pass_shader(names[i], error, size);
        if (!shaders[i].id) {
            for (int k = 0; k < i; k++) {
                cf_destroy_shader(shaders[k]);
            }
            return false;
        }
    }
    cascades->trace = shaders[0];
    cascades->gather = shaders[1];
    cascades->filter = shaders[2];
    cascades->resolve = shaders[3];
    cascades->material = cf_make_material();
    CF_SamplerParams point = cf_sampler_defaults();
    point.filter = CF_FILTER_NEAREST;
    point.mip_filter = CF_MIP_FILTER_NEAREST;
    point.wrap_u = point.wrap_v = point.wrap_w = CF_WRAP_MODE_CLAMP_TO_EDGE;
    cascades->point = cf_make_sampler(point);
    CF_SamplerParams linear = point;
    linear.filter = CF_FILTER_LINEAR;
    cascades->linear = cf_make_sampler(linear);
    cascades->triangle = render_make_pass_mesh();
    CF_TextureParams params = cf_texture_defaults(1, 1);
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_NEAREST;
    cascades->black = cf_make_texture(params);
    CF_Pixel black = { .val = 0xff000000u };
    cf_texture_update(cascades->black, &black, sizeof black);
    return true;
}

/* Runs one pass over the first width x height texels of a target. */
static void pass(render_scene3d_cascades *cascades, CF_Canvas target, CF_Shader shader, int width, int height)
{
    cf_apply_canvas(target, true);
    cf_apply_mesh(cascades->triangle);
    cf_apply_shader(shader, cascades->material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

static void set_texture(render_scene3d_cascades *cascades, const char *name, CF_Texture texture)
{
    cf_material_set_texture_fs_sampler(
        cascades->material,
        name,
        texture.id ? texture : cascades->black,
        cascades->point
    );
}

static void set_vector(render_scene3d_cascades *cascades, const char *name, CF_V4 value)
{
    cf_material_set_uniform_fs(cascades->material, name, &value, CF_UNIFORM_TYPE_FLOAT4, 1);
}

static void set_matrix(render_scene3d_cascades *cascades, const char *name, CF_M4x4 value)
{
    cf_material_set_uniform_fs(cascades->material, name, &value, CF_UNIFORM_TYPE_MAT4, 1);
}

/* The probes and maps of cascade i, as the shaders read them. */
static CF_V4 cascade_shape(const render_scene3d_cascades *cascades, int i)
{
    if (i >= CASCADES) {
        return (CF_V4){ 0 };
    }
    return (CF_V4){ (float)(PROBE_SPACING << i),
                    (float)(PROBE_SIDE << i),
                    (float)probes(cascades->width, i),
                    (float)probes(cascades->height, i) };
}

/* The camera's transforms, and whether the last frame's color holds: none
 * before the first frame or after a resize or reset, when it was never
 * drawn at this size. */
static void set_camera(render_scene3d_cascades *cascades, const render_scene3d_camera *camera, CF_Texture history)
{
    CF_M4x4 clip = cf_mul_m4(camera->projection, camera->view);
    /* Probes stand on the screen, so a moved camera puts them on other
     * surfaces. */
    bool restart = cascades->restart || !cascades->has_last ||
                   memcmp(&clip, &cascades->last_clip, sizeof clip);
    set_vector(
        cascades,
        "cascade_jitter",
        (CF_V4){ (float)cascades->frame, new_share, restart ? 1.0f : 0.0f, 0 }
    );
    cascades->frame = (cascades->frame + 1) % FRAME_PERIOD;
    cascades->restart = false;
    set_matrix(cascades, "cascade_unproject", cf_m4_invert(camera->projection));
    set_matrix(cascades, "cascade_to_world", cf_m4_invert(camera->view));
    set_matrix(cascades, "cascade_to_view", camera->view);
    set_matrix(cascades, "cascade_to_clip", clip);
    /* The last frame's color lies where last frame's camera saw it. */
    set_matrix(cascades, "cascade_to_history", cascades->has_last ? cascades->last_clip : clip);
    set_vector(
        cascades,
        "cascade_frame",
        (CF_V4){ (float)cascades->width,
                 (float)cascades->height,
                 history.id && cascades->has_last ? 1.0f : 0.0f,
                 (float)PROBE_SPACING }
    );
    cascades->last_clip = clip;
    cascades->has_last = true;
}

/* World tracing's rock and emitters, or black maps for screen tracing.
 * Returns whether the rays trace the world. */
static bool set_world(render_scene3d_cascades *cascades)
{
    bool world = cascades->mode == SCREEN_LIGHT_WORLD && cascades->view.volume;
    if (world) {
        update_rock(cascades);
        update_light(cascades);
        const atlas *rock = &cascades->rock, *light = &cascades->light;
        set_vector(cascades, "rock_corner", rock_corner(cascades));
        set_vector(
            cascades,
            "rock_cells",
            (CF_V4){ (float)rock->across,
                     (float)rock->down,
                     (float)rock->deep,
                     (float)rock->tiles }
        );
        set_vector(cascades, "light_corner", light_corner(cascades));
        set_vector(
            cascades,
            "light_cells",
            (CF_V4){ (float)light->across,
                     (float)light->down,
                     (float)light->deep,
                     (float)light->tiles }
        );
    }
    cf_material_set_texture_fs_sampler(
        cascades->material,
        "rock",
        world ? cascades->rock.texture : cascades->black,
        cascades->linear
    );
    cf_material_set_texture_fs_sampler(
        cascades->material,
        "light",
        world ? cascades->light.texture : cascades->black,
        cascades->linear
    );
    return world;
}

/* Traces and merges the cascades top-down. Each cascade's rays start where
 * the one below ends and reach four times as far, in the first cascade's
 * probe spacing on the probe's surface, so the cascades keep their shape at
 * any scale. */
static void trace(render_scene3d_cascades *cascades, bool world)
{
    float ends[CASCADES];
    for (int i = 0; i < CASCADES; i++) {
        ends[i] = (powf(4, (float)(i + 1)) - 1) / 3;
    }
    for (int i = CASCADES - 1; i >= 0; i--) {
        float start = i ? ends[i - 1] : 0;
        set_vector(cascades, "cascade_probe", cascade_shape(cascades, i));
        set_vector(cascades, "cascade_upper", cascade_shape(cascades, i + 1));
        set_vector(cascades, "cascade_ray", (CF_V4){ start, ends[i], (float)STEPS, world ? 2.0f : 1.0f });
        set_texture(cascades, "upper", cf_canvas_get_target(cascades->cascade[(i + 1) % 2]));
        pass(
            cascades,
            cascades->cascade[i % 2],
            cascades->trace,
            texels(cascades->width, i),
            texels(cascades->height, i)
        );
    }
}

bool render_scene3d_cascades_draw(
    render_scene3d_cascades *cascades,
    const render_scene3d_camera *camera,
    CF_Texture depth,
    CF_Texture occlusion,
    CF_Texture history,
    char *error,
    size_t size
)
{
    assert(render_scene3d_cascades_active(cascades));
    if (!load_passes(cascades, error, size)) {
        return false;
    }
    set_camera(cascades, camera, history);
    /* Every pass declares every map the cascades read. */
    CF_Canvas before = cascades->accumulated[cascades->turn];
    cascades->turn ^= 1;
    CF_Canvas after = cascades->accumulated[cascades->turn];
    set_texture(cascades, "depth_map", depth);
    set_texture(cascades, "history", history);
    set_texture(cascades, "occlusion_map", occlusion);
    set_texture(cascades, "previous", cf_canvas_get_target(before));
    trace(cascades, set_world(cascades));
    int across = probes(cascades->width, 0), down = probes(cascades->height, 0);
    set_vector(cascades, "cascade_probe", cascade_shape(cascades, 0));
    set_texture(cascades, "upper", cf_canvas_get_target(cascades->cascade[0]));
    pass(cascades, cascades->probes, cascades->gather, across, down);
    set_texture(cascades, "upper", cf_canvas_get_target(cascades->probes));
    pass(cascades, after, cascades->filter, across, down);
    set_texture(cascades, "upper", cf_canvas_get_target(after));
    pass(
        cascades,
        cascades->result,
        cascades->resolve,
        cascades->width,
        cascades->height
    );
    /* Later frames may free or resize what these passes read. */
    cf_material_clear_textures(cascades->material);
    return true;
}

CF_Texture render_scene3d_cascades_result(const render_scene3d_cascades *cascades)
{
    return cf_canvas_get_target(cascades->result);
}
