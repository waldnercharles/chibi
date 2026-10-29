#ifndef CHIBI_SHADER_PREPARED_H
#define CHIBI_SHADER_PREPARED_H
#include "runtime/shaders/program.h"

#include <stddef.h>
#define SHADER_PREPARED_VERSION UINT64_C(4)
/* Field-wise immutable IR, resource keys and sampler settings. No host callback,
 * process address, GPU object or authored source is persisted. Bytes use free(). */
bool shader_graph_prepared_write(const shader_graph *, unsigned char **, size_t *, shader_error *);
/* Initialized destination and remaining budget stay unchanged on failure.
 * Input and aggregate decoded storage are bounded to 64 MiB. */
bool shader_graph_prepared_read_budget(const void *, size_t, size_t *remaining, shader_graph *, shader_error *);
bool shader_graph_prepared_read(const void *, size_t, shader_graph *, shader_error *);
/* Renderer-independent catalog payload. Uniform slots retain exact float widths,
 * defaults and declared names; node source IDs remain in the canonical IR. */
typedef struct
{
    char *name;
    shader_graph graph;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4], bounds[2];
    bool shape;
    shader_alpha_mode alpha_mode;
} shader_prepared_asset;
void shader_prepared_asset_init(shader_prepared_asset *);
void shader_prepared_asset_free(shader_prepared_asset *);
bool shader_asset_prepared_write(const shader_prepared_asset *, unsigned char **, size_t *, shader_error *);
bool shader_asset_prepared_read_budget(
    const void *,
    size_t,
    size_t *remaining,
    shader_prepared_asset *,
    shader_error *
);
#endif
