#ifndef CHIBI_EXECUTION_QUERY_H
#define CHIBI_EXECUTION_QUERY_H
#include "ecs/ecs.h"
#include "execution/module.h"
#include "support/array.h"

typedef struct query_evaluator
{
    struct query_evaluator *next;
    const behavior_function *function;
    const execution_module *module;
    behavior_function_activation activation;
    behavior_function_context context;
    arena storage;
    size_t bytes;
} query_evaluator;
query_evaluator *program_query_evaluator_take(
    struct execution_program *,
    const execution_module *,
    const behavior_function *
);
void program_query_evaluator_keep(struct execution_program *, query_evaluator *);
void program_query_evaluator_free(query_evaluator *);

/* Append a description's native structural terms. */
void execution_query_terms(const execution_module *, int root, ecs_term **);
bool execution_query_membership_matches(
    const execution_module *,
    int root,
    const struct ecs_execution_view *,
    uint64_t owner,
    const uint64_t *variables
);
const char *execution_query_evaluate(
    behavior_function_context *,
    const execution_module *,
    int description,
    uint64_t owner,
    const uint64_t *variables,
    const pin_value *captures,
    int count,
    pin_value *row,
    bool *accepted
);
bool execution_query_cursor_value(pin_value, pin_value *);
#endif
