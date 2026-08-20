/* core/const_nodes.c -- corrected core literal nodes, registered as EXTRA_NODES.
 *
 * CAT-30: the core literals are Bool, Byte, Integer, Integer64, Real, Name,
 * and String. Each is pure and has exactly one typed "Value" data input
 * carrying the authored default and one typed return output. The output is
 * the input, so an unconnected literal evaluates to its authored default.
 */
#include "conv_nodes.h"
#include "node.h"

static pin_value literal_identity(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    return inputs[0];
}

#define LITERAL_DESC(VAR, NAME, PTYPE, PNAME)                                  \
    static pin_spec VAR##_inputs[] = {                                         \
        { "Value", PTYPE, PNAME, PIN_DATA },                                   \
    };                                                                         \
    static pin_spec VAR##_outputs[] = {                                        \
        { "Value", PTYPE, PNAME, PIN_DATA },                                   \
    };                                                                         \
    node_desc VAR = {                                                          \
        .name = NAME,                                                          \
        .input_count = 1,                                                      \
        .output_count = 1,                                                     \
        .inputs = VAR##_inputs,                                                \
        .outputs = VAR##_outputs,                                              \
        .pure_kind = NODE_PURE_CALL,                                           \
        .fn = literal_identity,                                                \
        .display_class = NODE_DISPLAY_LITERAL,                                 \
    };

LITERAL_DESC(NODE_Bool, "Bool", PIN_BOOL, "bool")
LITERAL_DESC(NODE_Byte, "Byte", PIN_U8, "byte")
LITERAL_DESC(NODE_Integer, "Integer", PIN_I32, "int")
LITERAL_DESC(NODE_Integer64, "Integer64", PIN_I64, "int64")
LITERAL_DESC(NODE_Real, "Real", PIN_F64, "double")
LITERAL_DESC(NODE_Name, "Name", PIN_NAME, "name")
LITERAL_DESC(NODE_String, "String", PIN_STR, "const char *")

node_desc *const EXTRA_NODES[] = {
    &NODE_Bool,    &NODE_Byte,         &NODE_Integer, &NODE_Integer64,
    &NODE_Real,    &NODE_Name,         &NODE_String,  &NODE_To_String,
    &NODE_To_Real, &NODE_To_Integer64,
};
const int EXTRA_NODE_COUNT = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0];
