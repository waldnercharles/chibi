#ifndef CHIBI_AUTHORING_SHADER_REPORT_H
#define CHIBI_AUTHORING_SHADER_REPORT_H
#include "graph/model/behavior_asset.h"
#include "runtime/shaders/inspection.h"

typedef enum
{
    SHADER_AUTHORING_SCALAR,
    SHADER_AUTHORING_VECTOR,
    SHADER_AUTHORING_COLOR,
} shader_authoring_presentation;

/* CPU compilation only: no GPU context, image loading or catalog publication.
 * Pass a linked source when its graph calls external shader libraries. */
typedef struct
{
    shader_graph program;
    shader_inspection costs;
    shader_alpha_mode alpha_mode;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    uint64_t source_nodes[SHADER_PARAMETER_MAX];
    /* Color only for explicit color pins or direct material color outputs.
     * Parameter names never determine presentation. */
    shader_authoring_presentation presentation[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4];
} shader_authoring_report;
void shader_authoring_report_init(shader_authoring_report *);
void shader_authoring_report_free(shader_authoring_report *);
/* Initialized destination is replaced on success and preserved on failure.
 * All report storage is owned, including parameter names and image keys. */
bool shader_authoring_inspect(const behavior_asset *, shader_authoring_report *, shader_error *);
/* Edits one uniquely declared parameter's authored default, preserving its name,
 * slot and type. Caller owns the graph undo gesture. Width must match the
 * declaration type and every value must be finite; integer/bool declarations
 * also require representable values. Missing, ambiguous or uneditable slots leave
 * source unchanged. This accepts unused declarations and unlinked/invalid graphs
 * so callers can repair sources. Reports are snapshots; inspect after editing. */
bool shader_authoring_set_default(behavior_asset *, int slot, const float *values, int width, shader_error *);
/* Double ingress preserves the full I32 domain; vector/F32 storage remains float. */
bool shader_authoring_set_default_exact(
    behavior_asset *,
    int slot,
    const double *values,
    int width,
    shader_error *
);
/* Fixed reproducible center probe; origin, UV .5/.5, time/activity zero,
 * normal/view +Y and preview coordinate gradients. Caller supplies resolver. */
shader_sample_context shader_authoring_center_context(const behavior_asset *);
/* NULL parameters selects authored defaults. Image samples require an explicit
 * resident image resolver in context. These are CPU values, not rendered pixels. */
bool shader_authoring_sample(
    const shader_authoring_report *,
    const shader_sample_context *,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float outputs[SHADER_OUTPUT_MAX][4],
    shader_error *
);
#endif
