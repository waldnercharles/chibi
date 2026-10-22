#include "execution/definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <string.h>

static const type_field *text_field(const type_desc *type, size_t offset, int depth)
{
    if (!type || depth >= TYPE_MAX_NESTING) {
        return NULL;
    }
    for (int n = 0; n < type->field_count; n++) {
        const type_field *field = &type->fields[n];
        if (field->offset == offset && field->type == PIN_STR) {
            return field;
        }
        if (field->type == PIN_STRUCT && offset >= field->offset &&
            offset - field->offset < field->size) {
            return text_field(
                type_catalog_find(field->type_name),
                offset - field->offset,
                depth + 1
            );
        }
    }
    return NULL;
}
const pin_value *behavior_function_frame_inputs(
    const behavior_function *f,
    const behavior_function_activation *activation,
    int frame,
    arena *storage,
    int *count
)
{
    *count = 0;
    int max_pin = 0;
    for (int i = 0; i < acount(f->parameters); i++) {
        if (f->parameters[i].frame == frame) {
            (*count)++;
            if (f->parameters[i].pin > max_pin) {
                max_pin = f->parameters[i].pin;
            }
        }
    }
    int *pins = arena_alloc(storage, (size_t)(max_pin + 1) * sizeof *pins);
    for (int i = 0; i <= max_pin; i++) {
        pins[i] = -1;
    }
    pin_value *values = arena_alloc(storage, (size_t)(*count + 1) * sizeof *values);
    bool *present = arena_alloc(storage, (size_t)(*count + 1) * sizeof *present);
    int index = 0;
    for (int i = 0; i < acount(f->parameters); i++) {
        const behavior_function_parameter *parameter = &f->parameters[i];
        if (parameter->frame != frame) {
            continue;
        }
        pins[parameter->pin] = index;
        pin_value *v = &values[index];
        *v = parameter->initial;
        present[index++] = true;
        if (v->type == PIN_STRUCT && v->container == PIN_CONTAINER_SINGLE) {
            const type_desc *type = pin_value_type(v);
            assert(type);
            v->ptr = arena_alloc(storage, type->size);
            memset(v->ptr, 0, type->size);
        }
    }
    for (int i = 0; i < acount(f->locals); i++) {
        const behavior_function_local *local = &f->locals[i];
        if (!local->parameter || local->frame != frame) {
            continue;
        }
        int index = pins[local->pin];
        assert(index >= 0);
        pin_value *v = &values[index];
        const uint64_t *bits = &activation->values[acount(f->slots) + i];
        if (local->offset == SIZE_MAX) {
            memcpy(&present[index], bits, sizeof *present);
        } else if (local->offset == SIZE_MAX - 1) {
            memcpy(&v->type_name, bits, sizeof v->type_name);
        } else {
            void *target = v->type == PIN_STRUCT && v->container == PIN_CONTAINER_SINGLE
                             ? (char *)v->ptr + local->offset
                             : (void *)&v->d;
            const type_field *field = local->initial.type == PIN_STR && v->type == PIN_STRUCT
                                        ? text_field(pin_value_type(v), local->offset, 0)
                                        : NULL;
            if (field && !field->text_descriptor) {
                pin_value value = local->initial;
                memcpy(&value.text, bits, sizeof value.text);
                type_field destination = *field;
                destination.offset = 0;
                if (pin_write_field(storage, target, &destination, value)) {
                    *count = 0;
                    return NULL;
                }
            } else {
                memcpy(target, bits, behavior_scalar_size(local->initial.type));
            }
        }
    }
    for (int i = 0; i < *count; i++) {
        if (!present[i]) {
            values[i].ptr = NULL;
        }
        if (pin_value_snapshot_to_arena(storage, &values[i], &values[i])) {
            *count = 0;
            return NULL;
        }
    }
    return values;
}
