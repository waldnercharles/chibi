#ifndef CHIBI_ENTITY_QUERY_H
#define CHIBI_ENTITY_QUERY_H
#include "ecs/ecs.h"
#include "execution/host.h"
#include "runtime/components.h"

typedef enum
{
    ENTITY_MATCH_HAS,
    ENTITY_MATCH_MISSING,
    ENTITY_MATCH_EQ,
    ENTITY_MATCH_NE,
    ENTITY_MATCH_LT,
    ENTITY_MATCH_LE,
    ENTITY_MATCH_GT,
    ENTITY_MATCH_GE
} entity_match_test;
typedef struct
{
    const ecs_component_desc *component;
    const type_field *field;
    entity_match_test test;
    double value;
} entity_condition;

const char *entity_condition_resolve(
    const EntityCondition *source,
    const ecs_component_desc *component,
    entity_condition *out
);
const char *entity_count_matches(ecs_world *, const pin_value *, pin_value *);
extern const node_host_call ENTITY_COUNT_MATCHING_CALL;
extern const node_access ENTITY_COUNT_MATCHING_ACCESS;
#endif
