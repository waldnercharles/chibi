#ifndef CHIBI_CODE_PROGRAM_RECONCILE_H
#define CHIBI_CODE_PROGRAM_RECONCILE_H

#include "assets/code_source.h"

/* Preserve source trivia while changing defaults and authored expressions.
 * Topology changes use readable source with all authored comments retained. */
char *code_program_reconcile(
    const code_source *source,
    const behavior_asset *asset,
    char *error,
    size_t size
);

#endif
