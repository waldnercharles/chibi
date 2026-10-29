#ifndef CHIBI_GRAPH_VALIDATION_H
#define CHIBI_GRAPH_VALIDATION_H

#include "execution/diagnostic.h"
#include "graph/model/behavior_asset.h"

void compile_report_init(compile_report *out);
void compile_report_free(compile_report *out);
int compile_report_blocked(const compile_report *out);
const compile_diagnostic *compile_report_first_error(const compile_report *out);
const compile_effects *compile_report_effects(const compile_report *out);
const char *cmpl_rule_name(int rule);
const char *cmpl_severity_name(int severity);

/* Read-only shader, shape, shader-library, model and field validation. The
 * initialized report is replaced. Executable source domains are rejected;
 * their diagnostics belong to the canonical source frontend. */
int graph_asset_compile(const behavior_asset *asset, compile_report *out);
/* Same validation with the complete authored model dependency collection. */
int graph_asset_compile_with_models(
    const behavior_asset *,
    const behavior_asset *const *models,
    int count,
    compile_report *
);

#endif
