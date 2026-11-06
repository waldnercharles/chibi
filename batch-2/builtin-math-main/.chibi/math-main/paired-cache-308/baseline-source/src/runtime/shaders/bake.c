#include "runtime/shaders/bake.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct shader_bake_job
{
    shader_graph graph;
    shader_bake_desc desc;
    shader_bake_result result;
    float parameters[SHADER_PARAMETER_MAX][4];
    float (*values)[4], (*derivatives)[4];
    int row;
    bool failed;
    shader_error error;
};
static const char *const names[] = { "base_color", "emission", "roughness",
                                     "metallic",   "normal",   "opacity",
                                     "unlit",      "height",   "occlusion" };
static const int widths[] = { 3, 3, 1, 1, 3, 1, 1, 1, 1 };
_Static_assert(SHADER_BAKE_CHANNEL_COUNT == SHADER_OUTPUT_MAX, "Surface bake channels");
const char *shader_bake_channel_name(shader_bake_channel channel)
{
    assert(channel >= 0 && channel < SHADER_BAKE_CHANNEL_COUNT);
    return names[channel];
}
int shader_bake_channel_width(shader_bake_channel channel)
{
    assert(channel >= 0 && channel < SHADER_BAKE_CHANNEL_COUNT);
    return widths[channel];
}
static bool fail(shader_error *error, const char *message)
{
    if (error) {
        *error = (shader_error){ 0 };
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}
static bool finite_values(const float *values, int count)
{
    for (int i = 0; i < count; i++) {
        if (!isfinite(values[i])) {
            return false;
        }
    }
    return true;
}
static double dot(const float a[3], const float b[3])
{
    return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
}
static bool plane(shader_bake_desc *d, shader_error *error)
{
    if (!finite_values(d->origin, 3) || !finite_values(d->axis_u, 3) ||
        !finite_values(d->axis_v, 3) || !finite_values(d->uv_origin, 2) ||
        !finite_values(d->uv_extent, 2) || !finite_values(d->context.normal, 3) ||
        !finite_values(d->context.view_direction, 3) || !isfinite(d->context.time)) {
        return fail(error, "Bake plane and sample settings must be finite");
    }
    double uu = dot(d->axis_u, d->axis_u), vv = dot(d->axis_v, d->axis_v);
    double uv = dot(d->axis_u, d->axis_v), determinant = uu * vv - uv * uv;
    if (!(determinant > 0) || !isfinite(determinant)) {
        return fail(error, "Bake plane edges must be nonzero and independent");
    }
    memset(d->context.object_from_world, 0, sizeof d->context.object_from_world);
    memset(d->context.part_from_world, 0, sizeof d->context.part_from_world);
    for (int i = 0; i < 3; i++) {
        d->context.object_from_world[i][i] = 1;
        d->context.part_from_world[i][i] = 1;
        d->context.uv_from_world[0][i] = (float)(d->uv_extent[0] *
                                                 (vv * d->axis_u[i] - uv * d->axis_v[i]) /
                                                 determinant);
        d->context.uv_from_world[1][i] = (float)(d->uv_extent[1] *
                                                 (uu * d->axis_v[i] - uv * d->axis_u[i]) /
                                                 determinant);
        if (!isfinite(d->context.uv_from_world[0][i]) ||
            !isfinite(d->context.uv_from_world[1][i])) {
            return fail(error, "Bake plane derivatives exceed finite float range");
        }
    }
    if (dot(d->context.normal, d->context.normal) == 0) {
        for (int i = 0; i < 3; i++) {
            int a = (i + 1) % 3, b = (i + 2) % 3;
            d->context.normal[i] = (float)(((double)d->axis_u[a] * d->axis_v[b] -
                                            (double)d->axis_u[b] * d->axis_v[a]) /
                                           sqrt(determinant));
        }
    }
    return true;
}
static bool parameters(shader_bake_job *job, const shader_bake_desc *d, shader_error *error)
{
    if (d->parameter_count < 0 || d->parameter_count > SHADER_PARAMETER_MAX ||
        (d->parameter_count && !d->parameters)) {
        return fail(error, "Invalid bake parameter list");
    }
    int supplied[SHADER_PARAMETER_MAX] = { 0 };
    for (int i = 0; i < d->parameter_count; i++) {
        shader_bake_parameter p = d->parameters[i];
        if (p.slot < 0 || p.slot >= SHADER_PARAMETER_MAX || p.width < 1 ||
            p.width > 4 || supplied[p.slot] || !finite_values(p.value, p.width)) {
            return fail(error, "Bake parameters need unique slots and finite typed values");
        }
        supplied[p.slot] = p.width;
        memcpy(job->parameters[p.slot], p.value, (size_t)p.width * sizeof(float));
    }
    for (int i = 0; i < acount(job->graph.nodes); i++) {
        const shader_node *n = &job->graph.nodes[i];
        if (n->op == SHADER_PARAMETER && supplied[n->parameter] != n->width) {
            return fail(error, "Bake parameter width does not match the material");
        }
    }
    return true;
}
shader_bake_job *shader_bake_new(const shader_graph *graph, const shader_bake_desc *desc, shader_error *error)
{
    assert(graph && desc);
    if (error) {
        *error = (shader_error){ 0 };
    }
    if (desc->width <= 0 || desc->height <= 0 ||
        desc->width > SHADER_BAKE_SIDE_MAX || desc->height > SHADER_BAKE_SIDE_MAX ||
        !desc->channels || (desc->channels & ~SHADER_BAKE_CHANNELS_ALL) ||
        graph->output_count != SHADER_OUTPUT_MAX) {
        fail(error, "Bake requires bounded positive dimensions, channels and a Surface graph");
        return NULL;
    }
    int stride = 0;
    for (int i = 0; i < SHADER_BAKE_CHANNEL_COUNT; i++) {
        if (desc->channels & (1u << i)) {
            stride += widths[i];
        }
    }
    size_t bytes = (size_t)desc->width * (size_t)desc->height * (size_t)stride *
                   sizeof(float);
    size_t budget = desc->max_bytes ? desc->max_bytes : (size_t)64 * 1024 * 1024;
    if (bytes > budget || bytes > SHADER_BAKE_BYTES_MAX) {
        fail(error, "Bake output exceeds its byte budget");
        return NULL;
    }
    shader_bake_desc d = *desc;
    if (!plane(&d, error)) {
        return NULL;
    }
    shader_bake_job *job = xcalloc(1, sizeof *job);
    if (!shader_graph_clone(graph, &job->graph, error)) {
        shader_bake_free(job);
        return NULL;
    }
    for (int i = 0; i < SHADER_BAKE_CHANNEL_COUNT; i++) {
        if (job->graph.nodes[job->graph.output[i]].width != widths[i]) {
            fail(error, "Surface output widths do not match bake channel types");
            shader_bake_free(job);
            return NULL;
        }
    }
    if (!parameters(job, desc, error)) {
        shader_bake_free(job);
        return NULL;
    }
    job->desc = d;
    job->desc.parameters = NULL;
    job->desc.parameter_count = 0;
    job->result = (shader_bake_result){ .width = d.width,
                                        .height = d.height,
                                        .stride = stride,
                                        .channels = d.channels,
                                        .pixels = xmalloc(bytes) };
    job->values = xcalloc((size_t)acount(graph->nodes), sizeof *job->values);
    job->derivatives = xcalloc((size_t)acount(graph->nodes) * 2, sizeof *job->derivatives);
    return job;
}
static bool sample(shader_bake_job *job, int x, int y, shader_error *error)
{
    const shader_bake_desc *d = &job->desc;
    shader_sample_context context = d->context;
    context.derivative_scratch = job->derivatives;
    context.derivative_capacity = acount(job->graph.nodes) * 2;
    double u = (x + .5) / d->width, v = (y + .5) / d->height;
    for (int i = 0; i < 3; i++) {
        float value = (float)(d->origin[i] + u * d->axis_u[i] + v * d->axis_v[i]);
        if (!isfinite(value)) {
            return fail(error, "Bake plane produced a nonfinite position");
        }
        context.world_position[i] = value;
        context.object_position[i] = value;
        context.part_position[i] = value;
    }
    context.position[0] = context.world_position[0];
    context.position[1] = context.object_position[2];
    context.uv[0] = (float)(d->uv_origin[0] + u * d->uv_extent[0]);
    context.uv[1] = (float)(d->uv_origin[1] + v * d->uv_extent[1]);
    if (!finite_values(context.uv, 2)) {
        return fail(error, "Bake plane produced nonfinite UVs");
    }
    if (!shader_graph_eval_values_validated(&job->graph, &context, job->parameters, job->values, error)) {
        return false;
    }
    float *target = job->result.pixels +
                    ((size_t)y * d->width + x) * job->result.stride;
    for (int i = 0; i < SHADER_BAKE_CHANNEL_COUNT; i++) {
        if (!(d->channels & (1u << i))) {
            continue;
        }
        int root = job->graph.output[i];
        if (!finite_values(job->values[root], widths[i])) {
            fail(error, "Material bake channel evaluated to a nonfinite value");
            if (error) {
                error->node = job->graph.nodes[root].source_id;
            }
            return false;
        }
        memcpy(target, job->values[root], (size_t)widths[i] * sizeof(float));
        target += widths[i];
    }
    return true;
}
bool shader_bake_step(shader_bake_job *job, int max_rows, shader_error *error)
{
    assert(job);
    if (job->failed) {
        if (error) {
            *error = job->error;
        }
        return false;
    }
    if (max_rows <= 0 || !job->result.pixels) {
        return fail(error, "Bake row budget must be positive and output still owned");
    }
    int count = job->desc.height - job->row;
    if (count > max_rows) {
        count = max_rows;
    }
    for (int end = job->row + count; job->row < end; job->row++) {
        for (int x = 0; x < job->desc.width; x++) {
            if (!sample(job, x, job->row, &job->error)) {
                job->failed = true;
                if (error) {
                    *error = job->error;
                }
                return false;
            }
        }
    }
    return true;
}
bool shader_bake_complete(const shader_bake_job *job)
{
    assert(job);
    return !job->failed && job->result.pixels && job->row == job->desc.height;
}
bool shader_bake_take(shader_bake_job *job, shader_bake_result *result, shader_error *error)
{
    assert(job && result);
    if (!shader_bake_complete(job)) {
        return fail(error, "Bake output is unavailable until all rows complete successfully");
    }
    shader_bake_result_free(result);
    *result = job->result;
    job->result = (shader_bake_result){ 0 };
    return true;
}
void shader_bake_result_free(shader_bake_result *result)
{
    assert(result);
    free(result->pixels);
    *result = (shader_bake_result){ 0 };
}
void shader_bake_free(shader_bake_job *job)
{
    if (!job) {
        return;
    }
    shader_graph_free(&job->graph);
    shader_bake_result_free(&job->result);
    free(job->values);
    free(job->derivatives);
    free(job);
}
