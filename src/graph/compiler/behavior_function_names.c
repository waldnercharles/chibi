#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <string.h>

scalar_value *behavior_lower_scalar_value(builder *b, int slot)
{
    for (int i = 0; i < acount(b->scalars); i++) {
        if (b->scalars[i].slot == slot) {
            return &b->scalars[i];
        }
    }
    return NULL;
}
int behavior_lower_named_scalar(builder *b, int value, int name, bool owned)
{
    if (value < 0) {
        return value;
    }
    scalar_value *old = behavior_lower_scalar_value(b, value);
    if (old) {
        value = old->value;
    }
    int slot = acount(b->function->slots);
    apush(b->function->slots, b->function->slots[value]);
    apush(
        b->scalars,
        ((scalar_value){ .slot = slot,
                         .value = value,
                         .name = name,
                         .variable = -1,
                         .wide = -1,
                         .logical_type = b->function->slots[value],
                         .owned = owned })
    );
    return slot;
}
bool behavior_function_static_name(const char **name)
{
    if (!*name) {
        return true;
    }
    const type_desc *type = registry_type_by_name(*name);
    if (type) {
        *name = type->name;
        return true;
    }
    static const char *const names[] = {
        "bool",  "byte",           "char",      "unsigned char",
        "short", "unsigned short", "int",       "unsigned int",
        "int64", "int64_t",        "uint64",    "uint64_t",
        "float", "double",         "ecs.entity"
    };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        if (!strcmp(*name, names[i])) {
            *name = names[i];
            return true;
        }
    }
    return false;
}
int behavior_lower_static_name(builder *b, const char *name)
{
    behavior_function_static_name(&name);
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_TYPE_NAME,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_CONSTANT,
                                         .value = { .str = name } }
    );
}
int behavior_lower_named_constant(builder *b, int value, const char *name)
{
    bool owned = behavior_function_static_name(&name);
    return behavior_lower_named_scalar(b, value, behavior_lower_static_name(b, name), owned);
}
int behavior_lower_inherit_name(builder *b, int value, int source)
{
    scalar_value *s = behavior_lower_scalar_value(b, source);
    if (!s || value < 0) {
        return value;
    }
    scalar_value copy = *s;
    int result = behavior_lower_named_scalar(b, value, copy.name, copy.owned);
    scalar_value *out = behavior_lower_scalar_value(b, result);
    out->variable = copy.variable;
    out->logical_type = copy.logical_type;
    out->wide = copy.wide;
    return result;
}
int behavior_lower_source_name(builder *b, int value, behavior_function_op op, int index, const pin_spec *spec)
{
    if (op == BF_CONSTANT) {
        return behavior_lower_named_constant(b, value, spec->default_value.type_name);
    }
    int name = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = op == BF_LOCAL ? BF_LOCAL : BF_TYPE_NAME,
            .type = BF_POINTER_TYPE,
            .left = op == BF_LOCAL ? behavior_lower_local_name(b, index) : index,
            .target = op,
            .value = { .type = spec->type, .str = spec->default_value.type_name } }
    );
    int result = behavior_lower_named_scalar(b, value, name, op == BF_VARIABLE);
    if (op == BF_VARIABLE && result >= 0) {
        behavior_lower_scalar_value(b, result)->variable = index;
    }
    return result;
}
int behavior_lower_coerce(builder *b, int value, const pin_spec *spec, bool binding)
{
    scalar_value *s = behavior_lower_scalar_value(b, value);
    if (!s) {
        return behavior_lower_convert(b, value, spec->type);
    }
    scalar_value copy = *s;
    int converted = behavior_lower_convert(b, value, spec->type);
    if (converted < 0) {
        return converted;
    }
    if (!binding && copy.wide < 0 && copy.logical_type == spec->type) {
        return converted;
    }
    const char *name = spec->type_name;
    bool owned = behavior_function_static_name(&name);
    int target = behavior_lower_static_name(b, name);
    if (!binding && copy.wide >= 0) {
        target = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = BF_SELECT,
                .type = BF_POINTER_TYPE,
                .left = copy.wide,
                .right = spec->type == PIN_F64 ? copy.name : target,
                .target = spec->type == PIN_F64 ? target : copy.name }
        );
        owned &= copy.owned;
    }
    return behavior_lower_named_scalar(b, converted, target, owned);
}
void behavior_lower_store_name(builder *b, behavior_function_op op, int value, int target)
{
    scalar_value *source = behavior_lower_scalar_value(b, value);
    scalar_value meta = source ? *source
                               : (scalar_value){ .name = -1, .variable = -1 };
    if (op == BF_SAVE) {
        const behavior_function_local local = b->function->locals[target];
        if (!local.node || local.pin < 0) {
            return;
        }
        if (((local.node->desc == &NODE_ForLoop || local.node->desc == &NODE_ForLoopWithBreak) &&
             local.pin == 1) ||
            ((local.node->desc == &NODE_ForEach || local.node->desc == &NODE_ForEachWithBreak) &&
             local.pin == 2)) {
            meta.name = behavior_lower_static_name(b, "int");
        }
        int name = meta.name >= 0 ? meta.name : behavior_lower_static_name(b, NULL);
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_SAVE,
                                             .type = BF_POINTER_TYPE,
                                             .left = name,
                                             .target = behavior_lower_local_name(b, target) }
        );
        return;
    }
    if (op != BF_STORE && op != BF_RESULT) {
        return;
    }
    if (op == BF_STORE && meta.variable == target) {
        return;
    }
    int name = meta.name >= 0 ? meta.name : behavior_lower_static_name(b, NULL);
    if (op == BF_STORE && !meta.owned && meta.name >= 0) {
        name = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_OWN_NAME,
                                             .type = BF_POINTER_TYPE,
                                             .left = name }
        );
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = op == BF_STORE ? BF_STORE_TYPE_NAME : BF_RESULT_TYPE_NAME,
            .left = name,
            .target = target }
    );
}
