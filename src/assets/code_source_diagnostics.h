#ifndef CHIBI_CODE_SOURCE_DIAGNOSTICS_H
#define CHIBI_CODE_SOURCE_DIAGNOSTICS_H

#include "assets/code_source.h"
#include "graph/compiler/compile.h"

/* Copies diagnostic provenance from a live linked snapshot. Foreign native IDs
 * identify their original asset, and source ranges highlight its importing call.
 * The source owns copied origin strings until its diagnostics are cleared. */
void code_source_compile_diagnostics(
    code_source *source,
    const behavior_asset *linked,
    const compile_report *report
);

#endif
