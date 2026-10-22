#include "runtime/behaviors/entity_query.h"

#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <string.h>

const char *entity_condition_resolve(
    const EntityCondition *source,
    const ecs_component_desc *component,
    entity_condition *out
)
{
    if (!component || !source->component || !source->component[0]) {
        return "query component is not registered";
    }
    const char *tests[] = { "has", "missing", "==", "!=", "<", "<=", ">", ">=" };
    int test = 0;
    for (; test < 8; test++) {
        if (source->test && !strcmp(source->test, tests[test])) {
            break;
        }
    }
    if (test == 8) {
        return "query test must be has, missing, ==, !=, <, <=, > or >=";
    }
    *out = (entity_condition){ .component = component,
                               .test = test,
                               .value = source->value };
    if (test <= ENTITY_MATCH_MISSING) {
        if (source->field && source->field[0]) {
            return "query presence tests require an empty field";
        }
        return NULL;
    }
    for (int i = 0; source->field && component->type && i < component->type->field_count;
         i++) {
        const type_field *field = &component->type->fields[i];
        if (strcmp(field->name, source->field)) {
            continue;
        }
        switch (field->type) {
            case PIN_BOOL:
            case PIN_I8:
            case PIN_U8:
            case PIN_I16:
            case PIN_U16:
            case PIN_I32:
            case PIN_U32:
            case PIN_F32:
            case PIN_F64: out->field = field; return NULL;
            default:
                return "query comparisons require a Boolean, 8/16/32-bit "
                       "integer, Float or Real field";
        }
    }
    return "query scalar component field is unknown";
}

static bool matches(const entity_condition *condition, ecs_world *world, ecs_entity entity)
{
    const void
        *row = ecs_component_data_const(world, entity, condition->component->id);
    if (condition->test == ENTITY_MATCH_HAS) {
        return row != NULL;
    }
    if (condition->test == ENTITY_MATCH_MISSING) {
        return row == NULL;
    }
    if (!row) {
        return false;
    }
    pin_value value;
    if (pin_read_field(NULL, row, condition->field, &value)) {
        return false;
    }
    double number;
    switch (value.type) {
        case PIN_BOOL: number = value.b; break;
        case PIN_I8: number = value.i8; break;
        case PIN_I16: number = value.i16; break;
        case PIN_I32: number = value.i; break;
        case PIN_U8: number = value.u8; break;
        case PIN_U16: number = value.u16; break;
        case PIN_U32: number = value.u; break;
        case PIN_F32: number = value.f; break;
        case PIN_F64: number = value.d; break;
        default: assert(false); return false;
    }
    switch (condition->test) {
        case ENTITY_MATCH_EQ: return number == condition->value;
        case ENTITY_MATCH_NE: return number != condition->value;
        case ENTITY_MATCH_LT: return number < condition->value;
        case ENTITY_MATCH_LE: return number <= condition->value;
        case ENTITY_MATCH_GT: return number > condition->value;
        case ENTITY_MATCH_GE: return number >= condition->value;
        default: assert(false); return false;
    }
}
static const ecs_component_desc *component_named(ecs_world *world, const char *key)
{
    for (int i = 0; key && i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *component = ecs_world_component_at(world, i);
        if (!strcmp(component->key ? component->key : component->name, key)) {
            return component;
        }
    }
    return NULL;
}
const char *entity_count_matches(ecs_world *world, const pin_value *inputs, pin_value *out)
{
    if (!world) {
        return "Count Matching Entities requires ecs.world";
    }
    const pin_array *entities = inputs[0].ptr, *conditions = inputs[1].ptr;
    entity_condition *plan = NULL;
    const char *error = NULL;
    for (int i = 0; conditions && i < conditions->count; i++) {
        pin_value condition_value = pin_array_get(conditions, i);
        const pin_value *item = &condition_value;
        if (item->type != PIN_STRUCT ||
            item->container != PIN_CONTAINER_SINGLE || !item->ptr ||
            !item->type_name || strcmp(item->type_name, "EntityCondition")) {
            error = "query conditions must contain non-null EntityCondition "
                    "values";
            goto done;
        }
        const EntityCondition *source = item->ptr;
        entity_condition condition;
        error = entity_condition_resolve(source, component_named(world, source->component), &condition);
        if (error) {
            goto done;
        }
        apush(plan, condition);
    }
    out->i = 0;
    for (int i = 0; entities && i < entities->count; i++) {
        ecs_entity entity = ecs_entity_from_u64(pin_array_get(entities, i).u64);
        if (!ecs_entity_is_alive(world, entity)) {
            continue;
        }
        bool match = true;
        for (int j = 0; match && j < acount(plan); j++) {
            match = matches(&plan[j], world, entity);
        }
        out->i += match;
    }
done:
    afree(plan);
    return error;
}
static const char *count_matches(void *world, const pin_value *inputs, pin_value *out)
{
    return entity_count_matches(world, inputs, out);
}
const node_access ENTITY_COUNT_MATCHING_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_host_call ENTITY_COUNT_MATCHING_CALL = { .service = "ecs.world",
                                                    .invoke = count_matches };
