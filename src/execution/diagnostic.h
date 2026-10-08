#ifndef CHIBI_EXECUTION_DIAGNOSTIC_H
#define CHIBI_EXECUTION_DIAGNOSTIC_H

#include <stdint.h>

/* Stable diagnostics shared by syntax, semantic producers, and executable
 * verification. Numeric origins let authoring surfaces resolve their own view. */

/* The authored root uses zero; nested scopes retain their stable identities. */
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
    CMPL_RULE_EDITOR_NODE = 29, /* An editor-only node outside the project that runs it. */
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

#endif
