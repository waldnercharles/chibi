#ifndef CHIBI_NATIVE_ARRAY_DEFINITION_H
#define CHIBI_NATIVE_ARRAY_DEFINITION_H

#include "runtime/native_definitions.h"

#define NATIVE_ARRAY_PORT(X, NAME, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP) \
    X(NAME,                                                                     \
      NATIVE_PARAMETER_DATA,                                                    \
      STORAGE,                                                                  \
      TYPE_NAME,                                                                \
      CONTAINER,                                                                \
      FLAGS,                                                                    \
      GROUP,                                                                    \
      SCRIPT_REFERENCE_NONE,                                                    \
      false,                                                                    \
      ((pin_value){ .type = STORAGE, .container = CONTAINER }))

#define NATIVE_ARRAY_MAKE_INPUTS(X)                                            \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_MAKE_OUTPUTS(X)                                           \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_ELEMENT_INPUTS(X)                                                   \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_ARRAY_ELEMENT_OUTPUTS(X)                                  \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_ONLY_INPUTS(X)                                      \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS(X)                                     \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_BOOL_INPUTS(X)                                                      \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_BOOL_INDEX_INPUTS(X)                                                \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_ARRAY_ARRAY_ITEM_INPUTS(X)                                                \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_TWO_INPUTS(X)                                                       \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Other Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_SET_INPUTS(X)                                                       \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)                  \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_INSERT_INPUTS(X)                                                    \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)                  \
    NATIVE_ARRAY_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)

#define NATIVE_ARRAY_ARRAY_SWAP_INPUTS(X)                                                      \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)                  \
    NATIVE_ARRAY_PORT(X, "Other Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_ARRAY_RESIZE_INPUTS(X)                                                    \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Index", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_ARRAY_SLICE_INPUTS(X)                                                     \
    NATIVE_ARRAY_PORT(X, "Array", PIN_FLOW, NULL, PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1) \
    NATIVE_ARRAY_PORT(X, "Start", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)                  \
    NATIVE_ARRAY_PORT(X, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_BOOL_OUTPUT(X)                                            \
    NATIVE_ARRAY_PORT(X, "Return Value", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_INT_OUTPUT(X)                                             \
    NATIVE_ARRAY_PORT(X, "Return Value", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)

#define NATIVE_ARRAY_MAKE_NAME "Make Array"
#define NATIVE_ARRAY_MAKE_SOURCE "make_array"
#define NATIVE_ARRAY_GET_NAME "Get"
#define NATIVE_ARRAY_GET_SOURCE "get"
#define NATIVE_ARRAY_LENGTH_NAME "Length"
#define NATIVE_ARRAY_LENGTH_SOURCE "length"
#define NATIVE_ARRAY_LAST_INDEX_NAME "Last Index"
#define NATIVE_ARRAY_LAST_INDEX_SOURCE "last_index"
#define NATIVE_ARRAY_EMPTY_NAME "Is Empty"
#define NATIVE_ARRAY_EMPTY_SOURCE "is_empty"
#define NATIVE_ARRAY_NOT_EMPTY_NAME "Is Not Empty"
#define NATIVE_ARRAY_NOT_EMPTY_SOURCE "is_not_empty"
#define NATIVE_ARRAY_VALID_INDEX_NAME "Is Valid Index"
#define NATIVE_ARRAY_VALID_INDEX_SOURCE "is_valid_index"
#define NATIVE_ARRAY_CONTAINS_NAME "Contains"
#define NATIVE_ARRAY_CONTAINS_SOURCE "contains"
#define NATIVE_ARRAY_FIND_NAME "Find"
#define NATIVE_ARRAY_FIND_SOURCE "find"
#define NATIVE_ARRAY_IDENTICAL_NAME "Identical"
#define NATIVE_ARRAY_IDENTICAL_SOURCE "identical"
#define NATIVE_ARRAY_ADD_NAME "Append"
#define NATIVE_ARRAY_ADD_SOURCE "append"
#define NATIVE_ARRAY_ADD_UNIQUE_NAME "Add Unique"
#define NATIVE_ARRAY_ADD_UNIQUE_SOURCE "add_unique"
#define NATIVE_ARRAY_EXTEND_NAME "Extend"
#define NATIVE_ARRAY_EXTEND_SOURCE "extend"
#define NATIVE_ARRAY_SET_ELEM_NAME "Set Array Elem"
#define NATIVE_ARRAY_SET_ELEM_SOURCE "set_array_elem"
#define NATIVE_ARRAY_INSERT_NAME "Insert"
#define NATIVE_ARRAY_INSERT_SOURCE "insert"
#define NATIVE_ARRAY_REMOVE_INDEX_NAME "Remove Index"
#define NATIVE_ARRAY_REMOVE_INDEX_SOURCE "remove_index"
#define NATIVE_ARRAY_REMOVE_ITEM_NAME "Remove Item"
#define NATIVE_ARRAY_REMOVE_ITEM_SOURCE "remove_item"
#define NATIVE_ARRAY_CLEAR_NAME "Clear"
#define NATIVE_ARRAY_CLEAR_SOURCE "clear"
#define NATIVE_ARRAY_REVERSE_NAME "Reverse"
#define NATIVE_ARRAY_REVERSE_SOURCE "reverse"
#define NATIVE_ARRAY_SWAP_NAME "Swap"
#define NATIVE_ARRAY_SWAP_SOURCE "swap"
#define NATIVE_ARRAY_RESIZE_NAME "Resize"
#define NATIVE_ARRAY_RESIZE_SOURCE "resize"
#define NATIVE_ARRAY_SLICE_NAME "Slice"
#define NATIVE_ARRAY_SLICE_SOURCE "slice"

#define NATIVE_ARRAY_OPERATIONS_WITH_ADD(X, ADD)                               \
    X(make,                                                                    \
      NATIVE_ARRAY_MAKE_NAME,                                                  \
      NATIVE_ARRAY_MAKE_SOURCE,                                                \
      NATIVE_ARRAY_MAKE_INPUTS,                                                \
      NATIVE_ARRAY_MAKE_OUTPUTS,                                               \
      true)                                                                    \
    X(get,                                                                     \
      NATIVE_ARRAY_GET_NAME,                                                   \
      NATIVE_ARRAY_GET_SOURCE,                                                 \
      NATIVE_ARRAY_ARRAY_ELEMENT_INPUTS,                                       \
      NATIVE_ARRAY_ARRAY_ELEMENT_OUTPUTS,                                      \
      false)                                                                   \
    X(length,                                                                  \
      NATIVE_ARRAY_LENGTH_NAME,                                                \
      NATIVE_ARRAY_LENGTH_SOURCE,                                              \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_INT_OUTPUT,                                                 \
      false)                                                                   \
    X(last_index,                                                              \
      NATIVE_ARRAY_LAST_INDEX_NAME,                                            \
      NATIVE_ARRAY_LAST_INDEX_SOURCE,                                          \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_INT_OUTPUT,                                                 \
      false)                                                                   \
    X(empty,                                                                   \
      NATIVE_ARRAY_EMPTY_NAME,                                                 \
      NATIVE_ARRAY_EMPTY_SOURCE,                                               \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_BOOL_OUTPUT,                                                \
      false)                                                                   \
    X(not_empty,                                                               \
      NATIVE_ARRAY_NOT_EMPTY_NAME,                                             \
      NATIVE_ARRAY_NOT_EMPTY_SOURCE,                                           \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_BOOL_OUTPUT,                                                \
      false)                                                                   \
    X(valid_index,                                                             \
      NATIVE_ARRAY_VALID_INDEX_NAME,                                           \
      NATIVE_ARRAY_VALID_INDEX_SOURCE,                                         \
      NATIVE_ARRAY_ARRAY_BOOL_INDEX_INPUTS,                                    \
      NATIVE_ARRAY_BOOL_OUTPUT,                                                \
      false)                                                                   \
    X(contains,                                                                \
      NATIVE_ARRAY_CONTAINS_NAME,                                              \
      NATIVE_ARRAY_CONTAINS_SOURCE,                                            \
      NATIVE_ARRAY_ARRAY_BOOL_INPUTS,                                          \
      NATIVE_ARRAY_BOOL_OUTPUT,                                                \
      false)                                                                   \
    X(find,                                                                    \
      NATIVE_ARRAY_FIND_NAME,                                                  \
      NATIVE_ARRAY_FIND_SOURCE,                                                \
      NATIVE_ARRAY_ARRAY_BOOL_INPUTS,                                          \
      NATIVE_ARRAY_INT_OUTPUT,                                                 \
      false)                                                                   \
    X(identical,                                                               \
      NATIVE_ARRAY_IDENTICAL_NAME,                                             \
      NATIVE_ARRAY_IDENTICAL_SOURCE,                                           \
      NATIVE_ARRAY_ARRAY_TWO_INPUTS,                                           \
      NATIVE_ARRAY_BOOL_OUTPUT,                                                \
      false)                                                                   \
    ADD(add,                                                                   \
        NATIVE_ARRAY_ADD_NAME,                                                 \
        NATIVE_ARRAY_ADD_SOURCE,                                               \
        NATIVE_ARRAY_ARRAY_ARRAY_ITEM_INPUTS,                                  \
        NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                       \
        false)                                                                 \
    X(add_unique,                                                              \
      NATIVE_ARRAY_ADD_UNIQUE_NAME,                                            \
      NATIVE_ARRAY_ADD_UNIQUE_SOURCE,                                          \
      NATIVE_ARRAY_ARRAY_ARRAY_ITEM_INPUTS,                                    \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(extend,                                                                  \
      NATIVE_ARRAY_EXTEND_NAME,                                                \
      NATIVE_ARRAY_EXTEND_SOURCE,                                              \
      NATIVE_ARRAY_ARRAY_TWO_INPUTS,                                           \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(set_elem,                                                                \
      NATIVE_ARRAY_SET_ELEM_NAME,                                              \
      NATIVE_ARRAY_SET_ELEM_SOURCE,                                            \
      NATIVE_ARRAY_ARRAY_SET_INPUTS,                                           \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(insert,                                                                  \
      NATIVE_ARRAY_INSERT_NAME,                                                \
      NATIVE_ARRAY_INSERT_SOURCE,                                              \
      NATIVE_ARRAY_ARRAY_INSERT_INPUTS,                                        \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(remove_index,                                                            \
      NATIVE_ARRAY_REMOVE_INDEX_NAME,                                          \
      NATIVE_ARRAY_REMOVE_INDEX_SOURCE,                                        \
      NATIVE_ARRAY_ARRAY_BOOL_INDEX_INPUTS,                                    \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(remove_item,                                                             \
      NATIVE_ARRAY_REMOVE_ITEM_NAME,                                           \
      NATIVE_ARRAY_REMOVE_ITEM_SOURCE,                                         \
      NATIVE_ARRAY_ARRAY_BOOL_INPUTS,                                          \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(clear,                                                                   \
      NATIVE_ARRAY_CLEAR_NAME,                                                 \
      NATIVE_ARRAY_CLEAR_SOURCE,                                               \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(reverse,                                                                 \
      NATIVE_ARRAY_REVERSE_NAME,                                               \
      NATIVE_ARRAY_REVERSE_SOURCE,                                             \
      NATIVE_ARRAY_ARRAY_ONLY_INPUTS,                                          \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(swap,                                                                    \
      NATIVE_ARRAY_SWAP_NAME,                                                  \
      NATIVE_ARRAY_SWAP_SOURCE,                                                \
      NATIVE_ARRAY_ARRAY_SWAP_INPUTS,                                          \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(resize,                                                                  \
      NATIVE_ARRAY_RESIZE_NAME,                                                \
      NATIVE_ARRAY_RESIZE_SOURCE,                                              \
      NATIVE_ARRAY_ARRAY_RESIZE_INPUTS,                                        \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)                                                                   \
    X(slice,                                                                   \
      NATIVE_ARRAY_SLICE_NAME,                                                 \
      NATIVE_ARRAY_SLICE_SOURCE,                                               \
      NATIVE_ARRAY_ARRAY_SLICE_INPUTS,                                         \
      NATIVE_ARRAY_ARRAY_ONLY_OUTPUTS,                                         \
      false)

extern const native_definition_pack NATIVE_ARRAY_DEFINITION_PACK;

#define NATIVE_ARRAY_OPERATIONS(X) NATIVE_ARRAY_OPERATIONS_WITH_ADD(X, X)

#define NATIVE_ARRAY_make_PRESENTATION                                         \
    .category = "Array", .keywords = "array make collection",                  \
    .description = "Builds an array from the Item inputs in pin order."
#define NATIVE_ARRAY_get_PRESENTATION                                          \
    .category = "Array", .keywords = "array get index",                        \
    .description = "Returns the element at zero-based Index; an out-of-range " \
                   "Index fails evaluation."
#define NATIVE_ARRAY_length_PRESENTATION                                       \
    .category = "Array", .keywords = "array length",                           \
    .description = "Returns the number of elements in Array."
#define NATIVE_ARRAY_last_index_PRESENTATION                                   \
    .category = "Array", .keywords = "array last index",                       \
    .description = "Returns the index of the final element, or -1 when Array " \
                   "is empty."
#define NATIVE_ARRAY_empty_PRESENTATION                                        \
    .category = "Array", .keywords = "array empty",                            \
    .description = "Whether Array has no elements."
#define NATIVE_ARRAY_not_empty_PRESENTATION                                    \
    .category = "Array", .keywords = "array nonempty",                         \
    .description = "Whether Array has at least one element."
#define NATIVE_ARRAY_valid_index_PRESENTATION                                  \
    .category = "Array", .keywords = "array index valid",                      \
    .description = "Whether Index is at least 0 and less than the length of "  \
                   "Array."
#define NATIVE_ARRAY_contains_PRESENTATION                                     \
    .category = "Array", .keywords = "array contains",                         \
    .description = "Whether any element of Array equals Item."
#define NATIVE_ARRAY_find_PRESENTATION                                         \
    .category = "Array", .keywords = "array find",                             \
    .description = "Returns the zero-based index of the first element equal "  \
                   "to "                                                       \
                   "Item, or -1 when none matches."
#define NATIVE_ARRAY_identical_PRESENTATION                                    \
    .category = "Array", .keywords = "array identical equality",               \
    .description = "Whether both arrays have the same element type, length "   \
                   "and "                                                      \
                   "equal elements in order."
#define NATIVE_ARRAY_add_PRESENTATION                                          \
    .category = "Array", .keywords = "array append add push",                  \
    .description = "Returns a copy of Array with Item added at the end."
#define NATIVE_ARRAY_add_unique_PRESENTATION                                   \
    .category = "Array", .keywords = "array add unique",                       \
    .description = "Returns a copy of Array with Item added at the end "       \
                   "unless "                                                   \
                   "an equal element exists."
#define NATIVE_ARRAY_extend_PRESENTATION                                       \
    .category = "Array", .keywords = "array extend concatenate",               \
    .description = "Returns a new array with the second Array's elements "     \
                   "after "                                                    \
                   "the first's."
#define NATIVE_ARRAY_set_elem_PRESENTATION                                     \
    .category = "Array", .keywords = "array set element",                      \
    .description = "Returns a copy of Array with Item at Index; an "           \
                   "out-of-range Index leaves it unchanged."
#define NATIVE_ARRAY_insert_PRESENTATION                                       \
    .category = "Array", .keywords = "array insert",                           \
    .description = "Returns a copy of Array with Item inserted at Index, "     \
                   "which "                                                    \
                   "is clamped to 0 through the length."
#define NATIVE_ARRAY_remove_index_PRESENTATION                                 \
    .category = "Array", .keywords = "array remove index",                     \
    .description = "Returns a copy of Array without the element at Index; an " \
                   "out-of-range Index changes nothing."
#define NATIVE_ARRAY_remove_item_PRESENTATION                                  \
    .category = "Array", .keywords = "array remove item",                      \
    .description = "Returns a copy of Array without the first element equal "  \
                   "to "                                                       \
                   "Item."
#define NATIVE_ARRAY_clear_PRESENTATION                                        \
    .category = "Array",                                                       \
    .keywords = "array clear", .description = "Returns an empty array with the same element type as Array."
#define NATIVE_ARRAY_reverse_PRESENTATION                                      \
    .category = "Array",                                                       \
    .keywords = "array reverse", .description = "Returns a copy of Array with its elements in reverse order."
#define NATIVE_ARRAY_swap_PRESENTATION                                         \
    .category = "Array", .keywords = "array swap",                             \
    .description = "Returns a copy of Array with the elements at the two "     \
                   "Index "                                                    \
                   "inputs exchanged; out-of-range leaves it unchanged."
#define NATIVE_ARRAY_resize_PRESENTATION                                       \
    .category = "Array", .keywords = "array resize",                           \
    .description = "Returns a copy of Array resized to Index elements, "       \
                   "truncating or padding with zero values; below 0 gives "    \
                   "empty."
#define NATIVE_ARRAY_slice_PRESENTATION                                        \
    .category = "Array",                                                       \
    .keywords = "array slice sub range window page subset part",               \
    .description = "Returns the Count elements of Array from Start, clamped "  \
                   "to "                                                       \
                   "the array, as a list shows one page of rows."

#endif
