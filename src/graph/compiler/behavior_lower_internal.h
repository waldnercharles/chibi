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
    bool name_owned;
    aggregate_field *fields;
} aggregate;
typedef struct
{
    int slot, pointer, name;
    pin_type type;
    pin_container container;
    bool temporary;
    bool stable; /* Immutable for this invocation, including saved snapshots. */
} array_value;
typedef struct
{
    int slot, value, name, variable, wide;
    pin_type logical_type;
    bool owned;
} scalar_value;
typedef struct
{
    const node *node;
    int pin, slot;
} output;
typedef struct
{
    node *node;
    node *from;
    int output;
    int after, pc, frame, loop, input, stack;
    bool continuation;
} task;
typedef struct
{
    const node *node;
    int parent, current, last, step, broken, completed, next, items;
    bool entity_snapshot;
} lowered_loop;
typedef struct
{
    graph *graph;
    node *entry;
    behavior_function *function;
} lowered_definition;
typedef struct
{
    lowered_definition *definitions;
    bool recursive;
} lowering_unit;
typedef struct behavior_lower_builder
{
    behavior_function *function;
    lowering_unit *unit;
    const behavior_asset *asset;
    graph *graph;
    const node *entry;
    const node *root_entry;
    task *tasks;
    int *labels;
    lowered_loop *loops;
    output *outputs, *inputs;
    bool recording, suppress_trace, capture_outputs;
    int pending_watch_frame;
    const node **traced;
    const node **evaluating;
    aggregate *aggregates;
    array_value *arrays;
    scalar_value *scalars;
    unsigned call_depth;
    int frame, source, loop, stack;
    int constant_start;
    behavior_function_diagnostic *diagnostic;
} builder;

int behavior_lower_schedule(builder *b, task t);
int behavior_lower_label(builder *b);
void behavior_lower_bind_label(builder *b, int label);
int behavior_lower_successor(builder *b, node *n, int pin, int after);
int behavior_lower_loop(builder *b, task t);
int behavior_lower_delay(builder *b, task t);
bool behavior_lower_is_switch(const node_desc *desc);
int behavior_lower_switch(builder *b, task t);
bool behavior_lower_is_state(const node_desc *desc);
int behavior_lower_state(builder *b, task t);
bool behavior_lower_state_inputs(builder *b, node *n);
int behavior_lower_hidden_local(builder *b, const node *n, int key, pin_type type);
int behavior_lower_owner(builder *b);
array_value *behavior_lower_array(builder *b, int slot);
int behavior_lower_array_value(builder *b, int pointer, int name, const pin_spec *spec);
int behavior_lower_array_source(builder *b, behavior_function_op op, int index, const pin_spec *spec);
int behavior_lower_array_copy(builder *b, int pointer);
int behavior_lower_array_default(builder *b, pin_value value);
int behavior_lower_array_operation(builder *b, node *n);
int behavior_lower_query(builder *b, node *n);
int behavior_lower_host_call(builder *b, node *n, int pin);
bool behavior_lower_invoke(builder *b, node *n, graph *target);
int behavior_lower_data_result(builder *b, const pin_spec *spec, int index);
bool behavior_lower_pack_value(builder *b, int value, int index);
int behavior_lower_motion(builder *b, node *n, int pin);
int behavior_lower_random(builder *b, node *n, int pin);
int behavior_lower_command(builder *b, node *n, int after);
scalar_value *behavior_lower_scalar_value(builder *b, int slot);
int behavior_lower_named_scalar(builder *b, int value, int name, bool owned);
int behavior_lower_static_name(builder *b, const char *name);
int behavior_lower_named_constant(builder *b, int value, const char *name);
int behavior_lower_inherit_name(builder *b, int value, int source);
int behavior_lower_coerce(builder *b, int value, const pin_spec *spec, bool binding);
int behavior_lower_local_name(builder *b, int local);
void behavior_lower_store_name(builder *b, behavior_function_op op, int value, int target);
int behavior_lower_source_name(builder *b, int value, behavior_function_op op, int index, const pin_spec *spec);
int behavior_lower_array_element(builder *b, int items, int index, const pin_spec *spec, bool entity_snapshot);
int behavior_lower_array_item(builder *b, int items, int index);
int behavior_lower_read(builder *b, int pointer, size_t offset, pin_type type, bool nullable);
int behavior_lower_component_column(builder *b, node *n, int pin, int world, const struct ecs_component_desc *desc);
int behavior_lower_component_read(builder *b, node *n);
bool behavior_lower_component_write(builder *b, node *n);
int behavior_lower_component_binding(builder *b, node *n, int pin);
int behavior_lower_component_target(builder *b, node *n, int world, int owner, const struct ecs_component_desc *desc);
bool behavior_lower_scalar(pin_type type);
int behavior_lower_value(builder *b, behavior_function_instruction i);
aggregate *behavior_lower_aggregate(builder *b, int slot);
aggregate behavior_lower_shape(const char *name);
int behavior_lower_aggregate_value(builder *b, aggregate a);
int behavior_lower_identity(builder *b, int slot, const node_desc *desc, const node *n, int pin, bool edge);
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
int behavior_lower_variable(const builder *b, uint64_t id, pin_type type, pin_container container);
int behavior_lower_local(builder *b, int frame, const node *n, int pin);
int behavior_lower_convert(builder *b, int slot, pin_type type);
int behavior_lower_input(builder *b, node *n, int pin);
int behavior_lower_output(builder *b, node *n, int pin);
void behavior_lower_watch(builder *b, const node *n, int pin, bool output, int value);
void behavior_lower_capture(builder *b, node *n, bool outputs_only);
void behavior_lower_watch_reset(builder *b, int frame);
void behavior_lower_watch_commit(builder *b, int frame);
void behavior_lower_trace_dependencies(builder *b, node *n);
void behavior_lower_trace(builder *b, node *from, int output, node *to, int input, graph_edge_kind kind);
void behavior_function_remove_observers(behavior_function *function);
bool behavior_lower_store(builder *b, behavior_function_op op, int slot, int target);
void behavior_lower_finish_values(builder *b);
#endif
