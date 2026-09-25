#ifndef CHIBI_COMPILE_H
#define CHIBI_COMPILE_H

#include "graph/model/behavior_asset.h"

#include <stdint.h>

/* CMPL: the compile and validation pass over one authored behavior_asset. The
 * pass is explicit and read-only (CMPL-1): it never rewires, retypes, or
 * deletes. CMPL-17 (editor navigation) and CMPL-19 (operator ambiguity) are
 * deferred: navigation is editor surface, and operator signatures arrive
 * with the reflection v2 widening ladder. */

/* CMPL-16: the default event graph is the only graph today, with a stable
 * id of 1. Multi-graph behavior_assets (Phase 4) assign further ids here. */
#define CMPL_GRAPH_DEFAULT 0u

enum
{
    CMPL_OK = 0,
    CMPL_ERROR = 1,
    CMPL_WARNING = 2,
    CMPL_NOTE = 3,
};

/* CMPL-2: stable rule codes equal the spec numbers, so a diagnostic's rule
 * is both a stable key and its spelling. */
enum
{
    CMPL_RULE_UNCONNECTED_EXEC = 4,       /* CMPL-4 */
    CMPL_RULE_INCOMPATIBLE_DATA_EDGE = 5, /* CMPL-5 */
    CMPL_RULE_DATA_CYCLE = 6,             /* CMPL-6 */
    CMPL_RULE_MISSING_REFERENCE = 7,      /* CMPL-7 */
    CMPL_RULE_DUPLICATE_NAME = 8,         /* CMPL-8 */
    CMPL_RULE_UNRESOLVED_WILDCARD = 12,   /* CMPL-12 */
    CMPL_RULE_UNUSED_VARIABLE = 15,       /* CMPL-15 */
    CMPL_RULE_FUNCTION_LATENT = 9,        /* CMPL-9 */
    CMPL_RULE_FUNCTION_SHAPE = 10,        /* CMPL-10 */
    CMPL_RULE_MACRO_RECURSION = 11,       /* CMPL-11 */
    CMPL_RULE_DISABLED = 18,              /* CMPL-18 */
    CMPL_RULE_UNKNOWN_NOMINAL_TYPE = 20,  /* CMPL-20 */
    CMPL_RULE_SYSTEM_HOST_WAIT = 21, /* Structural host waits need per-entity ownership. */
    CMPL_RULE_OPERATOR = 19,         /* CMPL-19 */
    CMPL_RULE_FIELD_PROJECTION = 22, /* Output fields retain their aggregate source. */
    CMPL_RULE_CALL_SIGNATURE = 23, /* Calls match nonprojected signature parameters. */
    CMPL_RULE_BACKEND = 25, /* Selected backend cannot publish a program. */
    CMPL_RULE_INPUT_BINDING = 24, /* Authored input source is missing or incompatible. */
    CMPL_RULE_QUERY_FUSION = 26,
    CMPL_RULE_QUERY_SCAN = 27,
    CMPL_RULE_CONTAINER = 28,
};

#define CMPL_MESSAGE_MAX 192

typedef struct compile_diagnostic
{
    int severity;      /* CMPL_ERROR, CMPL_WARNING or CMPL_NOTE */
    uint64_t graph_id; /* Stable authored graph id; zero is the root graph. */
    uint64_t node_id;  /* 0 when the rule is not node-scoped */
    uint64_t pin_id;   /* 0 when the rule is not pin-scoped */
    int rule;          /* one of the CMPL_RULE_* codes */
    char message[CMPL_MESSAGE_MAX];
} compile_diagnostic;

typedef struct compile_effects
{
    uint64_t *reads;
    uint64_t *writes;
    unsigned flags;
    int all_writes;
    int parallel_eligible;
    uint64_t blocking_node_id;
    const char *blocking_reason;
    unsigned long long topology_revision;
} compile_effects;

typedef struct compile_report
{
    compile_diagnostic *items; /* dyna buffer; NULL while empty */
    int error_count;
    int warning_count;
    int note_count;
    compile_effects effects;
} compile_report;

/* A report must be initialized before use and released when done. Reusing a
 * report for a second compile releases the previous list first. */
void compile_report_init(compile_report *out);
void compile_report_free(compile_report *out);

/* CMPL-3 / RUN-12: errors block a run, warnings do not. */
int compile_report_blocked(const compile_report *out);
const compile_diagnostic *compile_report_first_error(const compile_report *out);

/* CMPL-1: an explicit, deterministic, read-only pass over one behavior_asset.
 * Diagnostics are ordered by graph id, then node position (canvas y, then
 * x, then node id), then rule code, then pin id (CMPL-16). Returns the
 * error count. */
int behavior_asset_compile(const behavior_asset *asset, compile_report *out);
/* Systems cannot retain per-entity structural host-operation continuations. */
int behavior_asset_compile_for_system(const behavior_asset *asset, compile_report *out);

/* Stable rule code spelling, e.g. "CMPL-4". */
const char *cmpl_rule_name(int rule);
const char *cmpl_severity_name(int severity);
void behavior_asset_query_diagnostics(const behavior_asset *asset, compile_report *out);
const compile_effects *compile_report_effects(const compile_report *out);
int compile_effects_conflict(const compile_effects *left, const compile_effects *right, int same_instance);

#endif
