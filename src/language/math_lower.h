/* Private scalar lowering included by lower.c after its helpers. */
/* Built-in vectors and matrices lower to scalar operations on their float
 * components; each result is constructed once through its marked layout. */
#define VECTOR_PARTS 16
typedef struct
{
    int slot[VECTOR_PARTS];
    int count;
} vector_parts;
static const script_catalog_type *vector_layout(lower *l, script_math_kind kind)
{
    uint64_t id = script_catalog_id("type", script_math_name(kind));
    const script_catalog_type *type = script_catalog_type_by_id(l->catalog, id);
    assert(type && script_math_layout_valid(type));
    return type;
}
static size_t vector_offset(lower *l, script_math_kind kind, int component)
{
    return vector_layout(l, kind)->fields[component].offset;
}
static int scalar_constant(lower *l, pin_type type, double value, script_range range)
{
    pin_value constant_value = { .type = type };
    if (type == PIN_F32) {
        constant_value.f = (float)value;
    } else {
        constant_value.d = value;
    }
    return constant(l, constant_value, range);
}
static int scalar_op(lower *l, behavior_function_op op, pin_type type, int left, int right, script_range range)
{
    return value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = op, .type = type, .left = left, .right = right, .target = -1 },
        range
    );
}
static int scalar_unary(lower *l, behavior_function_op op, pin_type type, int value, script_range range)
{
    return value_op(l, (behavior_function_instruction){ .op = op, .type = type, .left = value }, range);
}
/* Math instructions compute in double precision; a unary one takes no right. */
static int scalar_math(lower *l, behavior_function_op op, pin_type type, int left, int right, script_range range)
{
    script_type narrow = { .storage = type }, wide = { .storage = PIN_F64 };
    behavior_function_instruction i = { .op = op,
                                        .type = PIN_F64,
                                        .left = convert(l, left, narrow, wide, range) };
    if (right >= 0) {
        i.right = convert(l, right, narrow, wide, range);
    }
    return convert(l, value_op(l, i, range), wide, narrow, range);
}
static int scalar_select(lower *l, pin_type type, int condition, int yes, int no, script_range range)
{
    return value_op(
        l,
        (
            behavior_function_instruction
        ){ .op = BF_SELECT, .type = type, .left = condition, .right = yes, .target = no },
        range
    );
}
/* Components are copied to scalar slots before later operands can mutate them. */
static void vector_read_value(
    lower *l,
    int value,
    script_type source,
    script_range range,
    pin_type element,
    vector_parts *out
)
{
    script_math_kind kind = script_math_of(l->catalog, source);
    script_type type = { .storage = element }, component = { .storage = PIN_F32 };
    if (kind < SCRIPT_MATH_VEC2) {
        out->slot[0] = convert(l, value, source, type, range);
        out->count = 1;
        return;
    }
    out->count = script_math_components(kind);
    for (int k = 0; k < out->count; k++) {
        int read = typed_op(
            l,
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = PIN_F32,
                                             .left = value,
                                             .offset = vector_offset(l, kind, k) },
            range,
            component
        );
        out->slot[k] = convert(l, read, component, type, range);
    }
}
static void vector_read(lower *l, int index, pin_type element, vector_parts *out)
{
    const script_expression *e = &l->a->expressions[index];
    vector_read_value(l, expression(l, index), e->type, e->range, element, out);
}
static int vector_make(lower *l, script_type type, const int *parts, script_range range)
{
    script_math_kind vector = script_math_of(l->catalog, type);
    int count = script_math_components(vector);
    pin_value headers[1 + 2 * VECTOR_PARTS];
    int slots[1 + 2 * VECTOR_PARTS];
    headers[0] = initial(l, type);
    slots[0] = -1;
    for (int k = 0; k < count; k++) {
        headers[1 + 2 * k] = (pin_value){ .type = PIN_U64,
                                          .u64 = vector_offset(l, vector, k) };
        slots[1 + 2 * k] = -1;
        headers[2 + 2 * k] = (pin_value){ .type = PIN_F32 };
        slots[2 + 2 * k] = parts[k];
    }
    return primitive(
        l,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        headers,
        slots,
        1 + 2 * count,
        type,
        range
    );
}
static int vector_construct(lower *l, const script_expression *e)
{
    script_math_kind vector = script_math_of(l->catalog, e->type);
    int count = script_math_components(vector), parts[VECTOR_PARTS], supplied = 0;
    for (int i = 0; i < e->argument_count; i++) {
        vector_parts argument;
        vector_read(l, e->arguments[i].value, PIN_F32, &argument);
        assert(supplied + argument.count <= VECTOR_PARTS);
        memcpy(parts + supplied, argument.slot, (size_t)argument.count * sizeof *parts);
        supplied += argument.count;
    }
    if (supplied == 1 && count > 1) {
        /* One scalar fills a vector, or the diagonal of a matrix. */
        int size = script_math_width(vector);
        int zero = script_math_is_matrix(vector)
                     ? scalar_constant(l, PIN_F32, 0, e->range)
                     : -1;
        for (int k = 1; k < count; k++) {
            parts[k] = zero < 0 || k / size == k % size ? parts[0] : zero;
        }
        supplied = count;
    }
    assert(supplied == count);
    return vector_make(l, e->type, parts, e->range);
}
static int vector_dot(lower *l, const vector_parts *x, const vector_parts *y, script_range range)
{
    int sum = scalar_op(l, BF_MUL, PIN_F32, x->slot[0], y->slot[0], range);
    for (int k = 1; k < x->count; k++) {
        int product = scalar_op(l, BF_MUL, PIN_F32, x->slot[k], y->slot[k], range);
        sum = scalar_op(l, BF_ADD, PIN_F32, sum, product, range);
    }
    return sum;
}
static int vector_length(lower *l, const vector_parts *x, script_range range)
{
    return scalar_math(l, BF_SQRT, PIN_F32, vector_dot(l, x, x, range), -1, range);
}
static int vector_equal(lower *l, const vector_parts *x, const vector_parts *y, script_range range)
{
    int none = constant(l, (pin_value){ .type = PIN_BOOL }, range);
    int all = scalar_op(l, BF_EQ, PIN_F32, x->slot[0], y->slot[0], range);
    for (int k = 1; k < x->count; k++) {
        int same = scalar_op(l, BF_EQ, PIN_F32, x->slot[k], y->slot[k], range);
        all = scalar_select(l, PIN_BOOL, all, same, none, range);
    }
    return all;
}
/* matN * vecN and matN * matN, with column-major components. */
static int matrix_product(lower *l, const script_expression *e, const vector_parts *x, const vector_parts *y)
{
    script_math_kind
        matrix = script_math_of(l->catalog, l->a->expressions[e->left].type);
    int size = script_math_width(matrix), columns = y->count / size,
        parts[VECTOR_PARTS];
    for (int c = 0; c < columns; c++) {
        for (int r = 0; r < size; r++) {
            int sum = -1;
            for (int j = 0; j < size; j++) {
                int product = scalar_op(
                    l,
                    BF_MUL,
                    PIN_F32,
                    x->slot[j * size + r],
                    y->slot[c * size + j],
                    e->range
                );
                sum = sum < 0
                        ? product
                        : scalar_op(l, BF_ADD, PIN_F32, sum, product, e->range);
            }
            parts[c * size + r] = sum;
        }
    }
    return vector_make(l, e->type, parts, e->range);
}
static int quaternion_product(
    lower *l,
    const script_expression *e,
    const vector_parts *x,
    const vector_parts *y
)
{
    static const int left[4][4] = { { 3, 0, 1, 2 },
                                    { 3, 0, 1, 2 },
                                    { 3, 0, 1, 2 },
                                    { 3, 0, 1, 2 } };
    static const int right[4][4] = { { 0, 3, 2, 1 },
                                     { 1, 2, 3, 0 },
                                     { 2, 1, 0, 3 },
                                     { 3, 0, 1, 2 } };
    static const int signs[4][4] = { { 1, 1, 1, -1 },
                                     { 1, -1, 1, 1 },
                                     { 1, 1, -1, 1 },
                                     { 1, -1, -1, -1 } };
    int parts[4];
    for (int k = 0; k < 4; k++) {
        parts[k] = -1;
        for (int j = 0; j < 4; j++) {
            int term = scalar_op(l, BF_MUL, PIN_F32, x->slot[left[k][j]], y->slot[right[k][j]], e->range);
            parts[k] = parts[k] < 0 ? term
                                    : scalar_op(
                                          l,
                                          signs[k][j] > 0 ? BF_ADD : BF_SUB,
                                          PIN_F32,
                                          parts[k],
                                          term,
                                          e->range
                                      );
        }
    }
    return vector_make(l, e->type, parts, e->range);
}
static int vector_operator(lower *l, const script_expression *e)
{
    vector_parts x, y = { .count = 0 };
    vector_read(l, e->left, PIN_F32, &x);
    if (e->right >= 0) {
        vector_read(l, e->right, PIN_F32, &y);
    }
    if (e->op == SCRIPT_OP_EQ || e->op == SCRIPT_OP_NE) {
        int equal = vector_equal(l, &x, &y, e->range);
        return e->op == SCRIPT_OP_EQ
                 ? equal
                 : scalar_op(
                       l,
                       BF_EQ,
                       PIN_BOOL,
                       equal,
                       constant(l, (pin_value){ .type = PIN_BOOL }, e->range),
                       e->range
                   );
    }
    script_math_kind
        left = script_math_of(l->catalog, l->a->expressions[e->left].type);
    if (left == SCRIPT_MATH_QUAT && e->op == SCRIPT_OP_MUL) {
        return quaternion_product(l, e, &x, &y);
    }
    if (e->op == SCRIPT_OP_MUL && script_math_is_matrix(left) && y.count > 1) {
        return matrix_product(l, e, &x, &y);
    }
    int parts[VECTOR_PARTS], count = x.count > y.count ? x.count : y.count;
    for (int k = 0; k < count; k++) {
        int a = x.slot[x.count == 1 ? 0 : k];
        if (e->op == SCRIPT_OP_NEG) {
            parts[k] = scalar_unary(l, BF_NEG, PIN_F32, a, e->range);
            continue;
        }
        int b = y.slot[y.count == 1 ? 0 : k];
        behavior_function_op op = e->op == SCRIPT_OP_MOD ? BF_MOD_F32
                                                         : binary_op(e->op);
        parts[k] = scalar_op(l, op, PIN_F32, a, b, e->range);
    }
    return vector_make(l, e->type, parts, e->range);
}
static int componentwise(lower *l, script_math_op op, pin_type t, const int *v, script_range range)
{
    switch (op) {
        case SCRIPT_MATH_ABS: return scalar_unary(l, BF_ABS, t, v[0], range);
        case SCRIPT_MATH_SIGN:
            return scalar_math(l, BF_SIGN, t, v[0], -1, range);
        case SCRIPT_MATH_FLOOR:
            return scalar_math(l, BF_FLOOR, t, v[0], -1, range);
        case SCRIPT_MATH_CEIL:
            return scalar_math(l, BF_CEIL, t, v[0], -1, range);
        case SCRIPT_MATH_ROUND:
            return scalar_math(l, BF_ROUND, t, v[0], -1, range);
        case SCRIPT_MATH_FRACT:
            return scalar_math(l, BF_FRACT, t, v[0], -1, range);
        case SCRIPT_MATH_SQRT:
            return scalar_math(l, BF_SQRT, t, v[0], -1, range);
        case SCRIPT_MATH_EXP: return scalar_math(l, BF_EXP, t, v[0], -1, range);
        case SCRIPT_MATH_EXP2:
            return scalar_math(l, BF_POW, t, scalar_constant(l, t, 2, range), v[0], range);
        case SCRIPT_MATH_LOG2: {
            int logarithm = scalar_math(l, BF_LOG, t, v[0], -1, range);
            return scalar_op(l, BF_DIV, t, logarithm, scalar_constant(l, t, 0.69314718055994530942, range), range);
        }
        case SCRIPT_MATH_SATURATE:
            return scalar_op(
                l,
                BF_MIN,
                t,
                scalar_op(l, BF_MAX, t, v[0], scalar_constant(l, t, 0, range), range),
                scalar_constant(l, t, 1, range),
                range
            );
        case SCRIPT_MATH_LOG: return scalar_math(l, BF_LOG, t, v[0], -1, range);
        case SCRIPT_MATH_SIN: return scalar_math(l, BF_SIN, t, v[0], -1, range);
        case SCRIPT_MATH_COS: return scalar_math(l, BF_COS, t, v[0], -1, range);
        case SCRIPT_MATH_TAN: return scalar_math(l, BF_TAN, t, v[0], -1, range);
        case SCRIPT_MATH_ASIN:
            return scalar_math(l, BF_ASIN, t, v[0], -1, range);
        case SCRIPT_MATH_ACOS:
            return scalar_math(l, BF_ACOS, t, v[0], -1, range);
        case SCRIPT_MATH_ATAN:
            return scalar_math(l, BF_ATAN, t, v[0], -1, range);
        case SCRIPT_MATH_MOD: {
            /* GLSL mod: x - y * floor(x / y), which takes the sign of y. */
            int quotient = scalar_math(
                l,
                BF_FLOOR,
                t,
                scalar_op(l, BF_DIV, t, v[0], v[1], range),
                -1,
                range
            );
            return scalar_op(l, BF_SUB, t, v[0], scalar_op(l, BF_MUL, t, v[1], quotient, range), range);
        }
        case SCRIPT_MATH_MIN: return scalar_op(l, BF_MIN, t, v[0], v[1], range);
        case SCRIPT_MATH_MAX: return scalar_op(l, BF_MAX, t, v[0], v[1], range);
        case SCRIPT_MATH_POW:
            return scalar_math(l, BF_POW, t, v[0], v[1], range);
        case SCRIPT_MATH_ATAN2:
            return scalar_math(l, BF_ATAN2, t, v[0], v[1], range);
        case SCRIPT_MATH_STEP:
            return scalar_select(
                l,
                t,
                scalar_op(l, BF_LT, t, v[1], v[0], range),
                scalar_constant(l, t, 0, range),
                scalar_constant(l, t, 1, range),
                range
            );
        case SCRIPT_MATH_CLAMP:
            return scalar_op(l, BF_MIN, t, scalar_op(l, BF_MAX, t, v[0], v[1], range), v[2], range);
        case SCRIPT_MATH_MIX: {
            int span = scalar_op(l, BF_SUB, t, v[1], v[0], range);
            return scalar_op(l, BF_ADD, t, v[0], scalar_op(l, BF_MUL, t, span, v[2], range), range);
        }
        case SCRIPT_MATH_SMOOTHSTEP: {
            int offset = scalar_op(l, BF_SUB, t, v[2], v[0], range);
            int span = scalar_op(l, BF_SUB, t, v[1], v[0], range);
            int x = scalar_op(l, BF_DIV, t, offset, span, range);
            x = scalar_op(l, BF_MAX, t, x, scalar_constant(l, t, 0, range), range);
            x = scalar_op(l, BF_MIN, t, x, scalar_constant(l, t, 1, range), range);
            int rise = scalar_op(l, BF_MUL, t, scalar_constant(l, t, 2, range), x, range);
            rise = scalar_op(l, BF_SUB, t, scalar_constant(l, t, 3, range), rise, range);
            return scalar_op(l, BF_MUL, t, scalar_op(l, BF_MUL, t, x, x, range), rise, range);
        }
        default: break;
    }
    assert(!"component-wise vector function");
    return -1;
}
static int matrix_determinant(lower *l, const int *components, int width, script_range range)
{
    if (width == 1) {
        return components[0];
    }
    int sum = -1;
    for (int column = 0; column < width; column++) {
        int minor[VECTOR_PARTS], count = 0;
        for (int c = 0; c < width; c++) {
            if (c == column) {
                continue;
            }
            for (int r = 1; r < width; r++) {
                minor[count++] = components[c * width + r];
            }
        }
        int term = scalar_op(
            l,
            BF_MUL,
            PIN_F32,
            components[column * width],
            matrix_determinant(l, minor, width - 1, range),
            range
        );
        sum = sum < 0
                ? term
                : scalar_op(l, column % 2 ? BF_SUB : BF_ADD, PIN_F32, sum, term, range);
    }
    return sum;
}
static int vector_function(lower *l, const script_expression *e)
{
    script_math_kind result = script_math_of(l->catalog, e->type);
    pin_type element = result >= SCRIPT_MATH_VEC2 ? PIN_F32 : slot_type(e->type);
    vector_parts args[3];
    assert(e->argument_count <= 3);
    for (int i = 0; i < e->argument_count; i++) {
        vector_read(l, e->arguments[i].value, element, &args[i]);
    }
    int parts[VECTOR_PARTS];
    if (e->math_op >= SCRIPT_MATH_ABS && e->math_op <= SCRIPT_MATH_SMOOTHSTEP) {
        int count = result >= SCRIPT_MATH_VEC2 ? script_math_components(result) : 1;
        for (int k = 0; k < count; k++) {
            int operands[3];
            for (int i = 0; i < e->argument_count; i++) {
                operands[i] = args[i].slot[args[i].count == 1 ? 0 : k];
            }
            parts[k] = componentwise(l, e->math_op, element, operands, e->range);
        }
        return result >= SCRIPT_MATH_VEC2 ? vector_make(l, e->type, parts, e->range)
                                          : parts[0];
    }
    const vector_parts *x = &args[0], *y = &args[1];
    switch (e->math_op) {
        case SCRIPT_MATH_LENGTH: return vector_length(l, x, e->range);
        case SCRIPT_MATH_DOT: return vector_dot(l, x, y, e->range);
        case SCRIPT_MATH_DISTANCE: {
            vector_parts delta = { .count = x->count };
            for (int k = 0; k < x->count; k++) {
                delta.slot[k] = scalar_op(l, BF_SUB, PIN_F32, x->slot[k], y->slot[k], e->range);
            }
            return vector_length(l, &delta, e->range);
        }
        case SCRIPT_MATH_CROSS:
            for (int k = 0; k < 3; k++) {
                int a = (k + 1) % 3, b = (k + 2) % 3;
                parts[k] = scalar_op(
                    l,
                    BF_SUB,
                    PIN_F32,
                    scalar_op(l, BF_MUL, PIN_F32, x->slot[a], y->slot[b], e->range),
                    scalar_op(l, BF_MUL, PIN_F32, x->slot[b], y->slot[a], e->range),
                    e->range
                );
            }
            return vector_make(l, e->type, parts, e->range);
        case SCRIPT_MATH_NORMALIZE_OR_ZERO: {
            int zero = scalar_constant(l, PIN_F32, 0, e->range);
            int maximum = zero;
            int valid = constant(l, (pin_value){ .type = PIN_BOOL, .b = true }, e->range);
            int no = constant(l, (pin_value){ .type = PIN_BOOL }, e->range);
            int limit = scalar_constant(l, PIN_F32, 3.4028234663852886e38, e->range);
            for (int k = 0; k < x->count; k++) {
                int magnitude = scalar_unary(l, BF_ABS, PIN_F32, x->slot[k], e->range);
                maximum = scalar_op(l, BF_MAX, PIN_F32, maximum, magnitude, e->range);
                int finite = scalar_op(l, BF_LE, PIN_F32, magnitude, limit, e->range);
                valid = scalar_select(l, PIN_BOOL, valid, finite, no, e->range);
            }
            int nonzero = scalar_op(l, BF_GT, PIN_F32, maximum, zero, e->range);
            valid = scalar_select(l, PIN_BOOL, valid, nonzero, no, e->range);
            int one = scalar_constant(l, PIN_F32, 1, e->range);
            int scale = scalar_select(l, PIN_F32, valid, maximum, one, e->range);
            vector_parts scaled = { .count = x->count };
            for (int k = 0; k < x->count; k++) {
                int component = scalar_select(l, PIN_F32, valid, x->slot[k], zero, e->range);
                scaled.slot[k] = scalar_op(l, BF_DIV, PIN_F32, component, scale, e->range);
            }
            int norm = vector_length(l, &scaled, e->range);
            norm = scalar_select(l, PIN_F32, valid, norm, one, e->range);
            for (int k = 0; k < x->count; k++) {
                parts[k] = scalar_op(l, BF_DIV, PIN_F32, scaled.slot[k], norm, e->range);
            }
            return vector_make(l, e->type, parts, e->range);
        }
        case SCRIPT_MATH_TRANSPOSE: {
            int width = script_math_width(result);
            for (int c = 0; c < width; c++) {
                for (int r = 0; r < width; r++) {
                    parts[c * width + r] = x->slot[r * width + c];
                }
            }
            return vector_make(l, e->type, parts, e->range);
        }
        case SCRIPT_MATH_DETERMINANT:
            return matrix_determinant(
                l,
                x->slot,
                script_math_width(script_math_of(
                    l->catalog,
                    l->a->expressions[e->arguments[0].value].type
                )),
                e->range
            );
        case SCRIPT_MATH_MATRIX_COMP_MULT:
            for (int k = 0; k < x->count; k++) {
                parts[k] = scalar_op(l, BF_MUL, PIN_F32, x->slot[k], y->slot[k], e->range);
            }
            return vector_make(l, e->type, parts, e->range);
        case SCRIPT_MATH_QUAT_CONJUGATE:
            for (int k = 0; k < 4; k++) {
                parts[k] = k == 3
                             ? x->slot[k]
                             : scalar_unary(l, BF_NEG, PIN_F32, x->slot[k], e->range);
            }
            return vector_make(l, e->type, parts, e->range);
        case SCRIPT_MATH_REFRACT: {
            int zero = scalar_constant(l, PIN_F32, 0, e->range);
            int one = scalar_constant(l, PIN_F32, 1, e->range);
            int eta = args[2].slot[0];
            int dot = vector_dot(l, y, x, e->range);
            int eta2 = scalar_op(l, BF_MUL, PIN_F32, eta, eta, e->range);
            int dot2 = scalar_op(l, BF_MUL, PIN_F32, dot, dot, e->range);
            int k = scalar_op(
                l,
                BF_SUB,
                PIN_F32,
                one,
                scalar_op(l, BF_MUL, PIN_F32, eta2, scalar_op(l, BF_SUB, PIN_F32, one, dot2, e->range), e->range),
                e->range
            );
            int negative = scalar_op(l, BF_LT, PIN_F32, k, zero, e->range);
            int root = scalar_math(
                l,
                BF_SQRT,
                PIN_F32,
                scalar_select(l, PIN_F32, negative, zero, k, e->range),
                -1,
                e->range
            );
            int along = scalar_op(
                l,
                BF_ADD,
                PIN_F32,
                scalar_op(l, BF_MUL, PIN_F32, eta, dot, e->range),
                root,
                e->range
            );
            for (int n = 0; n < x->count; n++) {
                int value = scalar_op(
                    l,
                    BF_SUB,
                    PIN_F32,
                    scalar_op(l, BF_MUL, PIN_F32, eta, x->slot[n], e->range),
                    scalar_op(l, BF_MUL, PIN_F32, along, y->slot[n], e->range),
                    e->range
                );
                parts[n] = scalar_select(l, PIN_F32, negative, zero, value, e->range);
            }
            return vector_make(l, e->type, parts, e->range);
        }
        case SCRIPT_MATH_REFLECT: {
            /* I - 2 * dot(N, I) * N */
            int along = vector_dot(l, y, x, e->range);
            along = scalar_op(l, BF_MUL, PIN_F32, scalar_constant(l, PIN_F32, 2, e->range), along, e->range);
            for (int k = 0; k < x->count; k++) {
                int offset = scalar_op(l, BF_MUL, PIN_F32, along, y->slot[k], e->range);
                parts[k] = scalar_op(l, BF_SUB, PIN_F32, x->slot[k], offset, e->range);
            }
            return vector_make(l, e->type, parts, e->range);
        }
        default: break;
    }
    assert(!"geometric vector function");
    return -1;
}
static void math_swizzle_assign(lower *l, const script_statement *statement)
{
    const script_expression *swizzle = &l->a->expressions[statement->expression];
    const script_expression *operation = &l->a->expressions[statement->second];
    bool synthesized = operation->kind == SCRIPT_EXPR_MATH &&
                       operation->math_op == SCRIPT_MATH_OPERATOR &&
                       operation->left == statement->expression;
    script_operator op = synthesized ? operation->op : statement->op;
    int rhs_index = synthesized ? operation->right : statement->second;
    int receiver = swizzle->left;
    script_type type = l->a->expressions[receiver].type;
    int destination = reference(l, receiver);
    vector_parts old = { 0 }, rhs, whole;
    if (op != SCRIPT_OP_NONE) {
        vector_read_value(
            l,
            target_read(l, destination, receiver, statement->range),
            type,
            statement->range,
            PIN_F32,
            &old
        );
    }
    vector_read(l, rhs_index, PIN_F32, &rhs);
    /* Preserve RHS changes to components that this swizzle does not assign. */
    vector_read_value(
        l,
        target_read(l, destination, receiver, statement->range),
        type,
        statement->range,
        PIN_F32,
        &whole
    );
    for (int k = 0; k < swizzle->swizzle_count; k++) {
        int component = rhs.slot[rhs.count == 1 ? 0 : k];
        int target = swizzle->swizzle[k];
        if (op != SCRIPT_OP_NONE) {
            component = scalar_op(
                l,
                binary_op(op),
                PIN_F32,
                old.slot[target],
                component,
                statement->range
            );
        }
        whole.slot[target] = component;
    }
    int value = vector_make(l, type, whole.slot, statement->range);
    reference_write(l, destination, value, type, statement->range);
}
static int vector_value(lower *l, const script_expression *e)
{
    switch (e->math_op) {
        case SCRIPT_MATH_CONSTRUCT: return vector_construct(l, e);
        case SCRIPT_MATH_SWIZZLE: {
            vector_parts source;
            const script_expression *parent = e->projection
                                                ? &l->a->expressions[e->projection - 1]
                                                : NULL;
            vector_read(l, parent ? e->projection - 1 : e->left, PIN_F32, &source);
            int parts[4];
            int count = e->swizzle_count;
            for (int k = 0; k < count; k++) {
                int component = e->swizzle[k];
                if (parent) {
                    /* Reads evaluate the logical projections and retain their
                     * debugger values. Writes use the composed physical map. */
                    int logical = 0;
                    while (logical < parent->swizzle_count &&
                           parent->swizzle[logical] != component) {
                        logical++;
                    }
                    assert(logical < parent->swizzle_count);
                    component = logical;
                }
                parts[k] = source.slot[component];
            }
            return count == 1 ? parts[0] : vector_make(l, e->type, parts, e->range);
        }
        case SCRIPT_MATH_OPERATOR: return vector_operator(l, e);
        default: return vector_function(l, e);
    }
}
