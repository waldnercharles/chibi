#ifndef CHIBI_CODE_CALLS_H
#define CHIBI_CODE_CALLS_H

#include "assets/code_source_context.h"
#include "graph/model/behavior_asset.h"

/* Native callable construction shared by source parsing and source printing.
 * Arguments/results address semantic port names, never serialized graph records.
 * The caller owns its unpublished candidate and discards it on any failure. */
typedef struct
{
    node *node;
    int pin;
} code_call_output;
typedef enum
{
    CODE_CALL_LITERAL,
    CODE_CALL_OUTPUT,
    CODE_CALL_BINDING,
} code_call_argument_kind;
typedef struct
{
    const char *name;
    bool positional; /* ordinary source argument order, excluding execution ports */
    int ordinal;
    code_call_argument_kind kind;
    pin_value literal;
    code_call_output source;
    const node_desc *conversion; /* optional explicit typed edge conversion */
    pin_binding binding;
    bool has_binding_type;
    pin_type binding_type;
    const char *binding_type_name;
    pin_container binding_container;
    bool has_type; /* explicit scalar numeric capture storage */
    pin_type type;
    bool has_fallback_binding;
    pin_binding fallback_binding;
    bool has_fallback_default;
    pin_value fallback_default;
} code_call_argument;
typedef struct
{
    const char *name;
    code_reference_kind reference_kind;
    pin_type type;
    const char *type_name;
    pin_container container;
    pin_kind kind;
    bool has_traits;
    unsigned flags;
    int group;
    const char *aggregate; /* named aggregate port that owns this field */
    const char *field;     /* reflected member path, without internal pin IDs */
    bool has_default;
    pin_value default_value;
} code_call_port;
typedef struct
{
    const char *name; /* registered name or unique normalized snake_case alias */
    const char *reference; /* variable, event, component, function or macro name */
    const char *reference_asset;
    const char *expression; /* native expression body, when callable supports it */
    graph_expression_mode expression_mode;
    bool native;         /* explicit engine capability qualification */
    const graph *target; /* resolved local/external function or macro signature */
    const behavior_asset *event; /* resolved event definition */
    const code_call_argument *arguments;
    int argument_count;
    bool complete_inputs, complete_results;
    const code_call_port *inputs;
    int input_count;
    const code_call_port *results;
    int result_count;
} code_call_spec;

/* Reversible descriptor spelling; aliases with collisions are refused. */
code_reference_kind code_call_reference_kind(const node_desc *, const char *);
bool code_call_name(const node_desc *desc, char *name, size_t size);
const node_desc *code_call_descriptor(const char *name, char *error, size_t size);
/* Exact or unique normalized semantic port spelling; -1 unknown, -2 ambiguous. */
int code_call_port_index(const node *n, bool output, const char *name, char *error, size_t size);
node *code_call_build(behavior_asset *asset, graph *g, const code_call_spec *spec, char *error, size_t size);
/* Retain an explicit scalar cast as a value boundary before another cast or
 * a consumer with a different storage type can absorb its rounding. */
int code_call_use_local(graph *, uint64_t, node *, int);
/* A cast enclosing fallback clauses materializes their storage inside its boundary. */
bool code_call_materialize_cast_fallback(behavior_asset *, graph *, code_call_argument *, char *, size_t);
int code_call_apply_fallback(graph *, node *, int, const code_call_argument *);
bool code_call_materialize_cast(
    behavior_asset *asset,
    graph *g,
    code_call_argument *argument,
    char *error,
    size_t size
);
/* Resolve one output, exposing reflected aggregate fields when requested.
 * Names and member paths must be explicit; no first-output inference. */
bool code_call_resolve_output(
    graph *g,
    node *n,
    const char *name,
    code_call_output *out,
    char *error,
    size_t size
);

#endif
