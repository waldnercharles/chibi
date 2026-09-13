#ifndef CHIBI_ENTITY_QUERY_H
#define CHIBI_ENTITY_QUERY_H
#include "ecs/ecs.h"
#include "graph/model/node.h"
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
int entity_query_lower(struct behavior_lower_builder *b, node *n, int pin);
extern node_desc NODE_Count_Matching_Entities;
#endif
