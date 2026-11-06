#include "language/constant_proof.h"

#include "support/array.h"

/* A compile-time value cannot read invocation or mutable storage. Calls are
 * excluded: being pure does not by itself promise compile-time evaluation. */
static bool compile_time_value(
    const script_analysis *analysis,
    constant_proof *proofs,
    unsigned generation,
    int index,
    int depth,
    int *steps,
    bool scalar
)
{
    if (index < 0 || depth >= SCRIPT_MAX_DEPTH) {
        return false;
    }
    /* A proof made with less remaining depth also holds at a shallower visit.
     * This retains the depth rule without expanding shared constant DAGs. */
    constant_proof *proof = &proofs[index];
    if (proof->generation == generation && proof->depth >= depth) {
        return true;
    }
    if (steps && --*steps < 0) {
        return false;
    }
    const script_expression *value = &analysis->expressions[index];
    /* Reading an element is no effect here: the kinds below admit only
     * constant operands, so the element read is of a constant. */
    if (value->effects & ~SCRIPT_EFFECT_READ) {
        return false;
    }
    if (scalar &&
        (value->type.container != PIN_CONTAINER_SINGLE ||
         (value->type.flags & ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL)) ||
         value->type.storage < PIN_BOOL || value->type.storage > PIN_STR ||
         (value->kind != SCRIPT_EXPR_CONSTANT &&
          value->kind != SCRIPT_EXPR_SYMBOL && value->kind != SCRIPT_EXPR_UNARY &&
          value->kind != SCRIPT_EXPR_BINARY && value->kind != SCRIPT_EXPR_CAST))) {
        return false;
    }
    bool valid = false;
    switch (value->kind) {
        case SCRIPT_EXPR_CONSTANT:
        case SCRIPT_EXPR_NIL: valid = true; break;
        case SCRIPT_EXPR_SYMBOL:
            valid = value->symbol >= 0 &&
                    analysis->symbols[value->symbol].kind == SCRIPT_CONSTANT &&
                    compile_time_value(
                        analysis,
                        proofs,
                        generation,
                        analysis->symbols[value->symbol].initializer,
                        depth + 1,
                        steps,
                        scalar
                    );
            break;
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_CAST:
        case SCRIPT_EXPR_MEMBER:
            valid = compile_time_value(analysis, proofs, generation, value->left, depth + 1, steps, scalar);
            break;
        case SCRIPT_EXPR_BINARY:
        case SCRIPT_EXPR_INDEX:
            valid = compile_time_value(analysis, proofs, generation, value->left, depth + 1, steps, scalar) &&
                    compile_time_value(analysis, proofs, generation, value->right, depth + 1, steps, scalar);
            break;
        case SCRIPT_EXPR_MATH:
            valid = (value->left < 0 ||
                     compile_time_value(analysis, proofs, generation, value->left, depth + 1, steps, false)) &&
                    (value->right < 0 ||
                     compile_time_value(analysis, proofs, generation, value->right, depth + 1, steps, false));
            for (int i = 0; valid && i < value->argument_count; i++) {
                valid = compile_time_value(
                    analysis,
                    proofs,
                    generation,
                    value->arguments[i].value,
                    depth + 1,
                    steps,
                    false
                );
            }
            break;
        case SCRIPT_EXPR_ARRAY:
        case SCRIPT_EXPR_MAP:
        case SCRIPT_EXPR_STRUCT:
        case SCRIPT_EXPR_UNION:
            valid = true;
            for (int i = 0; i < acount(value->arguments); i++) {
                if (!compile_time_value(
                        analysis,
                        proofs,
                        generation,
                        value->arguments[i].value,
                        depth + 1,
                        steps,
                        scalar
                    )) {
                    valid = false;
                    break;
                }
            }
            break;
        default: break;
    }
    if (valid) {
        *proof = (constant_proof){ generation, depth };
    }
    return valid;
}

bool script_constant_prove(
    const script_analysis *analysis,
    constant_proof *proofs,
    unsigned generation,
    int expression,
    int *steps,
    bool scalar
)
{
    return compile_time_value(analysis, proofs, generation, expression, 0, steps, scalar);
}
