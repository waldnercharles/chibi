#ifndef CHIBI_CORE_NODES_H
#define CHIBI_CORE_NODES_H

#include "graph/model/registry.h"

/* Core's hand-authored descriptor packs. */
extern node_desc *const STATE_NODES[];
extern const int STATE_NODE_COUNT;

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
    NODE_Break, NODE_Delay, NODE_PrintString, NODE_GetVariable, NODE_SetVariable,
    NODE_Self, NODE_FunctionEntry, NODE_Return, NODE_CallFunction,
    NODE_CallPureFunction, NODE_BehaviorExpression, NODE_CallMacro,
    NODE_SwitchInt, NODE_SwitchString, NODE_SwitchName, NODE_SwitchEnum,
    NODE_Select, NODE_RerouteData, NODE_RerouteExec, NODE_SetMembers, NODE_Add,
    NODE_Multiply, NODE_Subtract, NODE_Divide, NODE_Min, NODE_Max;
extern node_desc NODE_Clamp, NODE_Modulo, NODE_Negate, NODE_Abs;
extern node_desc NODE_And, NODE_Or, NODE_Not;

#endif
