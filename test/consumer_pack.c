#include "../core/registry.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    float x;
    int y;
} ConsumerPoint;

static const type_field consumer_point_fields[] = {
    { "x", PIN_F32, offsetof(ConsumerPoint, x), sizeof(float), "float" },
    { "y", PIN_I32, offsetof(ConsumerPoint, y), sizeof(int), "int" },
};

const type_desc T_ConsumerPoint = {
    .name = "ConsumerPoint",
    .size = sizeof(ConsumerPoint),
    .fields = consumer_point_fields,
    .field_count = 2,
};

static pin_spec consumer_make_inputs[] = {
    { "x", PIN_F32, "float" },
    { "y", PIN_I32, "int" },
};
static pin_spec consumer_make_outputs[] = {
    { "ConsumerPoint", PIN_STRUCT, "ConsumerPoint" },
};
node_desc CONSUMER_NODE_Make_Point = {
    .name = "Consumer_Make_Point",
    .input_count = 2,
    .output_count = 1,
    .inputs = consumer_make_inputs,
    .outputs = consumer_make_outputs,
    .pure_kind = NODE_PURE_MAKE,
    .type = &T_ConsumerPoint,
};

static pin_spec consumer_break_inputs[] = {
    { "ConsumerPoint", PIN_STRUCT, "ConsumerPoint" },
};
static pin_spec consumer_break_outputs[] = {
    { "x", PIN_F32, "float" },
    { "y", PIN_I32, "int" },
};
node_desc CONSUMER_NODE_Break_Point = {
    .name = "Consumer_Break_Point",
    .input_count = 1,
    .output_count = 2,
    .inputs = consumer_break_inputs,
    .outputs = consumer_break_outputs,
    .pure_kind = NODE_PURE_BREAK,
    .type = &T_ConsumerPoint,
};

static node_desc *const consumer_nodes[] = {
    &CONSUMER_NODE_Make_Point,
    &CONSUMER_NODE_Break_Point,
};
static const type_desc *const consumer_types[] = {
    &T_ConsumerPoint,
};
static const service_desc consumer_services[] = {
    { "ConsumerService", UINT64_C(0x2ae66b95c2b59798) },
};
static const service_desc *const consumer_service_refs[] = {
    &consumer_services[0],
};
static const operator_signature consumer_operators[] = {
    { "consumer_add", PIN_I32, PIN_I32, PIN_I32, 10 },
};

const registry_pack CONSUMER_REGISTRY_PACK = {
    .name = "consumer.test",
    .nodes = consumer_nodes,
    .node_count = sizeof consumer_nodes / sizeof consumer_nodes[0],
    .types = consumer_types,
    .type_count = sizeof consumer_types / sizeof consumer_types[0],
    .services = consumer_service_refs,
    .service_count = sizeof consumer_service_refs / sizeof consumer_service_refs[0],
    .operators = consumer_operators,
    .operator_count = sizeof consumer_operators / sizeof consumer_operators[0],
};
