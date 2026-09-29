#include "runtime/systems/system.h"

#include "graph/compiler/graph_ir.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/compiler/program.h"
#include "graph/document/events.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/math_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SYSTEM_MAX_PINS GRAPH_IR_MAX_OPERANDS
#define SYSTEM_MAX_TYPE_DEPTH 16

typedef enum
{
    SYSTEM_OP_CONST,
    SYSTEM_OP_GET,
    SYSTEM_OP_SET,
    SYSTEM_OP_BREAK,
    SYSTEM_OP_MAKE,
    SYSTEM_OP_CALL,
} system_op;

typedef struct
{
    system_op op;
    const node *node;
    const ecs_component_desc *component;
    const ecs_component_desc *world_component;
    const type_desc *type;
    node_fn fn;
    node_fast_fn fast;
    node_fast_direct_fn direct;
    int direct_count;
    int alias;
    int output;
    int input_count;
    int inputs[SYSTEM_MAX_PINS];
    int output_count;
    int outputs[SYSTEM_MAX_PINS];
    int field_alias[SYSTEM_MAX_PINS];
    pin_value values[SYSTEM_MAX_PINS];
} system_instruction;

typedef struct
{
    pin_type type;
    pin_container container;
    const char *type_name;
    const type_desc *description;
    size_t size;
    size_t offset;
    void *data;
} system_slot;

typedef struct
{
    const graph *graph;
    int *node_base;
    int *node_state;
    int *aliases;
    int value_count;
    int event_delta;
    int expression_depth;
    int dynamic_expression;
    union
    {
        graph_program shared;
        struct
        {
            graph_ir ir;
            graph_sljit_program *sljit;
        };
    };
    system_instruction *instructions;
    pin_value *values;
    system_slot *slots;
    unsigned char *storage;
    size_t storage_size;
    int typed;
    graph_jit_column *sljit_columns;
    void **sljit_bases;
    int sljit_column_count;
    arena arena;
} system_program;

struct game_system
{
    ecs_world *world;
    behavior_asset *definition;
    behavior_instance *instance;
    system_program *program;
    compile_report compile;
    ecs_component_id *query;
    ecs_query_expr *query_expression;
    ecs_query_expr *compiled_query_expression;
    node *tick;
    int match_count;
    ecs_entity *snapshot;
    uint32_t *batch_slots; /* Matched slots, retained across ticks. */
};

static void set_error(flow_error *error, int code, const node *origin, const char *message)
{
    if (error) {
        *error = (flow_error){ code, origin, -1, message, 0 };
    }
}

static const ecs_component_desc *component_by_id(ecs_component_id id)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component;
        }
    }
    return NULL;
}

static int query_has(const game_system *system, ecs_component_id component)
{
    for (int i = 0; i < acount(system->query); i++) {
        if (system->query[i] == component) {
            return 1;
        }
    }
    return 0;
}

static void add_query_component(game_system *system, ecs_component_id component)
{
    if (!query_has(system, component)) {
        apush(system->query, component);
    }
}

static void add_access_components(game_system *system, const node_access *access)
{
    if (!access) {
        return;
    }
    for (int i = 0; i < access->read_count; i++) {
        add_query_component(system, access->reads[i]);
    }
    for (int i = 0; access->read_refs && access->read_refs[i]; i++) {
        add_query_component(system, *access->read_refs[i]);
    }
    for (int i = 0; i < access->write_count; i++) {
        add_query_component(system, access->writes[i]);
    }
}

static void build_query(game_system *system)
{
    graph *graph = system->definition->graph;
    for (int i = 0; i < acount(graph->nodes); i++) {
        node *current = graph->nodes[i];
        if (current->desc && current->desc->flow_kind == NODE_FLOW_EVENT &&
            graph_host_event_is(current->reference_name, GRAPH_HOST_EVENT_TICK)) {
            if (system->tick) {
                system->tick = NULL;
            } else {
                system->tick = current;
            }
        }
        add_access_components(system, current->desc ? current->desc->access : NULL);
    }
}

static int program_node_index(const system_program *program, const node *target)
{
    for (int i = 0; i < acount(program->graph->nodes); i++) {
        if (program->graph->nodes[i] == target) {
            return i;
        }
    }
    return -1;
}

static int program_register(const system_program *program, const node *target, int output)
{
    int index = program_node_index(program, target);
    if (index < 0 || output < 0 || output >= target->output_count) {
        return -1;
    }
    int result = program->node_base[index] + output;
    while (program->aliases[result] >= 0) {
        result = program->aliases[result];
    }
    return result;
}

/* The data nodes compile_value evaluates, by how each computes its outputs. */
static bool data_supported(const node_desc *desc)
{
    int fields = desc->type ? desc->type->field_count : -1;
    bool sized = fields >= 0 && fields <= SYSTEM_MAX_PINS;
    switch (desc->pure_kind) {
        case NODE_PURE_DELTA: return desc->output_count == 1;
        case NODE_PURE_COPY:
            return desc->input_count == 1 && desc->output_count == 1;
        case NODE_PURE_MAKE:
            return sized && desc->input_count == fields && desc->output_count == 1;
        case NODE_PURE_BREAK:
            return sized && desc->input_count == 1 && desc->output_count == fields;
        case NODE_PURE_CALL:
            return desc->fn && desc->input_count <= SYSTEM_MAX_PINS &&
                   desc->output_count == 1;
        case NODE_PURE_FUNCTION: return desc == &NODE_BehaviorExpression;
        case NODE_PURE_READ: return desc == &NODE_RandomReal;
        default: return false;
    }
}

bool game_system_node_supported(const node_desc *desc)
{
    switch (desc->component_kind) {
        case NODE_COMPONENT_GET_SELF: return true;
        /* A whole-value write copies the struct, so text would go unretained. */
        case NODE_COMPONENT_SET_SELF: return type_is_plain(desc->type);
        case NODE_COMPONENT_NONE: break;
        default: return false;
    }
    return desc == &NODE_Event || (node_desc_is_pure(desc) && data_supported(desc));
}

static bool node_is(const node *target, node_component_kind kind)
{
    return target->desc->component_kind == kind &&
           game_system_node_supported(target->desc);
}

static int node_has_exec_pins(const node *target)
{
    for (int i = 0; i < target->input_count; i++) {
        if (target->input_pins[i].kind == PIN_EXEC) {
            return 1;
        }
    }
    for (int i = 0; i < target->output_count; i++) {
        if (target->output_pins[i].kind == PIN_EXEC) {
            return 1;
        }
    }
    return 0;
}

static int pin_matches_field(const pin_spec *pin, const type_field *field)
{
    pin_spec expected = {
        .type = field->type,
        .type_name = field->type_name,
        .container = PIN_CONTAINER_SINGLE,
    };
    return pin->kind == PIN_DATA && pin->name &&
           strcmp(pin->name, field->name) == 0 && pin_compatible(pin, &expected);
}

static int pin_matches_component(const pin_spec *pin, const ecs_component_desc *component)
{
    pin_spec expected = {
        .type = PIN_STRUCT,
        .type_name = component->type ? component->type->name : NULL,
        .container = PIN_CONTAINER_SINGLE,
    };
    return component->type && pin->type == PIN_STRUCT &&
           pin->container == PIN_CONTAINER_SINGLE && pin_compatible(pin, &expected);
}

static const edge *data_edge(const graph *graph, const node *target, int input)
{
    uint64_t pin = target->input_pins[input].id;
    for (int i = 0; i < acount(graph->edges); i++) {
        const edge *current = &graph->edges[i];
        if (current->kind == GRAPH_EDGE_DATA && current->to_id == target->id &&
            current->to_pin_id == pin) {
            return current;
        }
    }
    return NULL;
}

static const node *edge_source(const graph *graph, const edge *link, int *output)
{
    const node *source = graph_node_by_id(graph, link->from_id);
    if (!source) {
        return NULL;
    }
    for (int i = 0; i < source->output_count; i++) {
        if (source->output_pins[i].id == link->from_pin_id) {
            *output = i;
            return source;
        }
    }
    return NULL;
}

static const node *exec_target(const graph *graph, const node *source, int *found)
{
    const node *target = NULL;
    for (int pin = 0; pin < source->output_count; pin++) {
        if (source->output_pins[pin].kind != PIN_EXEC) {
            continue;
        }
        for (int i = 0; i < acount(graph->edges); i++) {
            const edge *link = &graph->edges[i];
            if (link->kind != GRAPH_EDGE_EXEC || link->from_id != source->id ||
                link->from_pin_id != source->output_pins[pin].id) {
                continue;
            }
            if (*found) {
                return NULL;
            }
            target = graph_node_by_id(graph, link->to_id);
            *found = 1;
        }
    }
    return target;
}

static system_instruction *add_instruction(system_program *program, system_op op, const node *node)
{
    system_instruction instruction = { .op = op, .node = node };
    apush(program->instructions, instruction);
    return &alast(program->instructions);
}

static int type_is_typed(const type_desc *type, int depth)
{
    if (!type || depth >= SYSTEM_MAX_TYPE_DEPTH) {
        return 0;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type == PIN_NAME || field->type == PIN_STR) {
            return 0;
        }
        if (field->type == PIN_STRUCT &&
            !type_is_typed(registry_type_by_name(field->type_name), depth + 1)) {
            return 0;
        }
    }
    return 1;
}

static size_t pin_storage_size(const pin_spec *pin)
{
    if (pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    switch (pin->type) {
        case PIN_FLOW: return 0;
        case PIN_BOOL: return sizeof(bool);
        case PIN_I8: return sizeof(int8_t);
        case PIN_U8: return sizeof(uint8_t);
        case PIN_I16: return sizeof(int16_t);
        case PIN_U16: return sizeof(uint16_t);
        case PIN_I32: return sizeof(int32_t);
        case PIN_U32: return sizeof(uint32_t);
        case PIN_I64: return sizeof(int64_t);
        case PIN_U64: return sizeof(uint64_t);
        case PIN_F32: return sizeof(float);
        case PIN_F64: return sizeof(double);
        case PIN_NAME:
        case PIN_STR: return 0;
        case PIN_HANDLE: return sizeof(uint64_t);
        case PIN_ENUM: return sizeof(int32_t);
        case PIN_STRUCT: {
            const type_desc *type = registry_type_by_name(pin->type_name);
            return type && type_is_typed(type, 0) ? type->size : 0;
        }
    }
    return 0;
}

static void set_slot(system_slot *slot, const pin_spec *pin)
{
    *slot = (system_slot){
        .type = pin->type,
        .container = pin->container,
        .type_name = pin->type_name,
        .description = pin->type == PIN_STRUCT ? registry_type_by_name(pin->type_name)
                                               : NULL,
        .size = pin_storage_size(pin),
    };
}

static bool slot_matches_pin(const system_slot *slot, const pin_spec *pin)
{
    pin_spec actual = {
        .type = slot->type,
        .type_name = slot->type_name,
        .container = slot->container,
    };
    return slot->type == pin->type && pin_compatible(&actual, pin);
}

static bool slot_is_float_pair(const system_slot *slot)
{
    const type_desc *type = slot->description;
    if (slot->container != PIN_CONTAINER_SINGLE || !type ||
        type->size != 2 * sizeof(float) || type->field_count != 2) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_F32 || field->size != sizeof(float) ||
            field->offset != (size_t)i * sizeof(float)) {
            return false;
        }
    }
    return true;
}

static bool call_has_fast_signature(const system_program *program, const system_instruction *call)
{
    const node_desc *desc = call->node->desc;
    const system_slot *output = &program->slots[call->output];
    if (call->input_count != desc->input_count || desc->output_count != 1) {
        return false;
    }
    bool scale = desc == &NODE_Multiply || desc == &NODE_Divide;
    bool pair = desc == &NODE_Add || desc == &NODE_Subtract || desc == &NODE_Min ||
                desc == &NODE_Max || desc == &NODE_Modulo || desc == &NODE_Clamp;
    if (scale || pair) {
        /* These wildcard descriptors carry two-float adapters, not adapters
         * for their default scalar pins. Generic structs also match by field
         * name, so equal byte sizes alone do not establish the same operation. */
        const system_slot *first = &program->slots[call->inputs[0]];
        if (!slot_is_float_pair(first) || !slot_is_float_pair(output) ||
            !slot_matches_pin(output, &call->node->input_pins[0])) {
            return false;
        }
        for (int i = 1; i < call->input_count; i++) {
            const system_slot *input = &program->slots[call->inputs[i]];
            if (scale) {
                if (input->type != PIN_F32 || input->container != PIN_CONTAINER_SINGLE) {
                    return false;
                }
                continue;
            }
            if (!slot_is_float_pair(input)) {
                return false;
            }
            for (int field = 0; field < 2; field++) {
                if (strcmp(
                        first->description->fields[field].name,
                        input->description->fields[field].name
                    ) != 0) {
                    return false;
                }
            }
        }
        return true;
    }
    if (!slot_matches_pin(output, &desc->outputs[0])) {
        return false;
    }
    for (int i = 0; i < call->input_count; i++) {
        if ((desc->inputs[i].flags & PIN_FLAG_WILDCARD) ||
            !slot_matches_pin(&program->slots[call->inputs[i]], &desc->inputs[i])) {
            return false;
        }
    }
    return !(desc->outputs[0].flags & PIN_FLAG_WILDCARD);
}

static int new_register(system_program *program, const pin_spec *pin)
{
    int result = program->value_count++;
    program->slots = (system_slot *)
        xrealloc(program->slots, (size_t)program->value_count * sizeof *program->slots);
    program->aliases = (int *)xrealloc(
        program->aliases,
        (size_t)program->value_count * sizeof *program->aliases
    );
    program->aliases[result] = -1;
    set_slot(&program->slots[result], pin);
    return result;
}

static int compile_value(system_program *program, const node *target, int output);
static int compile_exec(system_program *program, const node *target);

static pin_value real_width_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 1);
    pin_value value = inputs[0];
    if (value.type == PIN_F32) {
        return (pin_value){ .type = PIN_F64, .d = value.f };
    }
    assert(value.type == PIN_F64);
    return (pin_value){ .type = PIN_F32, .f = (float)value.d };
}

static int compile_input(system_program *program, const node *target, int input)
{
    const pin_spec *pin = &target->input_pins[input];
    const edge *link = data_edge(program->graph, target, input);
    if (link) {
        int output = 0;
        const node *source = edge_source(program->graph, link, &output);
        if (!source) {
            return -1;
        }
        int value = compile_value(program, source, output);
        if (value < 0) {
            return -1;
        }
        if (link->conversion) {
            const node_desc *conversion = link->conversion;
            int converted = new_register(program, &conversion->outputs[0]);
            system_instruction *instruction = add_instruction(program, SYSTEM_OP_CALL, target);
            instruction->fn = conversion->fn;
            instruction->output = converted;
            instruction->input_count = 1;
            instruction->inputs[0] = value;
            value = converted;
        }
        pin_type type = program->slots[value].type;
        if (type != pin->type && (type == PIN_F32 || type == PIN_F64) &&
            (pin->type == PIN_F32 || pin->type == PIN_F64)) {
            int converted = new_register(program, pin);
            system_instruction *instruction = add_instruction(program, SYSTEM_OP_CALL, target);
            instruction->fn = real_width_value;
            instruction->output = converted;
            instruction->input_count = 1;
            instruction->inputs[0] = value;
            value = converted;
        }
        return value;
    }
    int result = new_register(program, pin);
    system_instruction *instruction = add_instruction(program, SYSTEM_OP_CONST, target);
    instruction->output = result;
    instruction->output_count = 1;
    instruction->values[0] = pin->default_value;
    return result;
}

/* Generated expressions lower into the same instruction stream as their
 * caller. Each call owns distinct registers, including each random sample. */
static int compile_expression(system_program *program, const node *call)
{
    graph *body = behavior_asset_find_graph(program->graph->owner, call->reference_id);
    if (!body || !body->math_expression || call->output_count != 1 ||
        program->expression_depth >= 128) {
        return -1;
    }
    node *entry = graph_node_by_id(body, body->entry_id);
    node *ret = graph_node_by_id(body, body->return_id);
    if (!entry || !ret || entry->output_count != call->input_count ||
        ret->input_count != 1) {
        return -1;
    }
    for (int i = 0; i < call->input_count; i++) {
        program->dynamic_expression |= call->input_pins[i].type == PIN_STRUCT;
    }
    program->dynamic_expression |= call->output_pins[0].type == PIN_STRUCT;
    int *inputs = NULL;
    for (int i = 0; i < call->input_count; i++) {
        int value = compile_input(program, call, i);
        if (value < 0) {
            afree(inputs);
            return -1;
        }
        apush(inputs, value);
    }
    const graph *caller = program->graph;
    int *caller_base = program->node_base;
    int *caller_state = program->node_state;
    program->graph = body;
    program->node_base = xcalloc((size_t)acount(body->nodes), sizeof *program->node_base);
    program->node_state = xcalloc((size_t)acount(body->nodes), sizeof *program->node_state);
    for (int i = 0; i < acount(body->nodes); i++) {
        const node *n = body->nodes[i];
        program->node_base[i] = program->value_count;
        for (int p = 0; p < n->output_count; p++) {
            int value = new_register(program, &n->output_pins[p]);
            if (n == entry) {
                program->aliases[value] = inputs[p];
            }
        }
        if (n == entry) {
            program->node_state[i] = 2;
        }
    }
    program->expression_depth++;
    int result = compile_input(program, ret, 0);
    program->expression_depth--;
    free(program->node_base);
    free(program->node_state);
    program->graph = caller;
    program->node_base = caller_base;
    program->node_state = caller_state;
    afree(inputs);
    return result;
}

static int event_input_index(const node *event, int output)
{
    int index = 0;
    for (int i = 0; i < event->output_count; i++) {
        if (event->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (i == output) {
            return index;
        }
        index++;
    }
    return -1;
}

static int aggregate_output_count(const node *n)
{
    int count = 0;
    for (int i = 0; i < n->output_count; i++) {
        count += !n->output_pins[i].field_source;
    }
    return count;
}

static int compile_component_get(system_program *program, const node *target)
{
    int index = program_node_index(program, target);
    int output_count = aggregate_output_count(target);
    if (index < 0 || !target->desc->access || target->desc->access->read_count != 1 ||
        output_count == 0 || output_count > SYSTEM_MAX_PINS) {
        return -1;
    }
    if (program->node_state[index] == 2) {
        return 0;
    }
    if (program->node_state[index] == 1) {
        return -1;
    }
    const ecs_component_desc *component = component_by_id(target->desc->access->reads[0]);
    if (!component || !component->type) {
        return -1;
    }
    program->node_state[index] = 1;
    if (output_count == 1 && pin_matches_component(&target->output_pins[0], component)) {
        system_instruction *instruction = add_instruction(program, SYSTEM_OP_GET, target);
        instruction->component = component;
        instruction->output = program_register(program, target, 0);
        program->node_state[index] = 2;
        return 0;
    }
    if (output_count != component->type->field_count) {
        program->node_state[index] = 0;
        return -1;
    }
    for (int i = 0; i < output_count; i++) {
        if (!pin_matches_field(&target->output_pins[i], &component->type->fields[i])) {
            program->node_state[index] = 0;
            return -1;
        }
    }
    pin_spec component_pin = {
        .type = PIN_STRUCT,
        .type_name = component->type->name,
        .container = PIN_CONTAINER_SINGLE,
    };
    int source = new_register(program, &component_pin);
    system_instruction *get = add_instruction(program, SYSTEM_OP_GET, target);
    get->component = component;
    get->output = source;
    system_instruction *break_op = add_instruction(program, SYSTEM_OP_BREAK, target);
    break_op->type = component->type;
    break_op->input_count = 1;
    break_op->inputs[0] = source;
    break_op->output_count = output_count;
    for (int i = 0; i < output_count; i++) {
        break_op->outputs[i] = program_register(program, target, i);
    }
    program->node_state[index] = 2;
    return 0;
}

static int compile_component_set(system_program *program, const node *target)
{
    int index = program_node_index(program, target);
    int output_count = aggregate_output_count(target);
    if (index < 0 || !target->desc->access || target->desc->access->write_count != 1 ||
        (output_count != 0 &&
         (output_count != 2 || target->output_pins[1].kind != PIN_DATA)) ||
        node_has_exec_pins(target)) {
        return -1;
    }
    if (program->node_state[index] == 2) {
        return 0;
    }
    if (program->node_state[index] == 1) {
        return -1;
    }
    const ecs_component_desc *component = component_by_id(target->desc->access->writes[0]);
    if (!component || !component->type) {
        return -1;
    }
    program->node_state[index] = 1;
    int input = -1;
    if (target->input_count == 1 &&
        pin_matches_component(&target->input_pins[0], component)) {
        input = compile_input(program, target, 0);
    } else {
        if (target->input_count != component->type->field_count ||
            target->input_count > SYSTEM_MAX_PINS) {
            program->node_state[index] = 0;
            return -1;
        }
        int fields[SYSTEM_MAX_PINS];
        for (int i = 0; i < target->input_count; i++) {
            if (!pin_matches_field(&target->input_pins[i], &component->type->fields[i])) {
                program->node_state[index] = 0;
                return -1;
            }
            fields[i] = compile_input(program, target, i);
            if (fields[i] < 0) {
                program->node_state[index] = 0;
                return -1;
            }
        }
        pin_spec component_pin = {
            .type = PIN_STRUCT,
            .type_name = component->type->name,
            .container = PIN_CONTAINER_SINGLE,
        };
        input = new_register(program, &component_pin);
        system_instruction *make = add_instruction(program, SYSTEM_OP_MAKE, target);
        make->type = component->type;
        make->output = input;
        make->input_count = target->input_count;
        for (int i = 0; i < make->input_count; i++) {
            make->inputs[i] = fields[i];
        }
    }
    if (input < 0) {
        program->node_state[index] = 0;
        return -1;
    }
    system_instruction *set = add_instruction(program, SYSTEM_OP_SET, target);
    set->component = component;
    set->input_count = 1;
    set->inputs[0] = input;
    if (output_count > 1 && target->output_pins[1].kind == PIN_DATA) {
        int output = program_register(program, target, 1);
        if (output < 0) {
            program->node_state[index] = 0;
            return -1;
        }
        program->aliases[output] = input;
    }
    program->node_state[index] = 2;
    return 0;
}

static int compile_dataflow(system_program *program)
{
    int set_count = 0;
    for (int i = 0; i < acount(program->graph->nodes); i++) {
        const node *target = program->graph->nodes[i];
        if (target->desc->component_kind != NODE_COMPONENT_SET_SELF ||
            node_has_exec_pins(target)) {
            continue;
        }
        if (!game_system_node_supported(target->desc)) {
            return -1;
        }
        if (compile_component_set(program, target) != 0) {
            return -1;
        }
        set_count++;
    }
    return set_count ? 0 : -1;
}

static int compile_value(system_program *program, const node *target, int output)
{
    int index = program_node_index(program, target);
    int result = program_register(program, target, output);
    if (index < 0 || result < 0) {
        return -1;
    }
    if (target->output_pins[output].field_source) {
        int original = program->node_base[index] + output;
        if (program->aliases[original] >= 0) {
            return result;
        }
        int source = -1;
        const type_field *field = graph_output_field(target, output, &source);
        if (!field) {
            return -1;
        }
        const type_desc *type = registry_type_by_name(
            target->output_pins[source].type_name
        );
        if (type->field_count > SYSTEM_MAX_PINS) {
            return -1;
        }
        int input = compile_value(program, target, source);
        if (input < 0) {
            return -1;
        }
        system_instruction *projection = add_instruction(program, SYSTEM_OP_BREAK, target);
        projection->type = type;
        projection->input_count = 1;
        projection->inputs[0] = input;
        projection->output_count = type->field_count;
        for (int i = 0; i < type->field_count; i++) {
            const type_field *member = &type->fields[i];
            pin_spec pin = { .type = member->type,
                             .type_name = member->type_name,
                             .kind = PIN_DATA,
                             .container = PIN_CONTAINER_SINGLE };
            int value = new_register(program, &pin);
            projection->outputs[i] = value;
            if (member == field) {
                program->aliases[original] = value;
                result = value;
            }
        }
        return result;
    }
    int output_count = aggregate_output_count(target);
    if (program->node_state[index] == 2) {
        return result;
    }
    if (program->node_state[index] == 1) {
        return -1;
    }
    if (target->desc->flow_kind == NODE_FLOW_EVENT) {
        if (target->output_pins[output].kind != PIN_DATA ||
            event_input_index(target, output) != 0) {
            return -1;
        }
        program->aliases[result] = program->event_delta;
        program->node_state[index] = 2;
        return program->event_delta;
    }
    if (node_is(target, NODE_COMPONENT_GET_SELF)) {
        if (node_has_exec_pins(target)) {
            return compile_exec(program, target) == 0 ? result : -1;
        }
        return compile_component_get(program, target) == 0 ? result : -1;
    }
    if (!game_system_node_supported(target->desc)) {
        return -1;
    }

    program->node_state[index] = 1;
    const node_desc *desc = target->desc;
    system_instruction *instruction = NULL;
    int ok = 1;
    switch (desc->pure_kind) {
        case NODE_PURE_FUNCTION: {
            int value = compile_expression(program, target);
            if (value < 0) {
                ok = 0;
                break;
            }
            program->aliases[result] = value;
            result = value;
            break;
        }
        case NODE_PURE_READ:
            if (desc != &NODE_RandomReal) {
                ok = 0;
                break;
            }
            instruction = add_instruction(program, SYSTEM_OP_CALL, target);
            instruction->fn = desc->fn;
            instruction->output = result;
            break;
        case NODE_PURE_DELTA:
            if (output_count != 1) {
                ok = 0;
                break;
            }
            program->aliases[result] = program->event_delta;
            result = program->event_delta;
            break;
        case NODE_PURE_CONST:
            instruction = add_instruction(program, SYSTEM_OP_CONST, target);
            instruction->output = result;
            instruction->output_count = 1;
            instruction->values[0] = target->output_pins[output].default_value;
            break;
        case NODE_PURE_COPY: {
            if (target->input_count != 1 || output_count != 1) {
                ok = 0;
                break;
            }
            int input = compile_input(program, target, 0);
            if (input < 0) {
                ok = 0;
                break;
            }
            program->aliases[result] = input;
            result = input;
            break;
        }
        case NODE_PURE_BREAK:
            if (!desc->type || desc->type->field_count > SYSTEM_MAX_PINS ||
                target->input_count != 1 || output_count != desc->type->field_count) {
                ok = 0;
                break;
            }
            int input = compile_input(program, target, 0);
            instruction = add_instruction(program, SYSTEM_OP_BREAK, target);
            instruction->type = desc->type;
            instruction->input_count = 1;
            instruction->inputs[0] = input;
            instruction->output_count = output_count;
            for (int i = 0; i < output_count; i++) {
                instruction->outputs[i] = program_register(program, target, i);
            }
            ok = input >= 0;
            break;
        case NODE_PURE_MAKE:
            if (!desc->type || desc->type->field_count > SYSTEM_MAX_PINS ||
                target->input_count != desc->type->field_count || output_count != 1) {
                ok = 0;
                break;
            }
            int make_inputs[SYSTEM_MAX_PINS];
            for (int i = 0; i < target->input_count; i++) {
                make_inputs[i] = compile_input(program, target, i);
                ok = ok && make_inputs[i] >= 0;
            }
            if (!ok) {
                break;
            }
            instruction = add_instruction(program, SYSTEM_OP_MAKE, target);
            instruction->type = desc->type;
            instruction->output = result;
            instruction->input_count = target->input_count;
            for (int i = 0; i < instruction->input_count; i++) {
                instruction->inputs[i] = make_inputs[i];
            }
            break;
        case NODE_PURE_CALL:
            if (!desc->fn || target->input_count > SYSTEM_MAX_PINS ||
                output_count != 1) {
                ok = 0;
                break;
            }
            int call_inputs[SYSTEM_MAX_PINS];
            for (int i = 0; i < target->input_count; i++) {
                if (target->input_pins[i].kind != PIN_DATA) {
                    ok = 0;
                    break;
                }
                call_inputs[i] = compile_input(program, target, i);
                ok = ok && call_inputs[i] >= 0;
            }
            if (!ok) {
                break;
            }
            instruction = add_instruction(program, SYSTEM_OP_CALL, target);
            instruction->fn = desc->fn;
            instruction->output = result;
            instruction->input_count = target->input_count;
            for (int i = 0; i < instruction->input_count; i++) {
                instruction->inputs[i] = call_inputs[i];
            }
            if (call_has_fast_signature(program, instruction)) {
                instruction->fast = desc->fast;
                instruction->direct = desc->direct;
                instruction->direct_count = desc->direct_count;
            }
            break;
        default: ok = 0; break;
    }
    if (!ok) {
        program->node_state[index] = 0;
        return -1;
    }
    program->node_state[index] = 2;
    return result;
}

static int compile_exec(system_program *program, const node *target)
{
    int index = program_node_index(program, target);
    if (index < 0) {
        return -1;
    }
    if (program->node_state[index] == 2) {
        return 0;
    }
    if (program->node_state[index] == 1 || target->input_count > SYSTEM_MAX_PINS) {
        return -1;
    }
    bool get = node_is(target, NODE_COMPONENT_GET_SELF);
    if (!get && !node_is(target, NODE_COMPONENT_SET_SELF)) {
        return -1;
    }
    program->node_state[index] = 1;
    system_instruction *instruction = NULL;
    if (get) {
        if (!target->desc->access || target->desc->access->read_count != 1 ||
            target->input_count != 1 || target->output_count < 2) {
            program->node_state[index] = 0;
            return -1;
        }
        instruction = add_instruction(program, SYSTEM_OP_GET, target);
        instruction->component = component_by_id(target->desc->access->reads[0]);
        instruction->output = program_register(program, target, 1);
        if (!instruction->component || instruction->output < 0) {
            program->node_state[index] = 0;
            return -1;
        }
    } else {
        if (!target->desc->access || target->desc->access->write_count != 1 ||
            target->input_count < 2 || target->output_count < 1) {
            program->node_state[index] = 0;
            return -1;
        }
        int value_pin = -1;
        for (int i = 0; i < target->input_count; i++) {
            if (target->input_pins[i].kind == PIN_DATA) {
                value_pin = i;
                break;
            }
        }
        int input = value_pin >= 0 ? compile_input(program, target, value_pin) : -1;
        instruction = add_instruction(program, SYSTEM_OP_SET, target);
        instruction->component = component_by_id(target->desc->access->writes[0]);
        instruction->input_count = 1;
        instruction->inputs[0] = input;
        if (!instruction->component || input < 0) {
            program->node_state[index] = 0;
            return -1;
        }
        if (target->output_count > 1 && target->output_pins[1].kind == PIN_DATA) {
            int output = program_register(program, target, 1);
            if (output < 0) {
                program->node_state[index] = 0;
                return -1;
            }
            program->aliases[output] = input;
        }
    }
    program->node_state[index] = 2;
    int found = 0;
    const node *next = exec_target(program->graph, target, &found);
    if (found && !next) {
        return -1;
    }
    return next ? compile_exec(program, next) : 0;
}

static int instruction_uses(const system_instruction *instruction, int value)
{
    for (int i = 0; i < instruction->input_count; i++) {
        if (instruction->inputs[i] == value) {
            return 1;
        }
    }
    return 0;
}

static int instruction_writes_component(const system_instruction *instruction, ecs_component_id component)
{
    if (instruction->op == SYSTEM_OP_SET && instruction->component &&
        instruction->component->id == component) {
        return 1;
    }
    const node_access *access = instruction->node->desc->access;
    if (!access) {
        return 0;
    }
    for (int i = 0; i < access->write_count; i++) {
        if (access->writes[i] == component) {
            return 1;
        }
    }
    return 0;
}

static void mark_get_aliases(system_program *program)
{
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *get = &program->instructions[i];
        if (get->op != SYSTEM_OP_GET) {
            continue;
        }
        int last_use = i;
        for (int j = i + 1; j < acount(program->instructions); j++) {
            if (instruction_uses(&program->instructions[j], get->output)) {
                last_use = j;
            }
        }
        get->alias = 1;
        for (int j = i + 1; j < last_use; j++) {
            if (instruction_writes_component(
                    &program->instructions[j],
                    get->component->id
                )) {
                get->alias = 0;
                break;
            }
        }
    }
}

static const system_instruction *get_for_value(const system_program *program, int value)
{
    for (int i = 0; i < acount(program->instructions); i++) {
        const system_instruction *instruction = &program->instructions[i];
        if (instruction->op == SYSTEM_OP_GET && instruction->output == value) {
            return instruction;
        }
    }
    return NULL;
}

static void mark_break_aliases(system_program *program)
{
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *break_op = &program->instructions[i];
        if (break_op->op != SYSTEM_OP_BREAK) {
            continue;
        }
        const system_instruction *get = get_for_value(program, break_op->inputs[0]);
        if (!get || !get->alias) {
            continue;
        }
        for (int field = 0; field < break_op->output_count; field++) {
            int last_use = i;
            int value = break_op->outputs[field];
            for (int j = i + 1; j < acount(program->instructions); j++) {
                if (instruction_uses(&program->instructions[j], value)) {
                    last_use = j;
                }
            }
            break_op->field_alias[field] = 1;
            for (int j = i + 1; j < last_use; j++) {
                if (instruction_writes_component(
                        &program->instructions[j],
                        get->component->id
                    )) {
                    break_op->field_alias[field] = 0;
                    break;
                }
            }
        }
    }
}

static void *slot_data(system_program *program, int index);
static void store_pin(system_program *program, int index, pin_value value);

/* Typed storage keeps POD values out of pin_value and the per-entity arena.
 * The generic program remains available for every other synchronous program. */
static int slot_is_typed(const system_slot *slot)
{
    return slot->type == PIN_FLOW || slot->size > 0;
}

static int program_can_run_typed(const system_program *program)
{
    for (int i = 0; i < program->value_count; i++) {
        if (!slot_is_typed(&program->slots[i])) {
            return 0;
        }
    }
    for (int i = 0; i < acount(program->instructions); i++) {
        const system_instruction *instruction = &program->instructions[i];
        if (instruction->op == SYSTEM_OP_CALL && !instruction->fast) {
            return 0;
        }
        if (instruction->op == SYSTEM_OP_CONST &&
            instruction->values[0].type == PIN_STRUCT && !instruction->values[0].ptr) {
            return 0;
        }
    }
    return 1;
}

static size_t align_storage_offset(size_t offset)
{
    size_t alignment = _Alignof(max_align_t);
    size_t remainder = offset % alignment;
    return remainder ? offset + alignment - remainder : offset;
}

static void program_prepare_storage(system_program *program)
{
    for (int i = 0; i < program->value_count; i++) {
        system_slot *slot = &program->slots[i];
        if (!slot->size) {
            continue;
        }
        program->storage_size = align_storage_offset(program->storage_size);
        slot->offset = program->storage_size;
        program->storage_size += slot->size;
    }
    program->storage = (unsigned char *)xmalloc(program->storage_size);
    for (int i = 0; i < program->value_count; i++) {
        if (program->slots[i].size) {
            program->slots[i].data = program->storage + program->slots[i].offset;
        }
    }
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *instruction = &program->instructions[i];
        if (instruction->op == SYSTEM_OP_CONST) {
            store_pin(program, instruction->output, instruction->values[0]);
        }
    }
}

static graph_ir_op ir_op(system_op op)
{
    switch (op) {
        case SYSTEM_OP_CONST: return GRAPH_IR_OP_CONST;
        case SYSTEM_OP_GET: return GRAPH_IR_OP_GET_COMPONENT;
        case SYSTEM_OP_SET: return GRAPH_IR_OP_SET_COMPONENT;
        case SYSTEM_OP_BREAK: return GRAPH_IR_OP_BREAK_STRUCT;
        case SYSTEM_OP_MAKE: return GRAPH_IR_OP_MAKE_STRUCT;
        case SYSTEM_OP_CALL: return GRAPH_IR_OP_CALL;
    }
    return GRAPH_IR_OP_CONST;
}

static graph_ir_value ir_value(const system_slot *slot)
{
    graph_ir_value_kind kind = GRAPH_IR_VALUE_MANAGED;
    unsigned flags = 0;
    if (slot->type == PIN_FLOW) {
        kind = GRAPH_IR_VALUE_FLOW;
    } else if (slot->size) {
        kind = GRAPH_IR_VALUE_FIXED;
        flags = GRAPH_IR_VALUE_POD;
    }
    return (graph_ir_value){
        .type = slot->type,
        .container = slot->container,
        .kind = kind,
        .type_name = slot->type_name,
        .type_desc = slot->description,
        .size = slot->size,
        .alignment = slot->size ? _Alignof(max_align_t) : 0,
        .layout_hash = slot->description ? ecs_layout_hash(slot->description)
                                         : graph_ir_name_id(slot->type_name),
        .flags = flags,
    };
}

static void program_build_ir(system_program *program)
{
    for (int i = 0; i < program->value_count; i++) {
        int value = graph_ir_add_value(&program->ir, ir_value(&program->slots[i]));
        assert(value == i);
    }
    program->ir.event_delta = program->event_delta;
    for (int i = 0; i < acount(program->instructions); i++) {
        const system_instruction *instruction = &program->instructions[i];
        const node_desc *desc = instruction->node->desc;
        graph_ir_instruction ir_instruction = {
            .op = ir_op(instruction->op),
            .node = instruction->node,
            .type = instruction->type,
            .access = desc->access,
            .fn = instruction->fn,
            .fast = instruction->fast,
            .direct = instruction->direct,
            .direct_count = instruction->direct_count,
            .operation_id = instruction->op == SYSTEM_OP_CALL &&
                                    instruction->fn != desc->fn
                              ? 0
                              : graph_ir_name_id(desc->name),
            .resource_id = instruction->component ? instruction->component->id : 0,
            .output = instruction->op == SYSTEM_OP_SET || instruction->op == SYSTEM_OP_BREAK
                        ? -1
                        : instruction->output,
            .input_count = instruction->input_count,
            .output_count = instruction->output_count,
            .constant = instruction->values[0],
        };
        memcpy(ir_instruction.inputs, instruction->inputs, sizeof ir_instruction.inputs);
        memcpy(
            ir_instruction.outputs,
            instruction->outputs,
            sizeof ir_instruction.outputs
        );
        int added = graph_ir_add_instruction(&program->ir, &ir_instruction);
        assert(added == i);
    }
    assert(graph_ir_validate(&program->ir, NULL) == 0);
}

static system_program *program_new(const behavior_asset *definition, const node *tick)
{
    system_program *program = (system_program *)xcalloc(1, sizeof *program);
    graph_program_init(&program->shared);
    program->graph = definition->graph;
    int output_total = 0;
    for (int i = 0; i < acount(program->graph->nodes); i++) {
        program->node_base = (int *)
            xrealloc(program->node_base, (size_t)(i + 1) * sizeof *program->node_base);
        program->node_base[i] = output_total;
        output_total += program->graph->nodes[i]->output_count;
    }
    program->node_state = (int *)
        xcalloc((size_t)acount(program->graph->nodes), sizeof *program->node_state);
    for (int i = 0; i < acount(program->graph->nodes); i++) {
        if (program->graph->nodes[i]->disabled) {
            free(program->node_base);
            free(program->node_state);
            free(program->aliases);
            free(program->slots);
            free(program);
            return NULL;
        }
    }
    program->value_count = output_total;
    if (output_total) {
        program->aliases = (int *)xmalloc((size_t)output_total * sizeof *program->aliases);
        for (int i = 0; i < output_total; i++) {
            program->aliases[i] = -1;
        }
        program->slots = (system_slot *)
            xcalloc((size_t)output_total, sizeof *program->slots);
        for (int i = 0; i < acount(program->graph->nodes); i++) {
            const node *target = program->graph->nodes[i];
            for (int output = 0; output < target->output_count; output++) {
                set_slot(
                    &program->slots[program->node_base[i] + output],
                    &target->output_pins[output]
                );
            }
        }
    }
    pin_spec delta_pin = { .type = PIN_F32, .type_name = "float", .kind = PIN_DATA };
    program->event_delta = new_register(program, &delta_pin);
    int found = 0;
    const node *next = exec_target(program->graph, tick, &found);
    int compile_result = found ? (next ? compile_exec(program, next) : -1)
                       : program->graph->dataflow ? compile_dataflow(program)
                                                  : -1;
    if (compile_result != 0) {
        free(program->node_base);
        free(program->node_state);
        free(program->aliases);
        free(program->slots);
        free(program);
        return NULL;
    }
    program->values = (pin_value *)
        xcalloc((size_t)program->value_count, sizeof *program->values);
    program_build_ir(program);
    mark_get_aliases(program);
    mark_break_aliases(program);
    program->typed = !program->dynamic_expression && program_can_run_typed(program);
    if (program->typed) {
        program_prepare_storage(program);
        graph_program_compile_backend(&program->shared, NULL);
    }
    arena_init(&program->arena, 4096);
    return program;
}

static void program_free(system_program *program)
{
    if (!program) {
        return;
    }
    arena_free(&program->arena);
    graph_program_free(&program->shared);
    free(program->sljit_columns);
    free(program->sljit_bases);
    free(program->storage);
    free(program->aliases);
    free(program->slots);
    free(program->values);
    afree(program->instructions);
    free(program->node_base);
    free(program->node_state);
    free(program);
}

static int program_bind_world(system_program *program, ecs_world *world)
{
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *instruction = &program->instructions[i];
        if (!instruction->component) {
            continue;
        }
        instruction->world_component = ecs_world_component(
            world,
            instruction->component->id
        );
        if (!instruction->world_component) {
            return -1;
        }
    }
    if (!program->sljit) {
        return 0;
    }
    int count = graph_sljit_column_count(program->sljit);
    if (count != program->sljit_column_count) {
        free(program->sljit_columns);
        free(program->sljit_bases);
        program->sljit_columns = (graph_jit_column *)
            xcalloc((size_t)count, sizeof *program->sljit_columns);
        program->sljit_bases = (void **)xcalloc((size_t)count, sizeof *program->sljit_bases);
        program->sljit_column_count = count;
    }
    for (int i = 0; i < count; i++) {
        const ecs_component_desc *component = ecs_world_component(
            world,
            graph_sljit_column_id_at(program->sljit, i)
        );
        if (!component) {
            return -1;
        }
        program->sljit_columns[i].resource_id = component->id;
        program->sljit_columns[i].data = component->column_base;
        program->sljit_columns[i].size = component->element_size;
        program->sljit_bases[i] = component->column_base;
    }
    return 0;
}

static void *component_data(const ecs_component_desc *component, ecs_entity entity)
{
    return (unsigned char *)component->column_base +
           (size_t)entity.slot * component->element_size;
}

typedef struct
{
    const system_program *program;
    flow_error *error;
} system_jit_user;

static void system_jit_error(void *user, int, uint64_t operation_id)
{
    system_jit_user *jit = (system_jit_user *)user;
    for (int i = 0; i < acount(jit->program->ir.instructions); i++) {
        const graph_ir_instruction *instruction = &jit->program->ir.instructions[i];
        if (instruction->operation_id == operation_id) {
            set_error(jit->error, FLOW_ERR_NODE, instruction->node, "SLJIT node failed");
            return;
        }
    }
    set_error(jit->error, FLOW_ERR_NODE, NULL, "SLJIT node failed");
}

static int program_run_generic(system_program *program, ecs_entity entity, double delta, flow_error *error)
{
    arena_reset(&program->arena);
    memset(program->values, 0, (size_t)program->value_count * sizeof *program->values);
    program->values[program->event_delta] = (pin_value){
        .type = PIN_F32,
        .type_name = pin_type_name(PIN_F32),
        .f = (float)delta,
    };
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *instruction = &program->instructions[i];
        switch (instruction->op) {
            case SYSTEM_OP_CONST:
                program->values[instruction->output] = instruction->values[0];
                break;
            case SYSTEM_OP_GET: {
                void *source = component_data(instruction->world_component, entity);
                void *copy = arena_alloc(&program->arena, instruction->component->element_size);
                memcpy(copy, source, instruction->component->element_size);
                program->values[instruction->output] = (pin_value){
                    .type = PIN_STRUCT,
                    .type_name = instruction->component->type->name,
                    .ptr = copy,
                };
                break;
            }
            case SYSTEM_OP_SET: {
                pin_value value = program->values[instruction->inputs[0]];
                void *destination = component_data(instruction->world_component, entity);
                if (!value.ptr) {
                    set_error(error, FLOW_ERR_NODE, instruction->node, "component Set requires a value");
                    return FLOW_ERR_NODE;
                }
                memcpy(destination, value.ptr, instruction->component->element_size);
                break;
            }
            case SYSTEM_OP_BREAK: {
                pin_value value = program->values[instruction->inputs[0]];
                if (!value.ptr) {
                    set_error(error, FLOW_ERR_NODE, instruction->node, "struct Break requires a value");
                    return FLOW_ERR_NODE;
                }
                for (int field = 0; field < instruction->output_count; field++) {
                    pin_read_field(
                        value.ptr,
                        &instruction->type->fields[field],
                        &program->values[instruction->outputs[field]]
                    );
                }
                break;
            }
            case SYSTEM_OP_MAKE: {
                void *value = arena_alloc(&program->arena, instruction->type->size);
                memset(value, 0, instruction->type->size);
                for (int field = 0; field < instruction->input_count; field++) {
                    pin_write_field(
                        value,
                        &instruction->type->fields[field],
                        program->values[instruction->inputs[field]]
                    );
                }
                program->values[instruction->output] = (pin_value){
                    .type = PIN_STRUCT,
                    .type_name = instruction->type->name,
                    .ptr = value,
                };
                break;
            }
            case SYSTEM_OP_CALL: {
                pin_value args[SYSTEM_MAX_PINS];
                for (int arg = 0; arg < instruction->input_count; arg++) {
                    args[arg] = program->values[instruction->inputs[arg]];
                }
                program->values[instruction->output] = instruction->fn(
                    args,
                    instruction->input_count,
                    &program->arena
                );
                if (program->values[instruction->output].type == PIN_FLOW) {
                    set_error(error, FLOW_ERR_NODE, instruction->node, "data function produced an invalid result");
                    return FLOW_ERR_NODE;
                }
                break;
            }
        }
    }
    return FLOW_OK;
}

static void *slot_data(system_program *program, int index)
{
    assert(index >= 0 && index < program->value_count);
    return program->slots[index].data;
}

static void store_pin(system_program *program, int index, pin_value value)
{
    system_slot *slot = &program->slots[index];
    void *destination = slot_data(program, index);
    switch (slot->type) {
        case PIN_BOOL: memcpy(destination, &value.b, sizeof value.b); break;
        case PIN_I8: memcpy(destination, &value.i8, sizeof value.i8); break;
        case PIN_U8: memcpy(destination, &value.u8, sizeof value.u8); break;
        case PIN_I16: memcpy(destination, &value.i16, sizeof value.i16); break;
        case PIN_U16: memcpy(destination, &value.u16, sizeof value.u16); break;
        case PIN_I32: memcpy(destination, &value.i, sizeof value.i); break;
        case PIN_U32: memcpy(destination, &value.u, sizeof value.u); break;
        case PIN_I64: memcpy(destination, &value.i64, sizeof value.i64); break;
        case PIN_U64: memcpy(destination, &value.u64, sizeof value.u64); break;
        case PIN_F32: memcpy(destination, &value.f, sizeof value.f); break;
        case PIN_F64: memcpy(destination, &value.d, sizeof value.d); break;
        case PIN_NAME:
        case PIN_STR: memcpy(destination, &value.str, sizeof value.str); break;
        case PIN_HANDLE:
            memcpy(destination, &value.u64, sizeof value.u64);
            break;
        case PIN_ENUM: memcpy(destination, &value.e, sizeof value.e); break;
        case PIN_STRUCT:
            assert(value.ptr);
            memcpy(destination, value.ptr, slot->size);
            break;
        case PIN_FLOW: break;
    }
}

static int program_run_typed(system_program *program, ecs_entity entity, double delta, flow_error *error)
{
    *(float *)slot_data(program, program->event_delta) = (float)delta;
    for (int i = 0; i < acount(program->instructions); i++) {
        system_instruction *instruction = &program->instructions[i];
        switch (instruction->op) {
            case SYSTEM_OP_CONST: break;
            case SYSTEM_OP_GET: {
                void *source = component_data(instruction->world_component, entity);
                if (instruction->alias) {
                    program->slots[instruction->output].data = source;
                } else {
                    memcpy(
                        slot_data(program, instruction->output),
                        source,
                        instruction->component->element_size
                    );
                }
                break;
            }
            case SYSTEM_OP_SET: {
                void *destination = component_data(instruction->world_component, entity);
                const void *source = slot_data(program, instruction->inputs[0]);
                if (destination != source) {
                    memcpy(destination, source, instruction->component->element_size);
                }
                break;
            }
            case SYSTEM_OP_BREAK: {
                const unsigned char *source = (const unsigned char *)
                    slot_data(program, instruction->inputs[0]);
                for (int field = 0; field < instruction->output_count; field++) {
                    const type_field *type_field = &instruction->type->fields[field];
                    if (instruction->field_alias[field]) {
                        program->slots[instruction->outputs[field]]
                            .data = (void *)(source + type_field->offset);
                    } else {
                        memcpy(
                            slot_data(program, instruction->outputs[field]),
                            source + type_field->offset,
                            type_field->size
                        );
                    }
                }
                break;
            }
            case SYSTEM_OP_MAKE: {
                unsigned char *destination = (unsigned char *)
                    slot_data(program, instruction->output);
                memset(destination, 0, instruction->type->size);
                for (int field = 0; field < instruction->input_count; field++) {
                    const type_field *type_field = &instruction->type->fields[field];
                    memcpy(
                        destination + type_field->offset,
                        slot_data(program, instruction->inputs[field]),
                        type_field->size
                    );
                }
                break;
            }
            case SYSTEM_OP_CALL: {
                void *output = slot_data(program, instruction->output);
                int status;
                switch (instruction->direct_count) {
                    case 1: status = instruction->direct.zero(output); break;
                    case 2:
                        status = instruction->direct.one(
                            output,
                            slot_data(program, instruction->inputs[0])
                        );
                        break;
                    case 3:
                        status = instruction->direct.two(
                            output,
                            slot_data(program, instruction->inputs[0]),
                            slot_data(program, instruction->inputs[1])
                        );
                        break;
                    case 4:
                        status = instruction->direct.three(
                            output,
                            slot_data(program, instruction->inputs[0]),
                            slot_data(program, instruction->inputs[1]),
                            slot_data(program, instruction->inputs[2])
                        );
                        break;
                    case 5:
                        status = instruction->direct.four(
                            output,
                            slot_data(program, instruction->inputs[0]),
                            slot_data(program, instruction->inputs[1]),
                            slot_data(program, instruction->inputs[2]),
                            slot_data(program, instruction->inputs[3])
                        );
                        break;
                    default: {
                        const void *args[SYSTEM_MAX_PINS];
                        for (int arg = 0; arg < instruction->input_count; arg++) {
                            args[arg] = slot_data(program, instruction->inputs[arg]);
                        }
                        status = instruction->fast(output, args);
                        break;
                    }
                }
                if (status != 0) {
                    set_error(error, FLOW_ERR_NODE, instruction->node, "fast node failed");
                    return FLOW_ERR_NODE;
                }
                break;
            }
        }
    }
    return FLOW_OK;
}

static behavior_instance *make_instance(game_system *system)
{
    behavior_instance *instance = behavior_instance_create(system->definition, NULL, NULL, NULL);
    if (!instance) {
        return NULL;
    }
    behavior_instance_set_activation_pool(instance, 1);
    behavior_instance_set_trace_enabled(instance, 0);
    behavior_instance_bind_service(instance, "ecs.world", system->world);
    return instance;
}

static int rebuild(game_system *system)
{
    program_free(system->program);
    system->program = NULL;
    behavior_instance_destroy(system->instance);
    system->instance = NULL;
    compile_report_free(&system->compile);
    compile_report_init(&system->compile);
    behavior_asset_compile_for_system(system->definition, &system->compile);
    afree(system->query);
    ecs_query_expr_free(system->compiled_query_expression);
    system->compiled_query_expression = NULL;
    system->tick = NULL;
    system->match_count = 0;
    build_query(system);
    if (compile_report_blocked(&system->compile) || !system->tick) {
        return -1;
    }
    system->program = program_new(system->definition, system->tick);
    if (system->program && program_bind_world(system->program, system->world) == 0) {
        afree(system->query);
        for (int i = 0; i < acount(system->program->instructions); i++) {
            const ecs_component_desc
                *component = system->program->instructions[i].component;
            if (component) {
                add_query_component(system, component->id);
            }
        }
        ecs_query_expr *query = ecs_query_expr_new(ECS_QUERY_ALL);
        if (system->query_expression) {
            int added = ecs_query_expr_add(query, ecs_query_expr_clone(system->query_expression));
            assert(added == ECS_OK);
            (void)added;
        }
        for (int i = 0; i < acount(system->query); i++) {
            int added = ecs_query_expr_add(query, ecs_query_expr_has(system->query[i]));
            assert(added == ECS_OK);
            (void)added;
        }
        system->compiled_query_expression = query;
        return 0;
    }
    program_free(system->program);
    system->program = NULL;
    if (system->definition->graph->dataflow) {
        return -1;
    }
    system->instance = make_instance(system);
    if (system->instance) {
        ecs_query_expr *query = ecs_query_expr_new(ECS_QUERY_ALL);
        if (system->query_expression) {
            int added = ecs_query_expr_add(query, ecs_query_expr_clone(system->query_expression));
            assert(added == ECS_OK);
            (void)added;
        }
        for (int i = 0; i < acount(system->query); i++) {
            int added = ecs_query_expr_add(query, ecs_query_expr_has(system->query[i]));
            assert(added == ECS_OK);
            (void)added;
        }
        system->compiled_query_expression = query;
    }
    return system->instance ? 0 : -1;
}

game_system *game_system_new(ecs_world *world, behavior_asset *definition)
{
    if (!world || !definition) {
        return NULL;
    }
    game_system *system = (game_system *)xcalloc(1, sizeof *system);
    system->world = world;
    system->definition = definition;
    compile_report_init(&system->compile);
    rebuild(system);
    return system;
}

void game_system_free(game_system *system)
{
    if (!system) {
        return;
    }
    program_free(system->program);
    behavior_instance_destroy(system->instance);
    ecs_query_expr_free(system->query_expression);
    ecs_query_expr_free(system->compiled_query_expression);
    compile_report_free(&system->compile);
    afree(system->query);
    afree(system->snapshot);
    afree(system->batch_slots);
    behavior_asset_free(system->definition);
    free(system);
}

int game_system_set_query(game_system *system, const ecs_query_expr *expression)
{
    if (!system) {
        return -1;
    }
    ecs_query_expr *copy = expression ? ecs_query_expr_clone(expression) : NULL;
    ecs_query_expr_free(system->query_expression);
    system->query_expression = copy;
    return rebuild(system);
}

static int run_interpreted(game_system *system, ecs_entity entity, double now, double delta, flow_error *error)
{
    pin_value owner = {
        .type = PIN_HANDLE,
        .u64 = ecs_entity_to_u64(entity),
        .type_name = "ecs.entity",
    };
    pin_value tick_delta = {
        .type = PIN_F32,
        .f = (float)delta,
        .type_name = pin_type_name(PIN_F32),
    };
    flow_inputs inputs = { &tick_delta, 1, NULL };
    if (behavior_instance_rebind(system->instance, &owner) != 0) {
        set_error(error, FLOW_ERR_STATE, NULL, "system instance is busy");
        return FLOW_ERR_STATE;
    }
    flow_run *run = behavior_instance_enqueue(system->instance, system->tick, inputs, error);
    if (!run) {
        return error && error->code ? error->code : FLOW_ERR_STATE;
    }
    int result = behavior_instance_tick(system->instance, now);
    flow_state state = flow_run_status(run);
    if (result < 0 || state == FLOW_FAILED) {
        if (error && flow_run_error(run)) {
            *error = *flow_run_error(run);
        }
        behavior_instance_release_activation(system->instance, run);
        return result < 0 ? result : FLOW_ERR_NODE;
    }
    if (state != FLOW_COMPLETED) {
        behavior_instance_stop_activation(system->instance, run);
        behavior_instance_release_activation(system->instance, run);
        set_error(error, FLOW_ERR_STATE, system->tick, "System Tick must complete in one frame");
        return FLOW_ERR_STATE;
    }
    behavior_instance_release_activation(system->instance, run);
    return FLOW_OK;
}

static int run_sljit_batch_query(
    game_system *system,
    ecs_query *query,
    graph_sljit_batch_entry_fn entry,
    graph_jit_column *columns,
    int column_count,
    double delta,
    const graph_jit_host *host
)
{
    aclear(system->batch_slots);
    ecs_entity entity;
    int result;
    while ((result = ecs_query_next(query, &entity)) > 0) {
        apush(system->batch_slots, entity.slot);
    }
    if (result < 0) {
        return FLOW_ERR_STATE;
    }
    uint32_t *slots = system->batch_slots;
    int entity_count = acount(slots);
    if (entity_count) {
        graph_sljit_batch_context context = {
            .abi_version = GRAPH_JIT_ABI_VERSION,
            .entity_count = (uint32_t)entity_count,
            .delta_seconds = (float)delta,
            .columns = columns,
            .slots = slots,
            .column_count = column_count,
            .host = host,
        };
        if (entry(&context) != GRAPH_JIT_OK) {
            return FLOW_ERR_NODE;
        }
    }
    system->match_count += entity_count;
    return FLOW_OK;
}

static int run_sljit_query(
    game_system *system,
    ecs_query *query,
    double monotonic_time,
    double delta,
    flow_error *error
)
{
    int column_count = system->program->sljit_column_count;
    graph_jit_column *columns = system->program->sljit_columns;
    void **bases = system->program->sljit_bases;

    system_jit_user user = { system->program, error };
    graph_jit_host host = {
        .user = &user,
        .error = system_jit_error,
    };
    graph_jit_entry_fn entry = graph_sljit_entry(system->program->sljit);
    graph_sljit_batch_entry_fn batch_entry = graph_sljit_batch_entry(
        system->program->sljit
    );
    assert(entry);
    if (batch_entry) {
        int result = run_sljit_batch_query(system, query, batch_entry, columns, column_count, delta, &host);
        return result;
    }

    graph_jit_context context = {
        .abi_version = GRAPH_JIT_ABI_VERSION,
        .delta_seconds = (float)delta,
        .monotonic_time = monotonic_time,
        .columns = columns,
        .column_count = column_count,
        .host = &host,
    };
    ecs_entity entity;
    int result;
    while ((result = ecs_query_next(query, &entity)) > 0) {
        for (int i = 0; i < column_count; i++) {
            columns[i].data = (unsigned char *)bases[i] +
                              (size_t)entity.slot * columns[i].size;
        }
        context.entity_slot = entity.slot;
        context.entity_generation = entity.generation;
        if (entry(&context) != GRAPH_JIT_OK) {
            return error && error->code ? error->code : FLOW_ERR_NODE;
        }
        system->match_count++;
    }
    return result < 0 ? FLOW_ERR_STATE : FLOW_OK;
}

static int run_typed_query(game_system *system, ecs_query *query, double delta, flow_error *error)
{
    ecs_entity entity;
    int result;
    while ((result = ecs_query_next(query, &entity)) > 0) {
        result = program_run_typed(system->program, entity, delta, error);
        if (result != FLOW_OK) {
            return result;
        }
        system->match_count++;
    }
    return result < 0 ? FLOW_ERR_STATE : FLOW_OK;
}

static int run_generic_query(game_system *system, ecs_query *query, double delta, flow_error *error)
{
    ecs_entity entity;
    int result;
    while ((result = ecs_query_next(query, &entity)) > 0) {
        result = program_run_generic(system->program, entity, delta, error);
        if (result != FLOW_OK) {
            return result;
        }
        system->match_count++;
    }
    return result < 0 ? FLOW_ERR_STATE : FLOW_OK;
}

int game_system_tick_mode(
    game_system *system,
    double monotonic_time,
    double delta_seconds,
    flow_error *error,
    game_system_exec_mode mode
)
{
    assert(mode >= GAME_SYSTEM_EXEC_AUTO && mode <= GAME_SYSTEM_EXEC_ORACLE);
    if (!system || (!system->program && (!system->instance || !system->tick))) {
        if (system && compile_report_blocked(&system->compile)) {
            const compile_diagnostic *diagnostic = compile_report_first_error(
                &system->compile
            );
            set_error(
                error,
                FLOW_ERR_COMPILE,
                graph_node_by_id(
                    behavior_asset_find_graph(system->definition, diagnostic->graph_id),
                    diagnostic->node_id
                ),
                diagnostic->message
            );
            return FLOW_ERR_COMPILE;
        }
        set_error(error, FLOW_ERR_STATE, NULL, "System has no Tick event");
        return FLOW_ERR_STATE;
    }
    if (mode == GAME_SYSTEM_EXEC_AUTO) {
        mode = system->program && system->program->sljit ? GAME_SYSTEM_EXEC_SJLIT
             : system->program && system->program->typed
                 ? GAME_SYSTEM_EXEC_TYPED_BYTECODE
                 : GAME_SYSTEM_EXEC_GENERIC_BYTECODE;
    }
    if (mode == GAME_SYSTEM_EXEC_ORACLE && system->definition->graph->dataflow) {
        set_error(error, FLOW_ERR_STATE, system->tick, "System dataflow has no interpreter oracle");
        return FLOW_ERR_STATE;
    }
    if (mode == GAME_SYSTEM_EXEC_ORACLE && !system->instance) {
        system->instance = make_instance(system);
    }
    if (mode == GAME_SYSTEM_EXEC_ORACLE) {
        behavior_instance_set_backend(system->instance, BEHAVIOR_ORACLE);
    }
    if (mode == GAME_SYSTEM_EXEC_GENERIC_BYTECODE && !system->program) {
        set_error(error, FLOW_ERR_STATE, system->tick, "generic System bytecode is unavailable");
        return FLOW_ERR_STATE;
    }
    if (mode == GAME_SYSTEM_EXEC_TYPED_BYTECODE &&
        (!system->program || !system->program->typed)) {
        set_error(error, FLOW_ERR_STATE, system->tick, "typed System bytecode is unavailable");
        return FLOW_ERR_STATE;
    }
    if (mode == GAME_SYSTEM_EXEC_SJLIT &&
        (!system->program || !system->program->sljit)) {
        set_error(error, FLOW_ERR_STATE, system->tick, "SLJIT System code is unavailable");
        return FLOW_ERR_STATE;
    }
    if (system->program && program_bind_world(system->program, system->world) != 0) {
        set_error(error, FLOW_ERR_STATE, system->tick, "System query component is not registered");
        return FLOW_ERR_STATE;
    }
    ecs_query query;
    int result = system->compiled_query_expression
                   ? ecs_query_begin_expression(system->world, &query, system->compiled_query_expression)
                   : ecs_query_begin(
                         system->world,
                         &query,
                         system->query,
                         acount(system->query)
                     );
    if (result != ECS_OK) {
        set_error(error, FLOW_ERR_STATE, NULL, "System query could not start");
        return FLOW_ERR_STATE;
    }
    system->match_count = 0;
    if (system->program && mode != GAME_SYSTEM_EXEC_ORACLE) {
        if (mode == GAME_SYSTEM_EXEC_SJLIT) {
            result = run_sljit_query(system, &query, monotonic_time, delta_seconds, error);
        } else if (
            mode == GAME_SYSTEM_EXEC_GENERIC_BYTECODE ||
            (!system->program->typed && mode != GAME_SYSTEM_EXEC_TYPED_BYTECODE)
        ) {
            result = run_generic_query(system, &query, delta_seconds, error);
        } else {
            result = run_typed_query(system, &query, delta_seconds, error);
        }
    } else {
        ecs_entity entity;
        while ((result = ecs_query_next(&query, &entity)) > 0) {
            result = run_interpreted(system, entity, monotonic_time, delta_seconds, error);
            if (result < 0) {
                ecs_query_end(&query);
                return result;
            }
            system->match_count++;
        }
        result = result < 0 ? FLOW_ERR_STATE : FLOW_OK;
    }
    ecs_query_end(&query);
    return result;
}

const behavior_asset *game_system_behavior_asset(const game_system *system)
{
    return system ? system->definition : NULL;
}

const compile_report *game_system_compile(const game_system *system)
{
    return system ? &system->compile : NULL;
}

int game_system_query_count(const game_system *system)
{
    return system ? acount(system->query) : 0;
}

int game_system_match_count(const game_system *system)
{
    return system ? system->match_count : 0;
}

int game_system_uses_bytecode(const game_system *system)
{
    return system && system->program != NULL;
}

int game_system_uses_typed_bytecode(const game_system *system)
{
    return system && system->program && system->program->typed;
}

int game_system_uses_sljit(const game_system *system)
{
    return system && system->program && system->program->sljit;
}

int game_system_uses_sljit_batch(const game_system *system)
{
    return system && system->program && system->program->sljit &&
           graph_sljit_uses_batch(system->program->sljit);
}

int game_system_uses_sljit_registers(const game_system *system)
{
    return system && system->program && system->program->sljit &&
           graph_sljit_uses_registers(system->program->sljit);
}

int game_system_uses_sljit_simd(const game_system *system)
{
    return system && system->program && system->program->sljit &&
           graph_sljit_uses_simd(system->program->sljit);
}

const graph_program *game_system_program(const game_system *system)
{
    return system && system->program ? &system->program->shared : NULL;
}

int game_system_prepare_snapshot(game_system *system, flow_error *error)
{
    aclear(system->snapshot);
    if (system->compile.error_count || !system->program ||
        program_bind_world(system->program, system->world) != 0) {
        set_error(error, FLOW_ERR_COMPILE, NULL, "System has no valid compiled program or registered columns");
        return FLOW_ERR_COMPILE;
    }
    ecs_query query;
    int result = system->compiled_query_expression
                   ? ecs_query_begin_expression(system->world, &query, system->compiled_query_expression)
                   : ecs_query_begin(
                         system->world,
                         &query,
                         system->query,
                         acount(system->query)
                     );
    if (result != ECS_OK) {
        set_error(error, FLOW_ERR_STATE, NULL, "System query could not start");
        return FLOW_ERR_STATE;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity) > 0) {
        apush(system->snapshot, entity);
    }
    ecs_query_end(&query);
    return FLOW_OK;
}

int game_system_run_snapshot(game_system *system, double time, double delta, flow_error *error)
{
    system_program *p = system->program;
    system->match_count = 0;
    if (p->sljit) {
        system_jit_user user = { p, error };
        graph_jit_host host = { .user = &user, .error = system_jit_error };
        graph_jit_entry_fn entry = graph_sljit_entry(p->sljit);
        graph_jit_context context = { .abi_version = GRAPH_JIT_ABI_VERSION,
                                      .delta_seconds = (float)delta,
                                      .monotonic_time = time,
                                      .columns = p->sljit_columns,
                                      .column_count = p->sljit_column_count,
                                      .host = &host };
        for (int i = 0; i < acount(system->snapshot); i++) {
            ecs_entity entity = system->snapshot[i];
            for (int j = 0; j < p->sljit_column_count; j++) {
                p->sljit_columns[j].data = (unsigned char *)p->sljit_bases[j] +
                                           (size_t)entity.slot *
                                               p->sljit_columns[j].size;
            }
            context.entity_slot = entity.slot;
            context.entity_generation = entity.generation;
            if (entry(&context) != GRAPH_JIT_OK) {
                return error && error->code ? error->code : FLOW_ERR_NODE;
            }
            system->match_count++;
        }
        return FLOW_OK;
    }
    for (int i = 0; i < acount(system->snapshot); i++) {
        int result = p->typed
                       ? program_run_typed(p, system->snapshot[i], delta, error)
                       : program_run_generic(p, system->snapshot[i], delta, error);
        if (result != FLOW_OK) {
            return result;
        }
        system->match_count++;
    }
    return FLOW_OK;
}
