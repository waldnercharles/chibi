#include "toolchain/compiler_syntax.h"

#include "execution/function.h"
#include "support/arena.h"
#include "types/array.h"

#include <limits.h>
#include <string.h>
#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_chibi(void);

enum
{
    ROOT,
    KINDS,
    STARTS,
    ENDS,
    ROWS,
    COLUMNS,
    PARENTS,
    FIRST_CHILDREN,
    NEXT_SIBLINGS,
    ERRORS,
    MISSING,
    HAS_ERRORS,
    FIELDS,
    IS_NAMED,
    IS_EXTRA,
    RESULT_COUNT,
};
static const native_parameter_definition INPUTS[] = {
    { .name = "text", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "const char *" },
};
#define ARRAY_OUTPUT(NAME, TYPE, C_NAME)                                       \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = TYPE,                                                         \
      .type_name = C_NAME,                                                     \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition OUTPUTS[] = {
    { .name = "root", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    ARRAY_OUTPUT("kinds", PIN_STR, "const char *"),
    ARRAY_OUTPUT("starts", PIN_I32, "int"),
    ARRAY_OUTPUT("ends", PIN_I32, "int"),
    ARRAY_OUTPUT("rows", PIN_I32, "int"),
    ARRAY_OUTPUT("columns", PIN_I32, "int"),
    ARRAY_OUTPUT("parents", PIN_I32, "int"),
    ARRAY_OUTPUT("first_children", PIN_I32, "int"),
    ARRAY_OUTPUT("next_siblings", PIN_I32, "int"),
    ARRAY_OUTPUT("errors", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("missing", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("has_errors", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("fields", PIN_STR, "const char *"),
    ARRAY_OUTPUT("is_named", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("is_extra", PIN_BOOL, "bool"),
};
#undef ARRAY_OUTPUT
_Static_assert(NATIVE_COUNT(OUTPUTS) == RESULT_COUNT, "syntax result schema");

static const char *own(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
static const char *snapshot(arena *storage, TSTree *tree, pin_value *results)
{
    TSNode root = ts_tree_root_node(tree);
    uint32_t descendants = ts_node_descendant_count(root);
    size_t arrays = RESULT_COUNT - 1;
    if (!descendants || descendants > INT_MAX ||
        descendants > (SIZE_MAX - arrays * sizeof(pin_array)) /
                          (arrays * sizeof(pin_value))) {
        return "Syntax node count exceeds representable storage";
    }
    int capacity = (int)descendants;
    pin_array *outputs[RESULT_COUNT] = { 0 };
    for (int i = KINDS; i < RESULT_COUNT; i++) {
        outputs[i] = pin_array_snapshot_allocate(
            storage,
            OUTPUTS[i].storage,
            OUTPUTS[i].type_name,
            capacity
        );
        /* Unwritten items must never become roots if a later node fails. */
        outputs[i]->count = 0;
    }
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    const char *error = NULL;
    int count = 0, parent = -1, previous = -1;
    for (;;) {
        if (count == capacity) {
            error = "Syntax cursor exceeds descendant count";
            break;
        }
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSPoint point = ts_node_start_point(node);
        uint32_t start = ts_node_start_byte(node), end = ts_node_end_byte(node);
        if (start > INT_MAX || end > INT_MAX || point.row > INT_MAX ||
            point.column > INT_MAX) {
            error = "Syntax position exceeds integer range";
            break;
        }
        int current = count++;
        outputs[KINDS]->items[current] = (pin_value){
            .type = PIN_STR,
            .str = own(storage, ts_node_type(node))
        };
        outputs[STARTS]->items[current] = (pin_value){ .type = PIN_I32,
                                                       .i = (int)start };
        outputs[ENDS]->items[current] = (pin_value){ .type = PIN_I32, .i = (int)end };
        outputs[ROWS]->items[current] = (pin_value){ .type = PIN_I32,
                                                     .i = (int)point.row };
        outputs[COLUMNS]->items[current] = (pin_value){ .type = PIN_I32,
                                                        .i = (int)point.column };
        outputs[PARENTS]->items[current] = (pin_value){ .type = PIN_I32, .i = parent };
        outputs[FIRST_CHILDREN]->items[current] = (pin_value){ .type = PIN_I32,
                                                               .i = -1 };
        outputs[NEXT_SIBLINGS]->items[current] = (pin_value){ .type = PIN_I32,
                                                              .i = -1 };
        outputs[ERRORS]->items[current] = (pin_value){ .type = PIN_BOOL,
                                                       .b = ts_node_is_error(node) };
        outputs[MISSING]->items[current] = (pin_value){ .type = PIN_BOOL,
                                                        .b = ts_node_is_missing(node) };
        outputs[HAS_ERRORS]->items[current] = (pin_value){ .type = PIN_BOOL,
                                                           .b = ts_node_has_error(node) };
        outputs[FIELDS]->items[current] = (pin_value){
            .type = PIN_STR,
            .str = own(storage, ts_tree_cursor_current_field_name(&cursor))
        };
        outputs[IS_NAMED]->items[current] = (pin_value){ .type = PIN_BOOL,
                                                         .b = ts_node_is_named(node) };
        outputs[IS_EXTRA]->items[current] = (pin_value){ .type = PIN_BOOL,
                                                         .b = ts_node_is_extra(node) };
        for (int i = KINDS; i < RESULT_COUNT; i++) {
            outputs[i]->count = count;
        }
        if (parent >= 0) {
            if (previous < 0) {
                outputs[FIRST_CHILDREN]->items[parent].i = current;
            } else {
                outputs[NEXT_SIBLINGS]->items[previous].i = current;
            }
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            parent = current;
            previous = -1;
            continue;
        }
        for (;;) {
            if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                previous = current;
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                goto complete;
            }
            current = parent;
            parent = outputs[PARENTS]->items[current].i;
        }
    }
complete:
    ts_tree_cursor_delete(&cursor);
    if (!error) {
        results[ROOT].i = 0;
        for (int i = KINDS; i < RESULT_COUNT; i++) {
            results[i].ptr = outputs[i];
        }
    }
    return error;
}
static const char *parse(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *results
)
{
    if (!context || !context->value_storage || !arguments || !results ||
        arguments[0].type != PIN_STR || arguments[0].container != PIN_CONTAINER_SINGLE) {
        return "Invalid syntax parse arguments";
    }
    const char *text = arguments[0].str ? arguments[0].str : "";
    size_t bytes = strlen(text);
    if (bytes > UINT32_MAX || bytes > INT_MAX) {
        return "Syntax source exceeds integer range";
    }
    TSParser *parser = ts_parser_new();
    if (!parser) {
        return "Cannot create syntax parser";
    }
    if (!ts_parser_set_language(parser, tree_sitter_chibi())) {
        ts_parser_delete(parser);
        return "Syntax grammar ABI is incompatible";
    }
    TSTree *tree = ts_parser_parse_string(parser, NULL, text, (uint32_t)bytes);
    ts_parser_delete(parser);
    if (!tree) {
        return "Cannot parse syntax source";
    }
    const char *error = snapshot(context->value_storage, tree, results);
    ts_tree_delete(tree);
    return error;
}

static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Compiler Syntax Parse",
      .source_name = "compiler_syntax_parse",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CALL = { .context = parse };
static const node_access ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Compiler Syntax Parse",
      .implementation = { .call = &CALL, .access = &ACCESS } },
};
const native_definition_pack COMPILER_SYNTAX_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
