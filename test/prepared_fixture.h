#ifndef CHIBI_TEST_PREPARED_FIXTURE_H
#define CHIBI_TEST_PREPARED_FIXTURE_H
#include "ecs/ecs.h"
#include "execution/instruction.h"
#include "types/type.h"
#include "types/value_wire.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
        bool indirect = false, authored = false;
        int component = 0, tag = 0, container = 0;
        uint64_t node = 0, identity = 0, layout = 0;
        execution_wire_size(&wire, &stride);
        execution_wire_bool(&wire, &indirect);
        execution_wire_int(&wire, &component);
        execution_wire_u64(&wire, &node);
        execution_wire_int(&wire, &tag);
        execution_wire_int(&wire, &container);
        execution_wire_size(&wire, &name_size);
        execution_wire_bool(&wire, &authored);
        execution_wire_u64(&wire, &identity);
        execution_wire_u64(&wire, &layout);
        if (wire.error || fields[1] != PIN_BOOL ||
            fields[5] != (int)offsetof(prepared_counter, enabled) ||
            stride != sizeof(prepared_counter) || indirect || component < 0 ||
            tag != PIN_U64 || container != PIN_CONTAINER_SINGLE || name_size ||
            authored || identity != prepared_counter_component.id ||
            layout != type_layout_hash(&prepared_counter_type)) {
            continue;
        }
        *target = field_offset;
        matches++;
    }
    return matches == 1 && sizeof(prepared_counter) < 64;
}

/* The 1-based index of `text` in the module's string table, which the last
 * 8 bytes locate; 0 when the table lacks it. */
static size_t prepared_string_code(unsigned char *bytes, size_t length, const char *text)
{
    uint64_t at = 0;
    for (int i = 0; length >= 8 && i < 8; i++) {
        at |= (uint64_t)bytes[length - 8 + i] << (8 * i);
    }
    if (length < 8 || at > length - 8) {
        return 0;
    }
    execution_wire wire = { .data = bytes,
                            .size = length - 8,
                            .offset = (size_t)at,
                            .reading = true };
    int count = 0;
    execution_wire_int(&wire, &count);
    for (int i = 0; i < count && !wire.error; i++) {
        size_t size = 0;
        execution_wire_size(&wire, &size);
        if (wire.error || !size || size - 1 > wire.size - wire.offset) {
            return 0;
        }
        if (size - 1 == strlen(text) && !memcmp(bytes + wire.offset, text, size - 1)) {
            return (size_t)i + 1;
        }
        wire.offset += size - 1;
    }
    return 0;
}
/* Find a payload-free native header, not its module requirement or a
 * component instruction. The schema marker includes its field count. */
static bool prepared_native_header_layout(unsigned char *bytes, size_t length, size_t *target)
{
    size_t name = prepared_string_code(bytes, length, "PersistCounter");
    for (size_t offset = 0; name && offset < length; offset++) {
        execution_wire wire = { .data = bytes,
                                .size = length,
                                .offset = offset,
                                .reading = true };
        int tag = 0, container = 0, fields = -1;
        size_t code = 0;
        execution_wire_int(&wire, &tag);
        execution_wire_int(&wire, &container);
        execution_wire_size(&wire, &code);
        if (wire.error || tag != PIN_STRUCT ||
            container != PIN_CONTAINER_SINGLE || code != name) {
            continue;
        }
        bool authored = true, present = true;
        uint64_t layout = 0;
        execution_wire_bool(&wire, &authored);
        size_t layout_offset = wire.offset;
        execution_wire_u64(&wire, &layout);
        execution_wire_bool(&wire, &present);
        execution_wire_int(&wire, &fields);
        if (!wire.error && !authored && !present &&
            fields == prepared_counter_type.field_count &&
            layout == type_layout_hash(&prepared_counter_type)) {
            *target = layout_offset;
            return true;
        }
    }
    return false;
}

#endif
