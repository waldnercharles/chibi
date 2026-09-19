#include "graph/compiler/behavior_lower_internal.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

static bool signature(builder *b, node *n, const pin_spec *pins, int count, bool output)
{
    const pin_spec *declared = output ? n->desc->outputs : n->desc->inputs;
    int first = n->desc->flow ? 1 : 0;
    bool owned = n->desc->host_call->value != NULL;
    for (int i = first; i < count; i++) {
        int declared_index = !output && n->desc->host_call->variadic &&
                                     i >= n->desc->input_count
                               ? n->desc->input_count - 1
                               : i;
        const pin_spec *p = &pins[i], *d = &declared[declared_index];
        bool nominal = p->type == PIN_STRUCT || p->type == PIN_ENUM ||
                       p->type == PIN_HANDLE;
        bool array = p->container == PIN_CONTAINER_ARRAY ||
                     p->container == PIN_CONTAINER_SET;
        if (p->kind != PIN_DATA || p->container != d->container ||
            (p->container != PIN_CONTAINER_SINGLE && !(array && (owned || !output))) ||
            (!(d->flags & PIN_FLAG_WILDCARD) &&
             (p->type != d->type ||
              (nominal && ((p->type_name == NULL) != (d->type_name == NULL) ||
                           (p->type_name && strcmp(p->type_name, d->type_name))))))) {
            behavior_lower_unsupported(b, n, i, output, "host data signature");
            return false;
        }
        size_t size = 0;
        if (p->type == PIN_STRUCT) {
            aggregate shape = behavior_lower_shape(p->type_name);
            size = shape.type && (owned || !output || type_is_plain(shape.type))
                     ? shape.type->size
                     : 0;
            afree(shape.fields);
        }
        if (!behavior_lower_scalar(p->type) && !size &&
            ((!owned && output) || !behavior_scalar_string(p->type))) {
            behavior_lower_unsupported(b, n, i, output, "host data value ownership");
            return false;
        }
        apush(
            b->function->data_values,
            ((behavior_function_data_value){
                .initial = { .type = p->type, .type_name = p->type_name, .container = p->container },
                .size = array || (output && owned) ? 0 : size })
        );
    }
    return true;
}
static int address(builder *b, int index, bool buffer)
{
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .left = index, .target = buffer }
    );
}
static int read(builder *b, int pointer, size_t offset, pin_type type)
{
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_LOAD, .type = type, .left = pointer, .offset = offset }
    );
}
static void write(builder *b, int value, int pointer, size_t offset)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = b->function->slots[value],
                                         .left = value,
                                         .right = pointer,
                                         .offset = offset }
    );
}
bool behavior_lower_pack_value(builder *b, int value, int index)
{
    int pointer = address(b, index, false), name;
    aggregate *a = behavior_lower_aggregate(b, value);
    array_value *array = behavior_lower_array(b, value);
    if (array) {
        name = array->name;
        value = array->pointer;
    } else if (a) {
        int buffer = address(b, index, true);
        for (int i = 0; i < acount(a->fields); i++) {
            write(b, a->fields[i].slot, buffer, a->fields[i].offset);
        }
        int zero = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = BF_POINTER_TYPE,
                                             .value = { .type = BF_POINTER_TYPE } }
        );
        value = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_SELECT,
                                             .type = BF_POINTER_TYPE,
                                             .left = a->present,
                                             .right = buffer,
                                             .target = zero }
        );
        name = a->name >= 0 ? a->name : behavior_lower_static_name(b, a->type->name);
    } else {
        scalar_value *s = behavior_lower_scalar_value(b, value);
        if (!s) {
            return false;
        }
        name = s->name;
    }
    write(b, value, pointer, offsetof(pin_value, d));
    write(b, name, pointer, offsetof(pin_value, type_name));
    return true;
}
static int result(builder *b, const pin_spec *spec, int index, bool owned)
{
    int header = address(b, index, false);
    if (spec->container == PIN_CONTAINER_ARRAY || spec->container == PIN_CONTAINER_SET) {
        int pointer = read(b, header, offsetof(pin_value, ptr), BF_POINTER_TYPE);
        int name = read(b, header, offsetof(pin_value, type_name), BF_POINTER_TYPE);
        int slot = behavior_lower_array_value(b, pointer, name, spec);
        behavior_lower_array(b, slot)->stable = true;
        return slot;
    }
    if (spec->type != PIN_STRUCT) {
        int value = read(b, header, offsetof(pin_value, d), spec->type);
        return owned
                 ? behavior_lower_named_scalar(
                       b,
                       value,
                       read(b, header, offsetof(pin_value, type_name), BF_POINTER_TYPE),
                       false
                   )
                 : behavior_lower_named_constant(b, value, spec->type_name);
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    assert(a.type);
    a.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    a.name = owned ? read(b, header, offsetof(pin_value, type_name), BF_POINTER_TYPE)
                   : behavior_lower_static_name(b, spec->type_name);
    int buffer = owned ? read(b, header, offsetof(pin_value, ptr), BF_POINTER_TYPE)
                       : address(b, index, true);
    for (int i = 0; i < acount(a.fields); i++) {
        a.fields[i].slot = read(b, buffer, a.fields[i].offset, a.fields[i].type);
    }
    return behavior_lower_aggregate_value(b, a);
}
int behavior_lower_data_result(builder *b, const pin_spec *spec, int index)
{
    if (spec->type != PIN_STRUCT || spec->container != PIN_CONTAINER_SINGLE) {
        return result(b, spec, index, true);
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    if (!a.type) {
        afree(a.fields);
        return -1;
    }
    int header = address(b, index, false);
    int pointer = read(b, header, offsetof(pin_value, ptr), BF_POINTER_TYPE);
    int zero = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .value = { .type = BF_POINTER_TYPE } }
    );
    a.present = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = PIN_BOOL, .left = pointer, .right = zero }
    );
    a.name = read(b, header, offsetof(pin_value, type_name), BF_POINTER_TYPE);
    for (int i = 0; i < acount(a.fields); i++) {
        a.fields[i].slot = behavior_lower_read(
            b,
            pointer,
            a.fields[i].offset,
            a.fields[i].type,
            true
        );
    }
    return behavior_lower_aggregate_value(b, a);
}

int behavior_lower_host_call(builder *b, node *n, int pin)
{
    const node_desc *desc = n->desc;
    assert(desc->host_call && !!desc->host_call->invoke != !!desc->host_call->value);
    bool owned = desc->host_call->value != NULL;
    bool flow = desc->flow != NULL;
    int begin = flow ? 1 : 0;
    if ((!desc->host_call->variadic && n->input_count != desc->input_count) ||
        (desc->host_call->variadic && (!owned || desc->input_count < 1 ||
                                       n->input_count < desc->input_count - 1)) ||
        (owned && (flow || desc->pure_kind != NODE_PURE_CALL ||
                   desc->output_count != 1 || desc->host_call->service)) ||
        n->output_count < desc->output_count || !desc->output_count ||
        (flow && (!desc->input_count || desc->inputs[0].kind != PIN_EXEC ||
                  desc->outputs[0].kind != PIN_EXEC))) {
        return behavior_lower_unsupported(b, n, pin, true, "host data signature");
    }
    for (int i = desc->output_count; i < n->output_count; i++) {
        if (!graph_output_field(n, i, NULL)) {
            return behavior_lower_unsupported(b, n, i, true, "host data output projection");
        }
    }
    int input_count = n->input_count;
    /* Variadic nodes retain an unused Generic pin for the next argument. It
     * is an authoring affordance, not a value passed to the native call. */
    while (desc->host_call->variadic && input_count >= desc->input_count &&
           n->input_pins[input_count - 1].type == PIN_FLOW &&
           (n->input_pins[input_count - 1].flags & PIN_FLAG_WILDCARD)) {
        input_count--;
    }
    int index = acount(b->function->host_calls),
        first = acount(b->function->data_values);
    /* Reserve the complete bank before lowering inputs, which may make their
     * own host calls. Every producer has distinct storage and scalar snapshots. */
    if (!signature(b, n, n->input_pins, input_count, false) ||
        !signature(b, n, n->output_pins, desc->output_count, true)) {
        return -1;
    }
    int result_first = first + input_count - begin;
    b->function->managed |= owned;
    apush(
        b->function->host_calls,
        ((behavior_function_host_call){ .call = desc->host_call,
                                        .access = desc->access,
                                        .first = first,
                                        .result_first = result_first,
                                        .result_count = desc->output_count - begin })
    );
    if (desc->host_call->missing_service) {
        int service = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_HOST_SERVICE,
                                             .type = BF_POINTER_TYPE,
                                             .target = index }
        );
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = BF_ASSERT,
                .left = service,
                .target = flow ? BEHAVIOR_FUNCTION_HOST_FLOW : BEHAVIOR_FUNCTION_HOST_DATA,
                .node_id = n->id,
                .pin = flow ? -1 : 0,
                .message = desc->host_call->missing_service }
        );
    }
    int *arguments = NULL;
    for (int i = begin; i < input_count; i++) {
        int value = behavior_lower_input(b, n, i);
        if (value < 0 || !behavior_lower_pack_value(b, value, first + i - begin)) {
            afree(arguments);
            return -1;
        }
        apush(arguments, value);
    }
    for (int i = begin; owned && i < input_count; i++) {
        if (n->input_pins[i].container == PIN_CONTAINER_SINGLE &&
            n->input_pins[i].type == PIN_STRUCT) {
            aggregate *a = behavior_lower_aggregate(b, arguments[i - begin]);
            behavior_lower_emit(
                b,
                (behavior_function_instruction){
                    .op = BF_REQUIRE,
                    .left = a->present,
                    .node_id = n->id,
                    .pin = i,
                    .message = "data input has no struct value" }
            );
        }
    }
    afree(arguments);
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = index,
                                         .node_id = n->id,
                                         .left = flow ? BEHAVIOR_FUNCTION_HOST_FLOW
                                                      : BEHAVIOR_FUNCTION_HOST_DATA,
                                         .pin = flow ? -1 : 0 }
    );
    int selected = flow ? 0 : -1;
    for (int i = begin; i < desc->output_count; i++) {
        int value = result(b, &n->output_pins[i], result_first + i - begin, owned);
        if (flow) {
            int local = behavior_lower_local(b, b->frame, n, i);
            if (local < 0 || !behavior_lower_store(b, BF_SAVE, value, local)) {
                return -1;
            }
            continue;
        }
        if (i == pin) {
            selected = value;
        } else {
            apush(b->outputs, ((output){ n, i, value }));
        }
    }
    return selected;
}
