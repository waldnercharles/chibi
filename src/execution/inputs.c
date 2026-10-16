#include "execution/definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/catalog.h"
#include "types/record.h"

#include <assert.h>
#include <string.h>

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
            memcpy(target, bits, behavior_scalar_size(local->initial.type));
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
