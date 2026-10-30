#include "render/scene3d_trail.h"

#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    TRAIL_SOURCES = 64,
    TRAIL_POINTS = 32,
    TRAIL_SUBDIVISIONS = 4,
    TRAIL_SEGMENTS = TRAIL_SOURCES * TRAIL_POINTS * TRAIL_SUBDIVISIONS,
    TRAIL_VERTICES = 2 * (TRAIL_SEGMENTS + TRAIL_SOURCES),
    TRAIL_INDICES = 6 * TRAIL_SEGMENTS
};
typedef struct
{
    CF_V3 position;
    double time;
} trail_point;
typedef struct
{
    ecs_entity entity;
    Trail style;
    trail_point points[TRAIL_POINTS];
    int count;
    trail_point head;
    double sampled;
} trail_source;

typedef struct
{
    CF_V3 position;
    CF_V4 color, edge; /* Across ribbon, distance from each cap, emission. */
} trail_vertex;

typedef struct
{
    int first, count;
} trail_span;

typedef struct
{
    float depth;
    int vertex;
} ribbon_quad;

typedef struct
{
    CF_V3 origin;
    int first, count;
} ribbon_range;

struct scene3d_trails
{
    const ecs_world *world;
    double time, simulation_time;
    trail_source sources[TRAIL_SOURCES];
    scene3d_trail_segment segments[TRAIL_SEGMENTS];
    int count;
    bool reported_capacity;
    trail_span spans[TRAIL_SOURCES];
    int span_count;
    CF_Mesh mesh;
    CF_Shader shader;
    trail_vertex *vertices;
    uint16_t *indices;
    ribbon_quad *quads;
    int vertex_count, index_count;
    ribbon_range ranges[TRAIL_SOURCES];
    int range_count;
};

scene3d_trails *render_scene3d_trails_new()
{
    return xcalloc(1, sizeof(scene3d_trails));
}
void render_scene3d_trails_free(scene3d_trails *trails)
{
    if (trails->mesh.id) {
        cf_destroy_mesh(trails->mesh);
    }
    if (trails->shader.id) {
        cf_destroy_shader(trails->shader);
    }
    free(trails->vertices);
    free(trails->indices);
    free(trails->quads);
    free(trails);
}
void render_scene3d_trails_reset(scene3d_trails *trails)
{
    trails->world = NULL;
    trails->time = trails->simulation_time = 0;
    memset(trails->sources, 0, sizeof trails->sources);
    trails->count = trails->span_count = 0;
    trails->vertex_count = trails->index_count = trails->range_count = 0;
    trails->reported_capacity = false;
}

static trail_source *source_for(scene3d_trails *trails, ecs_entity entity)
{
    trail_source *oldest = &trails->sources[0];
    for (int i = 0; i < TRAIL_SOURCES; i++) {
        trail_source *source = &trails->sources[i];
        if (source->count &&
            ecs_entity_to_u64(source->entity) == ecs_entity_to_u64(entity)) {
            return source;
        }
        if (!source->count ||
            (oldest->count && source->points[source->count - 1].time <
                                  oldest->points[oldest->count - 1].time)) {
            oldest = source;
        }
    }
    if (oldest->count && !trails->reported_capacity) {
        fprintf(stderr, "Visual trails: %d-source budget full; replacing the oldest trail.\n", TRAIL_SOURCES);
        trails->reported_capacity = true;
    }
    *oldest = (trail_source){ .entity = entity };
    return oldest;
}

static void append(trail_source *source, trail_point point)
{
    if (source->count == TRAIL_POINTS) {
        memmove(
            source->points,
            source->points + 1,
            (TRAIL_POINTS - 1) * sizeof *source->points
        );
        source->count--;
    }
    source->points[source->count++] = point;
}

static void record(trail_source *source, CF_V3 position, double time)
{
    trail_point head = { position, time };
    if (!source->count) {
        append(source, head);
        source->head = head;
        source->sampled = time;
        return;
    }
    const double interval = 1.0 / 60;
    if (time > source->head.time) {
        /* Commit history on a fixed clock; intermediate renders only move the
         * attached head. Cap work after long gaps to the retained point budget. */
        double next = source->sampled + interval;
        if (next < time - (TRAIL_POINTS - 1) * interval) {
            next += floor((time - next) / interval - (TRAIL_POINTS - 1)) * interval;
        }
        for (; next <= time + 1e-9; next += interval) {
            float t = (float)
                fmin(1, (next - source->head.time) / (time - source->head.time));
            CF_V3 point = cf_add_v3(
                source->head.position,
                cf_mul_v3_f(cf_sub_v3(position, source->head.position), t)
            );
            CF_V3 delta = cf_sub_v3(point, source->points[source->count - 1].position);
            if (cf_dot_v3(delta, delta) > .0001f) {
                append(source, (trail_point){ point, next });
            }
            source->sampled = next;
        }
    }
    source->head = head;
}

static CF_V3 curve(CF_V3 a, CF_V3 b, CF_V3 tangent_a, CF_V3 tangent_b, float t)
{
    float t2 = t * t, t3 = t2 * t;
    return cf_add_v3(
        cf_add_v3(cf_mul_v3_f(a, 2 * t3 - 3 * t2 + 1), cf_mul_v3_f(b, -2 * t3 + 3 * t2)),
        cf_add_v3(cf_mul_v3_f(tangent_a, t3 - 2 * t2 + t), cf_mul_v3_f(tangent_b, t3 - t2))
    );
}

static void segments_for(scene3d_trails *trails, const trail_source *source, double time)
{
    if (!source->count) {
        return;
    }
    trail_point points[TRAIL_POINTS + 1];
    int count = source->count;
    memcpy(points, source->points, (size_t)count * sizeof *points);
    CF_V3 gap = cf_sub_v3(source->head.position, points[count - 1].position);
    if (cf_dot_v3(gap, gap) > .0001f) {
        points[count++] = source->head;
    } else {
        points[count - 1] = source->head;
    }
    int first = trails->count;
    for (int j = 1; j < count; j++) {
        trail_point a = points[j - 1], b = points[j];
        CF_V3 previous = points[j > 1 ? j - 2 : j - 1].position;
        CF_V3 following = points[j + 1 < count ? j + 1 : j].position;
        CF_V3 tangent_a = cf_mul_v3_f(cf_sub_v3(b.position, previous), .5f);
        CF_V3 tangent_b = cf_mul_v3_f(cf_sub_v3(following, a.position), .5f);
        CF_V3 from = a.position;
        for (int k = 1; k <= TRAIL_SUBDIVISIONS; k++) {
            CF_V3 to = k == TRAIL_SUBDIVISIONS
                         ? b.position
                         : curve(a.position, b.position, tangent_a, tangent_b, (float)k / TRAIL_SUBDIVISIONS);
            double born = a.time + (b.time - a.time) * (k - .5) / TRAIL_SUBDIVISIONS;
            float fade = (float)fmax(0, 1 - (time - born) / source->style.lifetime);
            unsigned color = source->style.color;
            trails->segments[trails->count++] = (scene3d_trail_segment){
                .from = from,
                .to = to,
                .color = { (color >> 24) / 255.0f,
                           ((color >> 16) & 255u) / 255.0f,
                           ((color >> 8) & 255u) / 255.0f,
                           (color & 255u) / 255.0f * fade * fade },
                .width = source->style.width * (.15f + .85f * fade),
                .emission = source->style.emission,
            };
            from = to;
        }
    }
    if (trails->count > first) {
        trails->spans[trails->span_count++] = (trail_span){ first, trails->count - first };
    }
}

void render_scene3d_trails_collect(scene3d_trails *trails, ecs_world *world, const engine *runtime, double time)
{
    double simulation_time = time;
    if (runtime) {
        const engine_frame_timing *timing = engine_last_timing(runtime);
        simulation_time = timing->simulation_time;
        time = fmax(0, simulation_time - (1 - timing->alpha) * ENGINE_FIXED_SECONDS);
    }
    if (trails->world != world || simulation_time < trails->simulation_time) {
        render_scene3d_trails_reset(trails);
    }
    time = fmax(time, trails->time);
    trails->world = world;
    trails->simulation_time = simulation_time;
    trails->time = time;
    trails->count = 0;
    trails->span_count = 0;
    for (int i = 0; i < TRAIL_SOURCES; i++) {
        trail_source *source = &trails->sources[i];
        int expired = 0;
        while (expired < source->count &&
               time - source->points[expired].time >= source->style.lifetime) {
            expired++;
        }
        source->count -= expired;
        memmove(
            source->points,
            source->points + expired,
            (size_t)source->count * sizeof *source->points
        );
    }
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_Trail.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!engine_entity_visible(runtime, world, entity)) {
                continue;
            }
            Transform position = engine_render_transform(runtime, world, entity).position;
            const Geometry3D *shape = Geometry3D_get_const(world, entity);
            float elevation = shape ? shape->elevation : 0;
            Trail style = *Trail_get_const(world, entity);
            if (!isfinite(
                    position.x + position.y + elevation + style.width +
                    style.lifetime + style.emission
                ) ||
                style.width <= 0 || style.lifetime <= 0 || style.emission < 0 ||
                !(style.color & 255u)) {
                continue;
            }
            trail_source *source = source_for(trails, entity);
            source->style = style;
            record(source, (CF_V3){ position.x, elevation, position.y }, time);
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < TRAIL_SOURCES; i++) {
        segments_for(trails, &trails->sources[i], time);
    }
}

static bool prepare_resources(scene3d_trails *trails, char *error, size_t size)
{
    if (!trails->shader.id) {
        char *vertex = render_shader_source("scene3d_trail.vert");
        char *fragment = render_shader_source("scene3d_trail.frag");
        if (vertex && fragment) {
            trails->shader = cf_make_shader_from_source(vertex, fragment);
        }
        if (!trails->shader.id) {
            snprintf(
                error,
                size,
                "Trail shader: %s",
                vertex && fragment ? cf_shader_compile_error() : "cannot read ribbon shader sources"
            );
        }
        free(vertex);
        free(fragment);
        if (!trails->shader.id) {
            return false;
        }
    }
    if (!trails->mesh.id) {
        CF_VertexAttribute attributes[] = {
            { .name = "in_pos",
              .format = CF_VERTEX_FORMAT_FLOAT3,
              .offset = offsetof(trail_vertex, position) },
            { .name = "in_color",
              .format = CF_VERTEX_FORMAT_FLOAT4,
              .offset = offsetof(trail_vertex, color) },
            { .name = "in_edge",
              .format = CF_VERTEX_FORMAT_FLOAT4,
              .offset = offsetof(trail_vertex, edge) },
        };
        trails->mesh = cf_make_mesh(TRAIL_VERTICES * sizeof(trail_vertex), attributes, 3, sizeof(trail_vertex));
        cf_mesh_set_index_buffer(trails->mesh, TRAIL_INDICES * sizeof(uint16_t), 16);
        trails->vertices = xmalloc(TRAIL_VERTICES * sizeof *trails->vertices);
        trails->indices = xmalloc(TRAIL_INDICES * sizeof *trails->indices);
        trails->quads = xmalloc(TRAIL_SEGMENTS * sizeof *trails->quads);
    }
    return true;
}

static int compare_quads(const void *left, const void *right)
{
    const ribbon_quad *a = left, *b = right;
    if (a->depth != b->depth) {
        return a->depth > b->depth ? -1 : 1;
    }
    return (a->vertex > b->vertex) - (a->vertex < b->vertex);
}

static CF_V3 ribbon_side(CF_V3 direction, CF_V3 view, CF_V3 fallback)
{
    CF_V3 side = cf_cross_v3(direction, view);
    return cf_dot_v3(side, side) > 1e-10f ? cf_norm_v3(side) : fallback;
}

bool render_scene3d_trails_prepare(
    scene3d_trails *trails,
    const render_scene3d_camera *camera,
    char *error,
    size_t size
)
{
    trails->vertex_count = trails->index_count = trails->range_count = 0;
    if (!trails->count) {
        return true;
    }
    if (!prepare_resources(trails, error, size)) {
        return false;
    }
    CF_M4x4 inverse = cf_m4_invert(camera->view);
    CF_V3 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14] };
    CF_V3 facing = { inverse.elements[8], inverse.elements[9], inverse.elements[10] };
    CF_V3 right = { inverse.elements[0], inverse.elements[1], inverse.elements[2] };
    bool perspective = fabsf(camera->projection.elements[15]) < .5f;
    ribbon_quad *quads = trails->quads;
    for (int span = 0; span < trails->span_count; span++) {
        trail_span range = trails->spans[span];
        const scene3d_trail_segment *segments = trails->segments + range.first;
        float total = 0, distance = 0;
        for (int i = 0; i < range.count; i++) {
            total += cf_distance_v3(segments[i].from, segments[i].to);
        }
        if (total < .001f) {
            continue;
        }
        int start = trails->vertex_count, quad_count = 0;
        CF_V3 center_sum = { 0 };
        for (int i = 0; i <= range.count; i++) {
            const scene3d_trail_segment *before = &segments[i ? i - 1 : 0];
            const scene3d_trail_segment *after = &segments[i < range.count ? i : i - 1];
            CF_V3 point = i ? before->to : after->from;
            CF_V3 view = perspective ? cf_sub_v3(eye, point) : facing;
            CF_V3 a = ribbon_side(cf_sub_v3(before->to, before->from), view, right);
            CF_V3 b = ribbon_side(cf_sub_v3(after->to, after->from), view, a);
            CF_V3 side = cf_add_v3(a, b);
            side = cf_dot_v3(side, side) > .0001f ? cf_norm_v3(side) : b;
            /* Shared miter sections close every join. Limit sharp-turn growth
             * to twice the half-width rather than creating unbounded spikes. */
            float width = (before->width + after->width) * .5f;
            CF_V3 offset = cf_mul_v3_f(side, width * .5f / fmaxf(.5f, fabsf(cf_dot_v3(side, b))));
            CF_V4 color = { (before->color.x + after->color.x) * .5f,
                            (before->color.y + after->color.y) * .5f,
                            (before->color.z + after->color.z) * .5f,
                            (before->color.w + after->color.w) * .5f };
            float emission = (before->emission + after->emission) * .5f;
            if (i) {
                distance += cf_distance_v3(before->from, before->to);
            }
            for (int edge = 0; edge < 2; edge++) {
                float sign = edge ? 1 : -1;
                trails->vertices[trails->vertex_count++] = (trail_vertex){
                    .position = cf_add_v3(point, cf_mul_v3_f(offset, sign)),
                    .color = color,
                    .edge = { sign, distance, fmaxf(0, total - distance), emission }
                };
            }
            center_sum = cf_add_v3(center_sum, point);
            if (i) {
                CF_V3 middle = cf_mul_v3_f(cf_add_v3(before->from, before->to), .5f);
                quads[quad_count++] = (ribbon_quad){
                    -cf_m4_transform_point(camera->view, middle).z,
                    start + 2 * (i - 1)
                };
            }
        }
        assert(trails->vertex_count <= TRAIL_VERTICES && quad_count <= TRAIL_SEGMENTS);
        CF_V3 origin = cf_mul_v3_f(center_sum, 2.0f / (trails->vertex_count - start));
        for (int i = start; i < trails->vertex_count; i++) {
            trails->vertices[i].position = cf_sub_v3(trails->vertices[i].position, origin);
        }
        /* Each source owns a contiguous sorted index range and its own depth
         * anchor, so distant trails cannot move a local path across an effect. */
        ribbon_range *draw = &trails->ranges[trails->range_count++];
        *draw = (ribbon_range){ origin, trails->index_count, quad_count * 6 };
        qsort(quads, (size_t)quad_count, sizeof *quads, compare_quads);
        for (int i = 0; i < quad_count; i++) {
            int base = quads[i].vertex;
            static const int offsets[] = { 0, 1, 2, 2, 1, 3 };
            for (int j = 0; j < 6; j++) {
                trails->indices[trails->index_count++] = (uint16_t)(base + offsets[j]);
            }
        }
    }
    if (!trails->index_count) {
        return true;
    }
    cf_mesh_update_vertex_data(trails->mesh, trails->vertices, trails->vertex_count);
    cf_mesh_update_index_data(trails->mesh, trails->indices, trails->index_count);
    return true;
}

void render_scene3d_trails_draw(const scene3d_trails *trails, const render_scene3d_camera *camera)
{
    if (!trails->index_count) {
        return;
    }
    CF_RenderState state = cf_render_state_3d_defaults();
    state.cull_mode = CF_CULL_MODE_NONE;
    state.depth_write_enabled = false;
    state.blend.enabled = true;
    state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
    state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cf_draw3d_push_projection(camera->projection);
    cf_draw3d_push_view(camera->view);
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(trails->shader);
    for (int i = 0; i < trails->range_count; i++) {
        const ribbon_range *range = &trails->ranges[i];
        CF_V4 origin = { range->origin.x, range->origin.y, range->origin.z, 0 };
        /* The shader applies this offset exactly once. Its uniform binding
         * also preserves a separate sortable command for each source; Cute
         * would otherwise coalesce mesh ranges under one farthest-depth key. */
        cf_draw3d_set_uniform("trail_origin", &origin, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw3d_push();
        cf_draw3d_translate(range->origin); // Submission depth anchor only.
        cf_draw3d_mesh_range(trails->mesh, range->first, range->count);
        cf_draw3d_pop();
    }
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_view();
    cf_draw3d_pop_projection();
}
