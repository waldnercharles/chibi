#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

aggregate *behavior_lower_aggregate(builder *b, int slot)
{
    for (int i = 0; i < acount(b->aggregates); i++) {
        if (b->aggregates[i].slot == slot) {
            return &b->aggregates[i];
        }
    }
    return NULL;
}
static bool shape_fields(const type_desc *type, size_t offset, aggregate_field **fields, int depth)
{
    if (!type || !type->field_count || depth > 16) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset + field->size > type->size) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = registry_type_by_name(field->type_name);
            if (!nested || nested->size != field->size ||
                !shape_fields(nested, offset + field->offset, fields, depth + 1)) {
                return false;
            }
            continue;
        }
        if (!behavior_lower_scalar(field->type) ||
            field->size != behavior_scalar_size(field->type)) {
            return false;
        }
        apush(*fields, ((aggregate_field){ offset + field->offset, field->type, -1 }));
    }
    return true;
}
aggregate behavior_lower_shape(const char *name)
{
    aggregate a = { .type = name ? registry_type_by_name(name) : NULL, .name = -1 };
    if (!shape_fields(a.type, 0, &a.fields, 0)) {
        afree(a.fields);
        a.type = NULL;
    }
    return a;
}
int behavior_lower_aggregate_value(builder *b, aggregate a)
{
    a.slot = acount(b->function->slots);
    apush(b->function->slots, PIN_STRUCT);
    apush(b->aggregates, a);
    return a.slot;
}
int behavior_lower_identity(builder *b, int slot, const node_desc *desc, const node *n, int pin, bool edge)
{
    aggregate *source = behavior_lower_aggregate(b, slot);
    if (!source || desc->input_count != 1 || desc->output_count != 1 ||
        desc->inputs[0].type != PIN_STRUCT || desc->outputs[0].type != PIN_STRUCT ||
        desc->inputs[0].container != PIN_CONTAINER_SINGLE ||
        desc->outputs[0].container != PIN_CONTAINER_SINGLE) {
        return -1;
    }
    aggregate result = behavior_lower_shape(desc->outputs[0].type_name);
    if (!result.type || result.type->size != source->type->size ||
        acount(result.fields) != acount(source->fields)) {
        afree(result.fields);
        return -1;
    }
    for (int i = 0; i < acount(result.fields); i++) {
        if (result.fields[i].offset != source->fields[i].offset ||
            result.fields[i].type != source->fields[i].type) {
            afree(result.fields);
            return -1;
        }
        result.fields[i].slot = source->fields[i].slot;
    }
    result.present = source->present;
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_REQUIRE,
            .left = source->present,
            .node_id = n->id,
            .pin = pin,
            .message = edge ? "edge conversion failed" : "data input has no struct value",
        }
    );
    return behavior_lower_aggregate_value(b, result);
}
static pin_value initial_field(pin_value initial, aggregate_field f)
{
    pin_value v = { .type = f.type };
    if (initial.ptr) {
        memcpy(&v.d, (const char *)initial.ptr + f.offset, behavior_scalar_size(f.type));
    }
    return v;
}
static int scalar_local(builder *b, int frame, const node *n, int pin, size_t offset, pin_value initial)
{
    for (int i = 0; i < acount(b->function->locals); i++) {
        const behavior_function_local *l = &b->function->locals[i];
        if (l->frame == frame && l->node == n && l->pin == pin && l->offset == offset) {
            return i;
        }
    }
    apush(
        b->function->locals,
        ((
            behavior_function_local
        ){ .initial = initial, .frame = frame, .node = n, .pin = pin, .offset = offset })
    );
    return acount(b->function->locals) - 1;
}
int behavior_lower_hidden_local(builder *b, const node *n, int key, pin_type type)
{
    return scalar_local(b, b->frame, n, -1 - key, 0, (pin_value){ .type = type });
}
int behavior_lower_source(
    builder *b,
    behavior_function_op op,
    int index,
    const pin_spec *spec,
    const node *n,
    int pin
)
{
    if (spec->type != PIN_STRUCT) {
        return behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = op,
                                             .type = spec->type,
                                             .left = index,
                                             .value = spec->default_value,
                                             .node_id = n->id,
                                             .pin = pin }
        );
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    if (!a.type) {
        return behavior_lower_unsupported(b, n, pin, true, "aggregate contains unsupported fields");
    }
    aggregate *stored = op == BF_LOCAL ? behavior_lower_aggregate(b, index) : NULL;
    a.present = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = op == BF_CONSTANT ? BF_CONSTANT
                : op == BF_LOCAL    ? BF_LOCAL
                                    : BF_PRESENT,
            .type = PIN_BOOL,
            .left = stored ? stored->present : index,
            .target = op,
            .value = { .type = PIN_BOOL, .b = spec->default_value.ptr != NULL } }
    );
    if (op == BF_CONSTANT && (!spec->default_value.type_name ||
                              strcmp(spec->default_value.type_name, a.type->name))) {
        a.name = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = BF_TYPE_NAME,
                .type = BF_POINTER_TYPE,
                .target = BF_CONSTANT,
                .value = { .str = spec->default_value.type_name } }
        );
    }
    if (op != BF_CONSTANT) {
        a.name_owned = op == BF_VARIABLE;
        a.name = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = op == BF_LOCAL ? BF_LOCAL : BF_TYPE_NAME,
                .type = BF_POINTER_TYPE,
                .left = stored ? stored->name : index,
                .target = op,
                .value = { .str = spec->default_value.type_name } }
        );
    }
    for (int i = 0; i < acount(a.fields); i++) {
        aggregate_field *f = &a.fields[i];
        f->slot = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = op,
                .type = f->type,
                .left = stored ? stored->fields[i].slot : index,
                .offset = f->offset,
                .indirect = op != BF_LOCAL && op != BF_CONSTANT,
                .value = initial_field(spec->default_value, *f),
                .node_id = n->id,
                .pin = pin }
        );
    }
    return behavior_lower_aggregate_value(b, a);
}
void behavior_lower_require(builder *b, const aggregate *a, const node *n, int pin)
{
    for (int pc = acount(b->function->instructions) - 1; pc >= 0; pc--) {
        const behavior_function_instruction *i = &b->function->instructions[pc];
        if (i->op >= BF_CHECK) {
            break;
        }
        if ((i->op == BF_REQUIRE && i->left == a->present) ||
            (i->op == BF_CONSTANT && i->output == a->present && i->value.b)) {
            return;
        }
    }
    behavior_lower_emit(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_REQUIRE, .left = a->present, .node_id = n->id, .pin = pin }
    );
}
int behavior_lower_field(builder *b, int slot, const type_field *field, const node *n, int pin)
{
    aggregate *source = behavior_lower_aggregate(b, slot);
    if (!source) {
        return -1;
    }
    behavior_lower_require(b, source, n, pin);
    if (field->type != PIN_STRUCT) {
        for (int i = 0; i < acount(source->fields); i++) {
            if (source->fields[i].offset == field->offset &&
                source->fields[i].type == field->type) {
                return source->fields[i].slot;
            }
        }
        return -1;
    }
    aggregate a = behavior_lower_shape(field->type_name);
    if (!a.type) {
        return -1;
    }
    a.present = source->present;
    for (int i = 0; i < acount(a.fields); i++) {
        for (int j = 0; j < acount(source->fields); j++) {
            if (source->fields[j].offset == field->offset + a.fields[i].offset) {
                a.fields[i].slot = source->fields[j].slot;
            }
        }
        assert(a.fields[i].slot >= 0);
    }
    return behavior_lower_aggregate_value(b, a);
}
int behavior_lower_make(builder *b, node *n)
{
    aggregate a = behavior_lower_shape(n->output_pins[0].type_name);
    if (!a.type || a.type != n->desc->type) {
        afree(a.fields);
        return -1;
    }
    a.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < a.type->field_count; i++) {
        const type_field *field = &a.type->fields[i];
        int v = behavior_lower_input(b, n, i);
        if (v < 0) {
            afree(a.fields);
            return -1;
        }
        aggregate *member = behavior_lower_aggregate(b, v);
        for (int j = 0; j < acount(a.fields); j++) {
            aggregate_field *f = &a.fields[j];
            if (f->offset < field->offset || f->offset >= field->offset + field->size) {
                continue;
            }
            if (!member) {
                f->slot = behavior_lower_convert(b, v, f->type);
            } else {
                for (int k = 0; k < acount(member->fields); k++) {
                    if (f->offset == field->offset + member->fields[k].offset) {
                        f->slot = member->fields[k].slot;
                    }
                }
            }
            if (f->slot < 0) {
                afree(a.fields);
                return -1;
            }
        }
    }
    return behavior_lower_aggregate_value(b, a);
}
static bool variable_header(builder *b, const aggregate *a, int target)
{
    const behavior_variable *v = &b->asset->variables[target];
    aggregate shape = behavior_lower_shape(v->type_name);
    bool same = shape.type && shape.type->size == a->type->size &&
                acount(shape.fields) == acount(a->fields);
    for (int i = 0; same && i < acount(shape.fields); i++) {
        same = shape.fields[i].offset == a->fields[i].offset &&
               shape.fields[i].type == a->fields[i].type;
    }
    afree(shape.fields);
    if (!same) {
        return false;
    }
    int name = a->name;
    bool owned = a->name_owned;
    if (name < 0) {
        owned = !strcmp(a->type->name, v->type_name);
        name = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = owned ? BF_STORAGE_NAME : BF_TYPE_NAME,
                .type = BF_POINTER_TYPE,
                .left = target,
                .target = BF_CONSTANT,
                .value = { .str = a->type->name },
            }
        );
    }
    if (!owned) {
        name = behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = BF_OWN_NAME,
                .type = BF_POINTER_TYPE,
                .left = name,
            }
        );
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_STORE_HEADER,
            .left = name,
            .right = a->present,
            .target = target,
        }
    );
    return true;
}
bool behavior_lower_store(builder *b, behavior_function_op op, int slot, int target)
{
    if (slot < 0 || target < 0) {
        return false;
    }
    aggregate *a = behavior_lower_aggregate(b, slot);
    if (!a) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = op,
                                             .type = b->function->slots[slot],
                                             .left = slot,
                                             .target = target }
        );
        return true;
    }
    if (op == BF_STORE) {
        if (!variable_header(b, a, target)) {
            return false;
        }
    }
    aggregate *stored = op == BF_SAVE ? behavior_lower_aggregate(b, target) : NULL;
    if (stored) {
        int name = a->name;
        if (name < 0) {
            name = behavior_lower_value(
                b,
                (behavior_function_instruction){ .op = BF_TYPE_NAME,
                                                 .type = BF_POINTER_TYPE,
                                                 .target = BF_CONSTANT,
                                                 .value = { .str = a->type->name } }
            );
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_SAVE,
                                             .type = BF_POINTER_TYPE,
                                             .left = name,
                                             .target = stored->name }
        );
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_SAVE,
                                             .type = PIN_BOOL,
                                             .left = a->present,
                                             .target = stored->present }
        );
    } else if (op == BF_RESULT) {
        bool found = false;
        for (int i = 0; i < acount(b->function->results); i++) {
            found |= b->function->results[i].index == target;
        }
        if (!found) {
            apush(b->function->results, ((behavior_function_result){ target, a->type }));
        }
        if (a->name >= 0) {
            behavior_lower_emit(
                b,
                (behavior_function_instruction){ .op = BF_RESULT_TYPE_NAME,
                                                 .left = a->name,
                                                 .target = target }
            );
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_RESULT_PRESENT,
                                             .left = a->present,
                                             .target = target }
        );
    }
    for (int i = 0; i < acount(a->fields); i++) {
        aggregate_field f = a->fields[i];
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = op,
                .type = f.type,
                .left = f.slot,
                .target = stored ? stored->fields[i].slot : target,
                .offset = f.offset,
                .indirect = !stored }
        );
    }
    return true;
}
void behavior_lower_finish_values(builder *b)
{
    behavior_function *f = b->function;
    int *map = xmalloc((size_t)(acount(f->slots) + 1) * sizeof *map);
    int count = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        map[i] = f->slots[i] == PIN_STRUCT ? -1 : count++;
        if (map[i] >= 0) {
            f->slots[map[i]] = f->slots[i];
        }
    }
    asetlen(f->slots, count);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            i->output = map[i->output];
        }
        if (behavior_function_left_slot(i->op)) {
            i->left = map[i->left];
        }
        if (behavior_function_right_slot(i->op)) {
            i->right = map[i->right];
        }
        if (((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) ||
            i->op == BF_SELECT) {
            i->target = map[i->target];
        }
    }
    free(map);
    for (int i = 0; i < acount(b->aggregates); i++) {
        afree(b->aggregates[i].fields);
    }
    afree(b->aggregates);
}
int behavior_lower_local(builder *b, int frame, const node *n, int pin)
{
    const pin_spec *spec = &n->output_pins[pin];
    if (spec->container != PIN_CONTAINER_SINGLE || spec->kind != PIN_DATA ||
        spec->field_source || spec->default_value.type != spec->type) {
        return behavior_lower_unsupported(b, n, pin, true, "stored output type, default or projection");
    }
    if (behavior_lower_scalar(spec->type)) {
        return scalar_local(b, frame, n, pin, 0, spec->default_value);
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    if (!a.type) {
        return behavior_lower_unsupported(b, n, pin, true, "stored aggregate fields");
    }
    a.present = scalar_local(
        b,
        frame,
        n,
        pin,
        SIZE_MAX,
        (pin_value){ .type = PIN_BOOL, .b = spec->default_value.ptr != NULL }
    );
    pin_value name = { .type = BF_POINTER_TYPE, .str = spec->default_value.type_name };
    a.name = scalar_local(b, frame, n, pin, SIZE_MAX - 1, name);
    for (int i = 0; i < acount(a.fields); i++) {
        aggregate_field *f = &a.fields[i];
        f->slot = scalar_local(b, frame, n, pin, f->offset, initial_field(spec->default_value, *f));
    }
    return behavior_lower_aggregate_value(b, a);
}
