#ifndef CHIBI_RUNTIME_SHADERS_BAKE_H
#define CHIBI_RUNTIME_SHADERS_BAKE_H

#include "runtime/shaders/program.h"

#include <stddef.h>

/* Channels match Surface Output order. Samples are raw linear floats, without
 * color conversion, normalization or clamping. */
typedef enum
{
    SHADER_BAKE_BASE_COLOR,
    SHADER_BAKE_EMISSION,
    SHADER_BAKE_ROUGHNESS,
    SHADER_BAKE_METALLIC,
    SHADER_BAKE_NORMAL,
    SHADER_BAKE_OPACITY,
    SHADER_BAKE_UNLIT,
    SHADER_BAKE_HEIGHT,
    SHADER_BAKE_OCCLUSION,
    SHADER_BAKE_CHANNEL_COUNT
} shader_bake_channel;
#define SHADER_BAKE_CHANNELS_ALL ((1u << SHADER_BAKE_CHANNEL_COUNT) - 1)
#define SHADER_BAKE_SIDE_MAX 4096
#define SHADER_BAKE_BYTES_MAX ((size_t)256 * 1024 * 1024)

const char *shader_bake_channel_name(shader_bake_channel);
int shader_bake_channel_width(shader_bake_channel);
typedef struct
{
    int slot, width;
    float value[4];
} shader_bake_parameter;
typedef struct
{
    int width, height;
    unsigned channels;
    /* Full plane edges from origin. Pixel centers use (x+.5)/width and
     * (y+.5)/height. Rows follow axis_v, including row zero. World, object and
     * Part coordinates coincide; local Position is object XZ, matching shader
     * detail finite differences. */
    float origin[3], axis_u[3], axis_v[3];
    float uv_origin[2], uv_extent[2];
    /* Normal, view, time and callbacks are copied. Coordinate fields are
     * derived from the plane. Callback contexts and their image pixels remain
     * borrowed and immutable until the job is freed. Callbacks must not allocate
     * per sample or perform IO. A zero Normal uses the plane's normal. */
    shader_sample_context context;
    const shader_bake_parameter *parameters;
    int parameter_count;
    /* Output budget; zero selects 64 MiB, capped by SHADER_BAKE_BYTES_MAX. */
    size_t max_bytes;
} shader_bake_desc;
typedef struct
{
    int width, height, stride;
    unsigned channels;
    /* Row-major, packed selected channels in enum order, components together. */
    float *pixels;
} shader_bake_result;
typedef struct shader_bake_job shader_bake_job;
/* Owns a validated graph and numeric parameter snapshot. All referenced
 * parameter slots require one entry of the graph's exact scalar/vector width. */
shader_bake_job *shader_bake_new(const shader_graph *, const shader_bake_desc *, shader_error *);
/* At most max_rows. Evaluation failure marks the job failed; a nonpositive row
 * budget is rejected without advancing it. No output becomes visible until
 * take succeeds. Cancel by freeing the job. Calls on one job are serialized. */
bool shader_bake_step(shader_bake_job *, int max_rows, shader_error *);
bool shader_bake_complete(const shader_bake_job *);
/* A complete job transfers its output once, replacing an initialized result.
 * Failure leaves result unchanged. */
bool shader_bake_take(shader_bake_job *, shader_bake_result *, shader_error *);
void shader_bake_free(shader_bake_job *);
void shader_bake_result_free(shader_bake_result *);
#endif
