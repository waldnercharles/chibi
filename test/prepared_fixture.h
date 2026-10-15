#ifndef CHIBI_TEST_PREPARED_FIXTURE_H
#define CHIBI_TEST_PREPARED_FIXTURE_H
#include "ecs/ecs.h"
#include "execution/instruction.h"
#include "types/type.h"
#include "types/value_wire.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int32_t value;
    bool enabled;
} prepared_counter;
static const type_field prepared_counter_fields[] = {
    { .name = "value", .type = PIN_I32, .type_name = "int", .offset = 0, .size = sizeof(int32_t) },
    { .name = "enabled",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(prepared_counter, enabled),
      .size = sizeof(bool) },
};
static const type_desc prepared_counter_type = {
    .name = "PersistCounter",
    .size = sizeof(prepared_counter),
    .fields = prepared_counter_fields,
    .field_count = 2,
};
static const ecs_component_desc prepared_counter_component = {
    .id = 901,
    .key = "fixture.persist_counter",
    .name = "PersistCounter",
    .element_size = sizeof(prepared_counter),
    .type = &prepared_counter_type,
};
enum
{
    PREPARED_TEST_MAX_BYTES = 512 * 1024,
    PREPARED_TEST_MAX_PATH = 4096
};
/* Locate the one checked getter's scalar wire fields without private runtime
 * headers, fixed-width integers or assumptions about module-table offsets. */
static bool prepared_scalar_getter_target(unsigned char *bytes, size_t length, size_t *target)
{
    int matches = 0;
    for (size_t offset = 0; offset < length; offset++) {
        execution_wire wire = { .data = bytes,
                                .size = length,
                                .offset = offset,
                                .reading = true };
        int fields[10] = { 0 };
        execution_wire_int(&wire, &fields[0]);
        if (wire.error || fields[0] != BF_COMPONENT_READ) {
            continue;
        }
        size_t field_offset = 0;
        for (int i = 1; i < 10; i++) {
            if (i == 5) {
                field_offset = wire.offset;
            }
            execution_wire_int(&wire, &fields[i]);
        }
        size_t stride = 0, name_size = 0;
        bool indirect = false;
        int component = 0, tag = 0, container = 0;
        uint64_t node = 0, identity = 0, layout = 0;
        execution_wire_size(&wire, &stride);
        execution_wire_bool(&wire, &indirect);
        execution_wire_int(&wire, &component);
        execution_wire_u64(&wire, &node);
        execution_wire_int(&wire, &tag);
        execution_wire_int(&wire, &container);
        execution_wire_size(&wire, &name_size);
        execution_wire_u64(&wire, &identity);
        execution_wire_u64(&wire, &layout);
        if (wire.error || fields[1] != PIN_BOOL ||
            fields[5] != (int)offsetof(prepared_counter, enabled) ||
            stride != sizeof(prepared_counter) || indirect || component < 0 ||
            tag != PIN_U64 || container != PIN_CONTAINER_SINGLE || name_size ||
            identity != prepared_counter_component.id ||
            layout != type_layout_hash(&prepared_counter_type)) {
            continue;
        }
        *target = field_offset;
        matches++;
    }
    return matches == 1 && sizeof(prepared_counter) < 64;
}

#endif
