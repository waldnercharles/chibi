#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    script_math_op op;
    int arity;
} math_function;
static const math_function FUNCTIONS[] = {
#define MATH_FUNCTION(OP, NAME, ARITY) { NAME, SCRIPT_MATH_##OP, ARITY },
    SCRIPT_MATH_FUNCTIONS(MATH_FUNCTION)
#undef MATH_FUNCTION
};
static script_type scalar_type()
{
    return (script_type){ .storage = PIN_F32 };
}
script_type script_analyzer_math_type(analyzer *a, TSNode node, script_math_kind kind)
{
    if (kind == SCRIPT_MATH_SCALAR) {
        return scalar_type();
    }
    const char *name = script_math_name(kind);
    uint64_t id = script_catalog_id("type", name);
    const script_catalog_type *found = script_catalog_type_by_id(a->catalog, id);
    if (found) {
        if (!script_math_layout_valid(found)) {
            script_analyzer_diagnostic(a, node, "Built-in math type '%s' has an incompatible host layout", name);
            return script_analyzer_bad_type();
        }
        return found->type;
    }
    if (script_catalog_find_type(a->catalog, name) != -1) {
        script_analyzer_diagnostic(a, node, "Built-in math type name '%s' is reserved", name);
        return script_analyzer_bad_type();
    }
    script_catalog_type type = script_math_type(&a->out->constants, kind);
    script_analyzer_publish_type(a, type);
    return type.type;
}
/* Gather only named builtin annotations and constructors before signatures.
 * No math-free analysis or prepared module gains unused builtin types. */
static void prepare(analyzer *a, TSNode node, int depth)
{
    if (depth >= SCRIPT_MAX_DEPTH || ts_node_is_null(node)) {
        return;
    }
    TSNode name = node;
    bool type = script_analyzer_is(node, "type_name");
    bool call = script_analyzer_is(node, "call_expression");
    if (call) {
        name = script_analyzer_field(node, "function");
    }
    if (type || (call && script_analyzer_is(name, "identifier"))) {
        char *text = script_analyzer_name_copy(a, name);
        script_math_kind kind = script_math_named(text);
        free(text);
        if (kind) {
            (void)script_analyzer_math_type(a, name, kind);
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        prepare(a, ts_node_named_child(node, i), depth + 1);
    }
}
void script_analyzer_math_prepare(analyzer *a, TSNode node)
{
    prepare(a, node, 0);
}
static script_math_kind kind_of(const analyzer *a, int value)
{
    return script_math_of(a->catalog, script_analyzer_expr_type(a, value));
}
static bool valid_kind(analyzer *a, TSNode node, script_math_kind kind)
{
    return kind < SCRIPT_MATH_VEC2 ||
           !(script_analyzer_math_type(a, node, kind).flags & SCRIPT_TYPE_ERROR);
}
static bool failed(const analyzer *a, int value)
{
    return script_analyzer_expr_type(a, value).flags & SCRIPT_TYPE_ERROR;
}
static bool scalar(analyzer *a, TSNode node, int value)
{
    script_type type = script_analyzer_expr_type(a, value);
    if (type.flags & SCRIPT_TYPE_ERROR) {
        return false;
    }
    if (!script_type_numeric(type) || script_type_char(type)) {
        script_analyzer_diagnostic(a, node, "Math components require f32 scalar values");
        return false;
    }
    if (type.storage == PIN_F64 && !(type.flags & SCRIPT_TYPE_UNTYPED)) {
        script_analyzer_diagnostic(a, node, "Math components require explicit f32(value) narrowing from f64");
        return false;
    }
    script_analyzer_contextual_numeric(a, value, scalar_type());
    return !failed(a, value);
}
static script_expression expression(analyzer *a, TSNode node, script_math_op op)
{
    return (script_expression){ .kind = SCRIPT_EXPR_MATH,
                                .math_op = op,
                                .range = script_analyzer_range(a, node),
                                .symbol = -1,
                                .left = -1,
                                .right = -1,
                                .entry = -1,
                                .type = script_analyzer_bad_type() };
}
static void argument(analyzer *a, script_expression *result, TSNode node, int value)
{
    apush(result->arguments, ((script_argument){ value, -1, 0, script_analyzer_range(a, node) }));
    result->effects |= script_analyzer_expr_effects(a, value);
}
static bool positional(TSNode arguments)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode node = ts_node_named_child(arguments, i);
        if (script_analyzer_is(node, "named_argument") ||
            script_analyzer_is(node, "typed_argument")) {
            return false;
        }
    }
    return true;
}
static int construct(analyzer *a, TSNode node, script_math_kind kind, TSNode arguments)
{
    script_expression result = expression(a, node, SCRIPT_MATH_CONSTRUCT);
    result.type = script_analyzer_math_type(a, node, kind);
    if (!positional(arguments)) {
        script_analyzer_diagnostic(a, arguments, "Built-in math constructors take positional arguments");
        result.type = script_analyzer_bad_type();
        return script_analyzer_expr_add(a, result);
    }
    int count = 0, supplied = 0;
    bool valid = !(result.type.flags & SCRIPT_TYPE_ERROR);
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode node = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(node)) {
            continue;
        }
        int value = script_analyzer_expression(a, node, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        script_math_kind source = kind_of(a, value);
        valid &= valid_kind(a, node, source);
        if (script_math_is_vector(source) && kind != SCRIPT_MATH_QUAT) {
            supplied += script_math_components(source);
        } else if (script_type_numeric(script_analyzer_expr_type(a, value))) {
            valid &= scalar(a, node, value);
            supplied++;
        } else {
            valid = false;
            if (!failed(a, value)) {
                script_analyzer_diagnostic(a, node, kind == SCRIPT_MATH_QUAT ? "quat takes four f32 components in x, y, z, w order" : "Vector and matrix constructors take f32 scalars and vectors");
            }
        }
        argument(a, &result, node, value);
        count++;
    }
    bool splat = kind != SCRIPT_MATH_QUAT && count == 1 && supplied == 1;
    if (valid && !splat && supplied != script_math_components(kind)) {
        script_analyzer_diagnostic(
            a,
            arguments,
            "%s needs %d components; these arguments supply %d",
            script_math_name(kind),
            script_math_components(kind),
            supplied
        );
        valid = false;
    }
    if (!valid) {
        result.type = script_analyzer_bad_type();
    }
    return script_analyzer_expr_add(a, result);
}
static const math_function *function_named(const char *name)
{
    for (size_t i = 0; i < sizeof FUNCTIONS / sizeof *FUNCTIONS; i++) {
        if (!strcmp(name, FUNCTIONS[i].name)) {
            return &FUNCTIONS[i];
        }
    }
    return NULL;
}
static bool memberwise(script_math_op op)
{
    return op >= SCRIPT_MATH_ABS && op <= SCRIPT_MATH_SMOOTHSTEP;
}
static bool recoverable(script_math_op op)
{
    return op == SCRIPT_MATH_NORMALIZE || op == SCRIPT_MATH_NORMALIZE_OR ||
           op == SCRIPT_MATH_INVERSE || op == SCRIPT_MATH_QUAT_NORMALIZE;
}
static script_type function_type(
    analyzer *a,
    TSNode node,
    const math_function *function,
    const int *values,
    const TSNode *nodes
)
{
    for (int i = 0; i < function->arity; i++) {
        if (!valid_kind(a, nodes[i], kind_of(a, values[i]))) {
            return script_analyzer_bad_type();
        }
    }
    if (recoverable(function->op)) {
        script_analyzer_diagnostic(
            a,
            node,
            "%s requires the recoverable math result adapter",
            function->name
        );
        return script_analyzer_bad_type();
    }
    script_math_kind first = kind_of(a, values[0]);
    if (memberwise(function->op)) {
        script_math_kind width = SCRIPT_MATH_SCALAR;
        bool valid = true;
        for (int i = 0; i < function->arity; i++) {
            script_math_kind kind = kind_of(a, values[i]);
            if (script_math_is_vector(kind)) {
                if (width != SCRIPT_MATH_SCALAR && width != kind) {
                    script_analyzer_diagnostic(
                        a,
                        nodes[i],
                        "%s operands differ in vector width",
                        function->name
                    );
                    valid = false;
                }
                width = kind;
            } else {
                valid &= scalar(a, nodes[i], values[i]);
            }
        }
        return valid ? script_analyzer_math_type(a, node, width)
                     : script_analyzer_bad_type();
    }
    if (function->op == SCRIPT_MATH_TRANSPOSE || function->op == SCRIPT_MATH_DETERMINANT ||
        function->op == SCRIPT_MATH_MATRIX_COMP_MULT) {
        bool valid = script_math_is_matrix(first);
        for (int i = 1; i < function->arity; i++) {
            valid &= kind_of(a, values[i]) == first;
        }
        if (!valid) {
            script_analyzer_diagnostic(
                a,
                node,
                "%s takes matrices of one width",
                function->name
            );
            return script_analyzer_bad_type();
        }
        return function->op == SCRIPT_MATH_DETERMINANT
                 ? scalar_type()
                 : script_analyzer_math_type(a, node, first);
    }
    if (function->op == SCRIPT_MATH_QUAT_CONJUGATE) {
        if (first != SCRIPT_MATH_QUAT) {
            script_analyzer_diagnostic(a, node, "quat_conjugate takes one quaternion");
            return script_analyzer_bad_type();
        }
        return script_analyzer_math_type(a, node, first);
    }
    bool valid = script_math_is_vector(first);
    int vectors = function->op == SCRIPT_MATH_REFRACT ? 2 : function->arity;
    for (int i = 1; i < vectors; i++) {
        valid &= kind_of(a, values[i]) == first;
    }
    if (function->op == SCRIPT_MATH_CROSS && first != SCRIPT_MATH_VEC3) {
        valid = false;
    }
    if (function->op == SCRIPT_MATH_REFRACT) {
        valid &= scalar(a, nodes[2], values[2]);
    }
    if (!valid) {
        if (function->op == SCRIPT_MATH_CROSS) {
            script_analyzer_diagnostic(a, node, "cross takes two vec3 operands");
        } else {
            script_analyzer_diagnostic(
                a,
                node,
                "%s takes vectors of one width",
                function->name
            );
        }
        return script_analyzer_bad_type();
    }
    bool result_scalar = function->op == SCRIPT_MATH_LENGTH ||
                         function->op == SCRIPT_MATH_DISTANCE ||
                         function->op == SCRIPT_MATH_DOT;
    return result_scalar ? scalar_type() : script_analyzer_math_type(a, node, first);
}
int script_analyzer_math_call(analyzer *a, TSNode node, const char *name, int **analyzed)
{
    TSNode arguments = script_analyzer_field(node, "arguments");
    script_math_kind kind = script_math_named(name);
    if (kind) {
        return construct(a, node, kind, arguments);
    }
    const math_function *function = function_named(name);
    if (!function || !positional(arguments)) {
        return -1;
    }
    uint32_t total = script_analyzer_named_count(arguments);
    int *by_node = xmalloc((size_t)(total ? total : 1) * sizeof *by_node);
    int *values = NULL;
    TSNode *nodes = NULL;
    bool aggregate = false;
    for (uint32_t i = 0; i < total; i++) {
        TSNode argument_node = ts_node_named_child(arguments, i);
        by_node[i] = ts_node_is_extra(argument_node)
                       ? -1
                       : script_analyzer_expression(
                             a,
                             argument_node,
                             (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
                         );
        if (by_node[i] >= 0) {
            apush(values, by_node[i]);
            apush(nodes, argument_node);
            aggregate |= kind_of(a, by_node[i]) >= SCRIPT_MATH_VEC2;
        }
    }
    /* The catalog an ordinary call resolves against: a package sees the host's
     * natives through a->native. */
    const script_catalog *visible = a->native ? a->native : a->catalog;
    bool native = script_catalog_find_capability(visible, name, SCRIPT_CAPABILITY_FUNCTION) !=
                -1;
    if (!aggregate && native && function->op != SCRIPT_MATH_STEP) {
        afree(values);
        afree(nodes);
        *analyzed = by_node;
        return -1;
    }
    script_expression result = expression(a, node, function->op);
    int count = acount(values);
    for (int i = 0; i < count; i++) {
        argument(a, &result, nodes[i], values[i]);
    }
    if (count != function->arity) {
        script_analyzer_diagnostic(
            a,
            node,
            "%s needs %d arguments",
            name,
            function->arity
        );
    } else {
        result.type = function_type(a, node, function, values, nodes);
    }
    afree(values);
    afree(nodes);
    free(by_node);
    return script_analyzer_expr_add(a, result);
}
bool script_analyzer_math_swizzle(analyzer *a, TSNode member, int receiver, const char *name, script_expression *result)
{
    script_math_kind kind = kind_of(a, receiver);
    if (!valid_kind(a, member, kind)) {
        result->kind = SCRIPT_EXPR_ERROR;
        result->type = script_analyzer_bad_type();
        return true;
    }
    if (script_math_is_matrix(kind)) {
        script_analyzer_diagnostic(a, member, "Matrix storage fields are internal; use matrix operations");
        result->kind = SCRIPT_EXPR_ERROR;
        result->type = script_analyzer_bad_type();
        return true;
    }
    if (!script_math_is_vector(kind)) {
        return false;
    }
    size_t count = strlen(name);
    result->kind = SCRIPT_EXPR_MATH;
    result->math_op = SCRIPT_MATH_SWIZZLE;
    result->type = script_analyzer_bad_type();
    if (!count || count > 4) {
        script_analyzer_diagnostic(a, member, "A swizzle needs one to four components");
        return true;
    }
    const char *alphabet = strchr("xyzw", name[0]) ? "xyzw" : "rgba";
    for (size_t i = 0; i < count; i++) {
        const char *component = strchr(alphabet, name[i]);
        if (!component) {
            script_analyzer_diagnostic(a, member, "A swizzle uses one of xyzw or rgba without mixing them");
            return true;
        }
        int index = (int)(component - alphabet);
        if (index >= script_math_width(kind)) {
            script_analyzer_diagnostic(
                a,
                member,
                "%s has no component '%c'",
                script_math_name(kind),
                name[i]
            );
            return true;
        }
        result->swizzle[i] = (unsigned char)index;
    }
    result->swizzle_count = (int)count;
    script_expression source = a->out->expressions[receiver];
    if (source.kind == SCRIPT_EXPR_MATH && source.math_op == SCRIPT_MATH_SWIZZLE) {
        /* Compose projections so nested lvalues validate the final actual
         * destinations and lower from one receiver location. */
        for (size_t i = 0; i < count; i++) {
            result->swizzle[i] = source.swizzle[result->swizzle[i]];
        }
        result->left = source.left;
        result->projection = receiver + 1;
    }
    result->type = script_analyzer_math_type(
        a,
        member,
        count == 1 ? SCRIPT_MATH_SCALAR : (script_math_kind)count
    );
    result->writable = a->out->expressions[receiver].writable;
    if (count == 1) {
        const script_catalog_type *type = script_catalog_type_by_id(
            a->catalog,
            script_catalog_canonical_type(
                a->catalog,
                script_analyzer_expr_type(a, result->left)
            )
                .nominal
        );
        result->member = type->fields[result->swizzle[0]].id;
        result->offset = type->fields[result->swizzle[0]].offset;
    }
    return true;
}
bool script_analyzer_math_write(analyzer *a, TSNode node, const script_expression *address)
{
    if (address->kind != SCRIPT_EXPR_MATH || address->math_op != SCRIPT_MATH_SWIZZLE) {
        return true;
    }
    for (int i = 0; i < address->swizzle_count; i++) {
        for (int j = 0; j < i; j++) {
            if (address->swizzle[i] == address->swizzle[j]) {
                script_analyzer_diagnostic(a, node, "A writable swizzle cannot repeat a destination component");
                return false;
            }
        }
    }
    return true;
}
bool script_analyzer_math_operator(analyzer *a, TSNode node, script_expression *result)
{
    script_math_kind left = kind_of(a, result->left);
    script_math_kind right = result->right >= 0 ? kind_of(a, result->right)
                                                : SCRIPT_MATH_NONE;
    bool x = left >= SCRIPT_MATH_VEC2, y = right >= SCRIPT_MATH_VEC2;
    if (!x && !y) {
        return false;
    }
    result->kind = SCRIPT_EXPR_MATH;
    result->math_op = SCRIPT_MATH_OPERATOR;
    result->type = script_analyzer_bad_type();
    if (!valid_kind(a, node, left) || !valid_kind(a, node, right)) {
        return true;
    }
    if (failed(a, result->left) || (result->right >= 0 && failed(a, result->right))) {
        return true;
    }
    if (result->op == SCRIPT_OP_NEG) {
        if (script_math_is_vector(left) || script_math_is_matrix(left)) {
            result->type = script_analyzer_math_type(a, node, left);
        } else {
            script_analyzer_diagnostic(a, node, "Unary math negation needs a vector or matrix");
        }
        return true;
    }
    if (result->op == SCRIPT_OP_EQ || result->op == SCRIPT_OP_NE) {
        if (x && left == right) {
            result->type = script_analyzer_boolean_type();
        } else {
            script_analyzer_diagnostic(a, node, "Math equality requires operands of the same built-in type");
        }
        return true;
    }
    if (left == SCRIPT_MATH_QUAT || right == SCRIPT_MATH_QUAT) {
        if (result->op == SCRIPT_OP_MUL && left == SCRIPT_MATH_QUAT &&
            right == SCRIPT_MATH_QUAT) {
            result->type = script_analyzer_math_type(a, node, SCRIPT_MATH_QUAT);
        } else if (result->op == SCRIPT_OP_MUL && left == SCRIPT_MATH_QUAT && right == SCRIPT_MATH_VEC3) {
            script_analyzer_diagnostic(a, node, "Quaternion rotation requires the recoverable math result adapter");
        } else {
            script_analyzer_diagnostic(a, node, "quat supports quaternion composition and recoverable quat * vec3 rotation");
        }
        return true;
    }
    bool arithmetic = result->op == SCRIPT_OP_ADD || result->op == SCRIPT_OP_SUB ||
                      result->op == SCRIPT_OP_MUL || result->op == SCRIPT_OP_DIV;
    if (!arithmetic) {
        script_analyzer_diagnostic(a, node, "Math values support + - * / == and !=; compare components instead");
        return true;
    }
    script_math_kind output = SCRIPT_MATH_NONE;
    if ((script_math_is_vector(left) && !y) || (!x && script_math_is_vector(right))) {
        int value = x ? result->right : result->left;
        if (scalar(a, node, value)) {
            output = x ? left : right;
        }
    } else if (script_math_is_vector(left) && left == right) {
        output = left;
    } else if (script_math_is_matrix(left) && !y && (result->op == SCRIPT_OP_MUL || result->op == SCRIPT_OP_DIV)) {
        if (scalar(a, node, result->right)) {
            output = left;
        }
    } else if (!x && script_math_is_matrix(right) && result->op == SCRIPT_OP_MUL) {
        if (scalar(a, node, result->left)) {
            output = right;
        }
    } else if (script_math_is_matrix(left) && left == right && result->op != SCRIPT_OP_DIV) {
        output = left;
    } else if (
        script_math_is_matrix(left) && script_math_is_vector(right) &&
        script_math_width(left) == script_math_width(right) && result->op == SCRIPT_OP_MUL
    ) {
        output = right;
    }
    if (output) {
        result->type = script_analyzer_math_type(a, node, output);
        return true;
    }
    if (script_math_is_vector(left) && script_math_is_matrix(right) &&
        result->op == SCRIPT_OP_MUL) {
        script_analyzer_diagnostic(a, node, "A matrix multiplies a column vector on its right; write matrix * vector");
    } else {
        script_analyzer_diagnostic(
            a,
            node,
            "Cannot combine %s and %s with this math operator",
            x ? script_math_name(left) : "a scalar",
            y ? script_math_name(right) : "a scalar"
        );
    }
    return true;
}
