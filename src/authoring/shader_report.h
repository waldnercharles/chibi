#ifndef CHIBI_AUTHORING_SHADER_REPORT_H
#define CHIBI_AUTHORING_SHADER_REPORT_H
#include "graph/model/behavior_asset.h"
#include "runtime/shaders/inspection.h"

/* CPU compilation only: no GPU context, image loading or catalog publication.
 * Pass a linked source when its graph calls external shader libraries. */
typedef struct
{
    shader_graph program;
    shader_inspection costs;
    shader_alpha_mode alpha_mode;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4];
} shader_authoring_report;
void shader_authoring_report_init(shader_authoring_report *);
void shader_authoring_report_free(shader_authoring_report *);
/* Initialized destination is replaced on success and preserved on failure.
 * All report storage is owned, including parameter names and image keys. */
bool shader_authoring_inspect(const behavior_asset *, shader_authoring_report *, shader_error *);
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
