#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "runtime/behaviors/entity_query.h"
#include "support/array.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static int value(builder *b, pin_value v)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = v.type, .value = v }
    );
}
static int binary(builder *b, behavior_function_op op, pin_type type, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op, .type = type, .left = left, .right = right }
    );
}
static void branch(builder *b, int condition, int label)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = label }
    );
}
static void jump(builder *b, int label)
{
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = label });
}
static int local(builder *b, int index)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_LOCAL, .type = PIN_I32, .left = index }
    );
}
static int zero_test(builder *b, int slot, bool equal)
{
    int zero = value(b, (pin_value){ .type = b->function->slots[slot] });
    return binary(b, equal ? BF_EQ : BF_NE, PIN_BOOL, slot, zero);
}
static const ecs_component_desc *component_named(const char *key)
{
    for (int i = 0; key && i < registry_node_count(); i++) {
        const ecs_component_desc *component = registry_node(i)->component;
        if (component &&
            !strcmp(component->key ? component->key : component->name, key)) {
            return component;
        }
    }
    return NULL;
}
static const pin_value *constant_conditions(builder *b, node *n)
{
    node *source;
    int pin;
    if (graph_input_source(b->graph, n, 1, &source, &pin)) {
        return source->desc->pure_kind == NODE_PURE_CONST &&
                       node_desc_is_pure(source->desc)
                 ? &source->output_pins[pin].default_value
                 : NULL;
    }
    return n->input_pins[1].binding ? NULL : &n->input_pins[1].default_value;
}
int entity_query_lower(builder *b, node *n, int pin)
{
    if (pin != 0 || n->input_count != 2 || n->output_count != 1 ||
        n->input_pins[0].type != PIN_HANDLE ||
        n->input_pins[0].container != PIN_CONTAINER_ARRAY ||
        n->input_pins[1].type != PIN_STRUCT ||
        n->input_pins[1].container != PIN_CONTAINER_ARRAY ||
        n->output_pins[0].type != PIN_I32 ||
        n->output_pins[0].container != PIN_CONTAINER_SINGLE) {
        return behavior_lower_unsupported(b, n, pin, true, "Count Matching Entities signature");
    }
    const pin_value *criteria = constant_conditions(b, n);
    if (!criteria) {
        compile_diagnostic d = { .severity = CMPL_WARNING,
                                 .rule = CMPL_RULE_QUERY_SCAN,
                                 .graph_id = b->graph->graph_id,
                                 .node_id = n->id,
                                 .pin_id = n->input_pins[1].id };
        snprintf(d.message, sizeof d.message, "Dynamic conditions require a separate scan on every evaluation. Use constant conditions to allow query fusion.");
        apush(b->function->query_diagnostics, d);
        return behavior_lower_host_call(b, n, pin);
    }
    const pin_array *conditions = criteria->ptr;
    entity_condition *plan = NULL;
    const char *error = NULL;
    for (int i = 0; conditions && i < conditions->count; i++) {
        const pin_value *item = &conditions->items[i];
        if (item->type != PIN_STRUCT ||
            item->container != PIN_CONTAINER_SINGLE || !item->ptr ||
            !item->type_name || strcmp(item->type_name, "EntityCondition")) {
            error = "query conditions must contain non-null EntityCondition "
                    "values";
            break;
        }
        const EntityCondition *source = item->ptr;
        entity_condition condition;
        error = entity_condition_resolve(source, component_named(source->component), &condition);
        if (error) {
            break;
        }
        apush(plan, condition);
    }
    if (error) {
        afree(plan);
        return behavior_lower_unsupported(b, n, 1, false, error);
    }
    int source = behavior_lower_input(b, n, 0);
    if (b->recording && behavior_lower_input(b, n, 1) < 0) {
        afree(plan);
        return -1;
    }
    array_value *array = behavior_lower_array(b, source);
    if (!array) {
        afree(plan);
        return -1;
    }
    int pointer = array->pointer;
    behavior_reduction reduction = { .begin = acount(b->function->instructions),
                                     .pointer = pointer,
                                     .graph_id = b->graph->graph_id,
                                     .node_id = n->id,
                                     .pin_id = n->input_pins[0].id };
    b->function->component_access = true;
    int world = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_ASSERT,
            .left = world,
            .node_id = n->id,
            .pin = 0,
            .target = BEHAVIOR_FUNCTION_HOST_DATA,
            .message = "Count Matching Entities requires ecs.world" }
    );
    /* Validate every condition even for an empty source, matching the host
     * helper. Component rows are only read inside this non-yielding operation. */
    int *columns = NULL;
    for (int i = 0; i < acount(plan); i++) {
        int column = behavior_lower_component_column(b, n, 1, world, plan[i].component);
        apush(columns, column);
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = BF_ASSERT,
                .left = column,
                .node_id = n->id,
                .pin = 1,
                .target = BEHAVIOR_FUNCTION_HOST_DATA,
                .message = "query component is not registered" }
        );
    }
    int length = behavior_lower_read(b, pointer, offsetof(pin_array, count), PIN_I32, true);
    int items = behavior_lower_read(b, pointer, offsetof(pin_array, items), BF_POINTER_TYPE, true);
    /* Inputs finish before a reduction starts; the scan cannot call or yield.
     * Reuse frame scratch across reductions, keeping prior results in SSA
     * slots. This lets the allocator retain both counters in registers. */
    int index_local = behavior_lower_hidden_local(b, b->entry, 0, PIN_I32);
    int count_local = behavior_lower_hidden_local(b, b->entry, 1, PIN_I32);
    int zero = value(b, (pin_value){ .type = PIN_I32 });
    int one = value(b, (pin_value){ .type = PIN_I32, .i = 1 });
    behavior_lower_store(b, BF_SAVE, zero, index_local);
    behavior_lower_store(b, BF_SAVE, zero, count_local);
    int loop = behavior_lower_label(b), next = behavior_lower_label(b),
        end = behavior_lower_label(b);
    behavior_lower_bind_label(b, loop);
    reduction.loop = acount(b->function->instructions);
    int index = local(b, index_local);
    branch(b, binary(b, BF_GE, PIN_BOOL, index, length), end);
    int item = behavior_lower_array_item(b, items, index);
    int handle = behavior_lower_read(b, item, offsetof(pin_value, u64), PIN_HANDLE, false);
    int entity = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_ENTITY, .type = PIN_U32, .left = world, .right = handle }
    );
    branch(b, zero_test(b, entity, true), next);
    reduction.body = acount(b->function->instructions);
    reduction.index = index;
    reduction.entity = entity;
    reduction.index_local = index_local;
    reduction.count_local = count_local;
    const behavior_function_op comparisons[] = { BF_EQ, BF_NE, BF_LT,
                                                 BF_LE, BF_GT, BF_GE };
    for (int i = 0; i < acount(plan); i++) {
        entity_condition condition = plan[i];
        int row = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_ROW,
                                             .type = BF_POINTER_TYPE,
                                             .left = columns[i],
                                             .right = entity,
                                             .offset = condition.component->element_size }
        );
        branch(b, zero_test(b, row, condition.test != ENTITY_MATCH_MISSING), next);
        if (!condition.field) {
            continue;
        }
        int field = behavior_lower_read(
            b,
            row,
            condition.field->offset,
            condition.field->type,
            false
        );
        int threshold;
        if (condition.field->type == PIN_I32 && condition.value >= INT32_MIN &&
            condition.value <= INT32_MAX && trunc(condition.value) == condition.value) {
            threshold = value(
                b,
                (pin_value){ .type = PIN_I32, .i = (int)condition.value }
            );
        } else {
            field = condition.field->type == PIN_F32 || condition.field->type == PIN_F64
                      ? behavior_lower_convert(b, field, PIN_F64)
                      : behavior_lower_value(
                            b,
                            (behavior_function_instruction){ .op = BF_CAST,
                                                             .type = PIN_F64,
                                                             .left = field,
                                                             .right = field }
                        );
            threshold = value(b, (pin_value){ .type = PIN_F64, .d = condition.value });
        }
        int matches = binary(b, comparisons[condition.test - ENTITY_MATCH_EQ], PIN_BOOL, field, threshold);
        branch(b, zero_test(b, matches, true), next);
    }
    int count = binary(b, BF_ADD, PIN_I32, local(b, count_local), one);
    behavior_lower_store(b, BF_SAVE, count, count_local);
    behavior_lower_bind_label(b, next);
    reduction.next = acount(b->function->instructions);
    behavior_lower_store(b, BF_SAVE, binary(b, BF_ADD, PIN_I32, index, one), index_local);
    jump(b, loop);
    behavior_lower_bind_label(b, end);
    reduction.end = acount(b->function->instructions);
    apush(b->function->reductions, reduction);
    afree(plan);
    afree(columns);
    return behavior_lower_named_constant(b, local(b, count_local), "int");
}
