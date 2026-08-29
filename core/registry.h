#ifndef CBLUEPRINT_REGISTRY_H
#define CBLUEPRINT_REGISTRY_H

#include "node.h"
#include "operator.h"
#include "service.h"

typedef struct registry_pack
{
    const char *name;
    node_desc *const *nodes;
    int node_count;
    const type_desc *const *types;
    int type_count;
    const service_desc *const *services;
    int service_count;
    const operator_signature *operators;
    int operator_count;
} registry_pack;

/* Each final target supplies one ordered, process-global pack list. */
extern const registry_pack *const REGISTRY_PACKS[];
extern const int REGISTRY_PACK_COUNT;

/* A pack is valid when every descriptor is valid and all identities are
 * unique in the supplied, explicit pack order. */
int registry_validate_packs(const registry_pack *const packs[], int pack_count);
void registry_validate(void);

/* Core's hand-authored descriptor packs. */
extern node_desc *const EXTRA_NODES[];
extern const int EXTRA_NODE_COUNT;
extern node_desc *const FLOW_NODES[];
extern const int FLOW_NODE_COUNT;
extern node_desc *const ARRAY_NODES[];
extern const int ARRAY_NODE_COUNT;
extern node_desc *const STATE_NODES[];
extern const int STATE_NODE_COUNT;
extern node_desc *const SWITCH_NODES[];
extern const int SWITCH_NODE_COUNT;
extern node_desc *const REROUTE_NODES[];
extern const int REROUTE_NODE_COUNT;
extern node_desc *const SET_MAP_NODES[];
extern const int SET_MAP_NODE_COUNT;
extern node_desc *const STRUCT_NODES[];
extern const int STRUCT_NODE_COUNT;

extern const registry_pack CORE_EXTRA_PACK;
extern const registry_pack CORE_FLOW_PACK;
extern const registry_pack CORE_ARRAY_PACK;
extern const registry_pack CORE_STATE_PACK;
extern const registry_pack CORE_SWITCH_PACK;
extern const registry_pack CORE_REROUTE_PACK;
extern const registry_pack CORE_SET_MAP_PACK;
extern const registry_pack CORE_STRUCT_PACK;

extern node_desc NODE_Event, NODE_Branch, NODE_Sequence, NODE_ForLoop,
    NODE_ForLoopWithBreak, NODE_WhileLoop, NODE_ForEach, NODE_ForEachWithBreak,
    NODE_Break, NODE_Delay, NODE_PrintString, NODE_GetVariable,
    NODE_SetVariable, NODE_Self, NODE_FunctionEntry, NODE_Return,
    NODE_CallFunction, NODE_CallPureFunction, NODE_CallMacro, NODE_SwitchInt,
    NODE_SwitchString, NODE_SwitchName, NODE_SwitchEnum, NODE_Select,
    NODE_RerouteData, NODE_RerouteExec, NODE_SetMembers, NODE_Add,
    NODE_Multiply, NODE_Subtract, NODE_Divide, NODE_Min, NODE_Max;

int registry_node_count(void);
const node_desc *registry_node(int index);
const node_desc *registry_node_by_name(const char *name);

int registry_type_count(void);
const type_desc *registry_type(int index);
const type_desc *registry_type_by_name(const char *name);

int registry_service_count(void);
const service_desc *registry_service(int index);
const service_desc *registry_service_by_name(const char *name);

int registry_operator_count(void);
const operator_signature *registry_operator(int index);

#endif
