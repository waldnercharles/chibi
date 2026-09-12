#ifndef CHIBI_BEHAVIOR_LOWER_INTERNAL_H
#define CHIBI_BEHAVIOR_LOWER_INTERNAL_H
#include "graph/compiler/behavior_function_internal.h"
typedef struct
{
    size_t offset;
    pin_type type;
    int slot;
} aggregate_field;
typedef struct
{
    const type_desc *type;
    int slot, present, name;
    aggregate_field *fields;
} aggregate;
typedef struct
{
    const node *node;
    int pin, slot;
} output;
typedef struct
{
    node *node;
    int after, pc, frame;
} task;
typedef struct
{
    behavior_function *function;
    const behavior_asset *asset;
    graph *graph;
    const node *entry;
    const node *root_entry;
    task *tasks;
    output *outputs;
    const node **evaluating;
    aggregate *aggregates;
    unsigned call_depth;
    int frame, source;
    behavior_function_diagnostic *diagnostic;
} builder;

int behavior_lower_owner(builder *b);
int behavior_lower_component_read(builder *b, node *n);
bool behavior_lower_component_write(builder *b, node *n);
int behavior_lower_component_binding(builder *b, node *n, int pin);
bool behavior_lower_scalar(pin_type type);
int behavior_lower_value(builder *b, behavior_function_instruction i);
aggregate *behavior_lower_aggregate(builder *b, int slot);
aggregate behavior_lower_shape(const char *name);
int behavior_lower_aggregate_value(builder *b, aggregate a);
int behavior_lower_source(
    builder *b,
    behavior_function_op op,
    int index,
    const pin_spec *spec,
    const node *n,
    int pin
);
void behavior_lower_require(builder *b, const aggregate *a, const node *n, int pin);
int behavior_lower_field(builder *b, int slot, const type_field *field, const node *n, int pin);
int behavior_lower_make(builder *b, node *n);
int behavior_lower_unsupported(builder *b, const node *n, int pin, bool output, const char *reason);
int behavior_lower_emit(builder *b, behavior_function_instruction i);
int behavior_lower_variable(const builder *b, uint64_t id, pin_type type);
int behavior_lower_local(builder *b, int frame, const node *n, int pin);
int behavior_lower_convert(builder *b, int slot, pin_type type);
int behavior_lower_input(builder *b, node *n, int pin);
bool behavior_lower_store(builder *b, behavior_function_op op, int slot, int target);
void behavior_lower_finish_values(builder *b);
#endif
