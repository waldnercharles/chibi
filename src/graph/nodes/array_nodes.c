#include "graph/nodes/array_nodes.h"

#include "graph/model/native_definition.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/arrays.h"

#define COUNT(A) (sizeof(A) / sizeof *(A))
static pin_spec make_inputs[] = { NATIVE_ARRAY_MAKE_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec make_outputs[] = { NATIVE_ARRAY_MAKE_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_element_inputs[] = {
    NATIVE_ARRAY_ARRAY_ELEMENT_INPUTS(GRAPH_NATIVE_PORT)
};
static pin_spec array_element_outputs[] = {
    NATIVE_ARRAY_ARRAY_ELEMENT_OUTPUTS(GRAPH_NATIVE_PORT)
};
static pin_spec array_only_inputs[] = { NATIVE_ARRAY_ARRAY_ONLY_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_only_outputs[] = { NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_bool_inputs[] = { NATIVE_ARRAY_ARRAY_BOOL_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_bool_index_inputs[] = {
    NATIVE_ARRAY_ARRAY_BOOL_INDEX_INPUTS(GRAPH_NATIVE_PORT)
};
static pin_spec array_array_item_inputs[] = {
    NATIVE_ARRAY_ARRAY_ARRAY_ITEM_INPUTS(GRAPH_NATIVE_PORT)
};
static pin_spec array_two_inputs[] = { NATIVE_ARRAY_ARRAY_TWO_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_set_inputs[] = { NATIVE_ARRAY_ARRAY_SET_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_insert_inputs[] = { NATIVE_ARRAY_ARRAY_INSERT_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_swap_inputs[] = { NATIVE_ARRAY_ARRAY_SWAP_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_resize_inputs[] = { NATIVE_ARRAY_ARRAY_RESIZE_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec array_slice_inputs[] = { NATIVE_ARRAY_ARRAY_SLICE_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec bool_output[] = { NATIVE_ARRAY_BOOL_OUTPUT(GRAPH_NATIVE_PORT) };
static pin_spec int_output[] = { NATIVE_ARRAY_INT_OUTPUT(GRAPH_NATIVE_PORT) };

node_desc NODE_MakeArray = {
    .name = NATIVE_ARRAY_MAKE_NAME,
    .source_name = NATIVE_ARRAY_MAKE_SOURCE,
    .input_count = COUNT(make_inputs),
    .output_count = COUNT(make_outputs),
    .inputs = make_inputs,
    .outputs = make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_make_CALL,
    .host_call = &NATIVE_ARRAY_make_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_make_PRESENTATION,

};

node_desc NODE_ArrayGet = {
    .name = NATIVE_ARRAY_GET_NAME,
    .source_name = NATIVE_ARRAY_GET_SOURCE,
    .input_count = COUNT(array_element_inputs),
    .output_count = COUNT(array_element_outputs),
    .inputs = array_element_inputs,
    .outputs = array_element_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_get_CALL,
    NATIVE_ARRAY_get_PRESENTATION,

};

node_desc NODE_ArrayLength = {
    .name = NATIVE_ARRAY_LENGTH_NAME,
    .source_name = NATIVE_ARRAY_LENGTH_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(int_output),
    .inputs = array_only_inputs,
    .outputs = int_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_length_CALL,
    NATIVE_ARRAY_length_PRESENTATION,

};

node_desc NODE_ArrayLastIndex = {
    .name = NATIVE_ARRAY_LAST_INDEX_NAME,
    .source_name = NATIVE_ARRAY_LAST_INDEX_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(int_output),
    .inputs = array_only_inputs,
    .outputs = int_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_last_index_CALL,
    NATIVE_ARRAY_last_index_PRESENTATION,

};

node_desc NODE_ArrayIsEmpty = {
    .name = NATIVE_ARRAY_EMPTY_NAME,
    .source_name = NATIVE_ARRAY_EMPTY_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(bool_output),
    .inputs = array_only_inputs,
    .outputs = bool_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_empty_CALL,
    NATIVE_ARRAY_empty_PRESENTATION,

};

node_desc NODE_ArrayIsNotEmpty = {
    .name = NATIVE_ARRAY_NOT_EMPTY_NAME,
    .source_name = NATIVE_ARRAY_NOT_EMPTY_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(bool_output),
    .inputs = array_only_inputs,
    .outputs = bool_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_not_empty_CALL,
    NATIVE_ARRAY_not_empty_PRESENTATION,

};

node_desc NODE_ArrayIsValidIndex = {
    .name = NATIVE_ARRAY_VALID_INDEX_NAME,
    .source_name = NATIVE_ARRAY_VALID_INDEX_SOURCE,
    .input_count = COUNT(array_bool_index_inputs),
    .output_count = COUNT(bool_output),
    .inputs = array_bool_index_inputs,
    .outputs = bool_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_valid_index_CALL,
    NATIVE_ARRAY_valid_index_PRESENTATION,

};

node_desc NODE_ArrayContains = {
    .work = NODE_WORK_LINEAR_SEARCH,
    .name = NATIVE_ARRAY_CONTAINS_NAME,
    .source_name = NATIVE_ARRAY_CONTAINS_SOURCE,
    .input_count = COUNT(array_bool_inputs),
    .output_count = COUNT(bool_output),
    .inputs = array_bool_inputs,
    .outputs = bool_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_contains_CALL,
    .host_call = &NATIVE_ARRAY_contains_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_contains_PRESENTATION,

};

node_desc NODE_ArrayFind = {
    .name = NATIVE_ARRAY_FIND_NAME,
    .source_name = NATIVE_ARRAY_FIND_SOURCE,
    .work = NODE_WORK_LINEAR_SEARCH,
    .input_count = COUNT(array_bool_inputs),
    .output_count = COUNT(int_output),
    .inputs = array_bool_inputs,
    .outputs = int_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_find_CALL,
    .host_call = &NATIVE_ARRAY_find_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_find_PRESENTATION,

};

node_desc NODE_ArrayIdentical = {
    .name = NATIVE_ARRAY_IDENTICAL_NAME,
    .source_name = NATIVE_ARRAY_IDENTICAL_SOURCE,
    .input_count = COUNT(array_two_inputs),
    .output_count = COUNT(bool_output),
    .inputs = array_two_inputs,
    .outputs = bool_output,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_identical_CALL,
    .host_call = &NATIVE_ARRAY_identical_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_identical_PRESENTATION,

};

node_desc NODE_ArrayAdd = {
    .name = NATIVE_ARRAY_ADD_NAME,
    .source_name = NATIVE_ARRAY_ADD_SOURCE,
    .input_count = COUNT(array_array_item_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_array_item_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_add_CALL,
    .host_call = &NATIVE_ARRAY_add_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_add_PRESENTATION,

};

node_desc NODE_ArrayAddUnique = {
    .name = NATIVE_ARRAY_ADD_UNIQUE_NAME,
    .source_name = NATIVE_ARRAY_ADD_UNIQUE_SOURCE,
    .input_count = COUNT(array_array_item_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_array_item_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_add_unique_CALL,
    .host_call = &NATIVE_ARRAY_add_unique_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_add_unique_PRESENTATION,

};

node_desc NODE_ArrayExtend = {
    .name = NATIVE_ARRAY_EXTEND_NAME,
    .source_name = NATIVE_ARRAY_EXTEND_SOURCE,
    .input_count = COUNT(array_two_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_two_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_extend_CALL,
    .host_call = &NATIVE_ARRAY_extend_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_extend_PRESENTATION,

};

node_desc NODE_ArraySetElem = {
    .name = NATIVE_ARRAY_SET_ELEM_NAME,
    .source_name = NATIVE_ARRAY_SET_ELEM_SOURCE,
    .input_count = COUNT(array_set_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_set_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_set_elem_CALL,
    .host_call = &NATIVE_ARRAY_set_elem_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_set_elem_PRESENTATION,

};

node_desc NODE_ArrayInsert = {
    .name = NATIVE_ARRAY_INSERT_NAME,
    .source_name = NATIVE_ARRAY_INSERT_SOURCE,
    .input_count = COUNT(array_insert_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_insert_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_insert_CALL,
    .host_call = &NATIVE_ARRAY_insert_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_insert_PRESENTATION,

};

node_desc NODE_ArrayRemoveIndex = {
    .name = NATIVE_ARRAY_REMOVE_INDEX_NAME,
    .source_name = NATIVE_ARRAY_REMOVE_INDEX_SOURCE,
    .input_count = COUNT(array_bool_index_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_bool_index_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_remove_index_CALL,
    .host_call = &NATIVE_ARRAY_remove_index_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_remove_index_PRESENTATION,

};

node_desc NODE_ArrayRemoveItem = {
    .name = NATIVE_ARRAY_REMOVE_ITEM_NAME,
    .source_name = NATIVE_ARRAY_REMOVE_ITEM_SOURCE,
    .input_count = COUNT(array_bool_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_bool_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_remove_item_CALL,
    .host_call = &NATIVE_ARRAY_remove_item_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_remove_item_PRESENTATION,

};

node_desc NODE_ArrayClear = {
    .name = NATIVE_ARRAY_CLEAR_NAME,
    .source_name = NATIVE_ARRAY_CLEAR_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_only_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_clear_CALL,
    .host_call = &NATIVE_ARRAY_clear_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_clear_PRESENTATION,

};

node_desc NODE_ArrayReverse = {
    .name = NATIVE_ARRAY_REVERSE_NAME,
    .source_name = NATIVE_ARRAY_REVERSE_SOURCE,
    .input_count = COUNT(array_only_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_only_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_reverse_CALL,
    .host_call = &NATIVE_ARRAY_reverse_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_reverse_PRESENTATION,

};

node_desc NODE_ArraySwap = {
    .name = NATIVE_ARRAY_SWAP_NAME,
    .source_name = NATIVE_ARRAY_SWAP_SOURCE,
    .input_count = COUNT(array_swap_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_swap_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_swap_CALL,
    .host_call = &NATIVE_ARRAY_swap_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_swap_PRESENTATION,

};

node_desc NODE_ArrayResize = {
    .name = NATIVE_ARRAY_RESIZE_NAME,
    .source_name = NATIVE_ARRAY_RESIZE_SOURCE,
    .input_count = COUNT(array_resize_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_resize_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_resize_CALL,
    .host_call = &NATIVE_ARRAY_resize_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_resize_PRESENTATION,

};

node_desc NODE_ArraySlice = {
    .name = NATIVE_ARRAY_SLICE_NAME,
    .source_name = NATIVE_ARRAY_SLICE_SOURCE,
    .input_count = COUNT(array_slice_inputs),
    .output_count = COUNT(array_only_outputs),
    .inputs = array_slice_inputs,
    .outputs = array_only_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_ARRAY_slice_CALL,
    .host_call = &NATIVE_ARRAY_slice_HOST,
    .access = &NATIVE_ARRAY_ACCESS,
    NATIVE_ARRAY_slice_PRESENTATION,

};

static node_desc *const ARRAY_NODES[] = {
    &NODE_MakeArray,      &NODE_ArrayGet,         &NODE_ArraySetElem,
    &NODE_ArrayAdd,       &NODE_ArrayAddUnique,   &NODE_ArrayExtend,
    &NODE_ArrayClear,     &NODE_ArrayContains,    &NODE_ArrayFind,
    &NODE_ArrayIdentical, &NODE_ArrayLength,      &NODE_ArrayLastIndex,
    &NODE_ArrayResize,    &NODE_ArrayRemoveIndex, &NODE_ArrayRemoveItem,
    &NODE_ArrayIsEmpty,   &NODE_ArrayIsNotEmpty,  &NODE_ArrayIsValidIndex,
    &NODE_ArrayReverse,   &NODE_ArraySwap,        &NODE_ArrayInsert,
    &NODE_ArraySlice,
};

const registry_pack CORE_ARRAY_PACK = {
    .name = "core.array",
    .nodes = ARRAY_NODES,
    .node_count = sizeof ARRAY_NODES / sizeof ARRAY_NODES[0],
};
