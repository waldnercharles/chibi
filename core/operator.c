#include "operator.h"

#include "array.h"

#include <string.h>

static operator_signature *g_signatures;

static int rank(pin_type type)
{
    switch (type) {
        case PIN_U8: return 1;
        case PIN_I32: return 2;
        case PIN_I64: return 3;
        case PIN_F32:
        case PIN_F64: return 4;
        default: return 0;
    }
}

int operator_register(const operator_signature *signature)
{
    if (!signature || !signature->name || !signature->name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(g_signatures); i++) {
        if (strcmp(g_signatures[i].name, signature->name) == 0 &&
            g_signatures[i].left == signature->left &&
            g_signatures[i].right == signature->right) {
            return -1;
        }
    }
    apush(g_signatures, *signature);
    return 0;
}

const operator_signature *operator_resolve(const char *name, pin_type left, pin_type right)
{
    const operator_signature *best = NULL;
    int best_cost = 1000;
    for (int i = 0; i < acount(g_signatures); i++) {
        const operator_signature *candidate = &g_signatures[i];
        if (strcmp(candidate->name, name) != 0) {
            continue;
        }
        int left_cost = candidate->left == left ? 0 : rank(candidate->left) - rank(left);
        int right_cost = candidate->right == right
                           ? 0
                           : rank(candidate->right) - rank(right);
        if (left_cost < 0 || right_cost < 0) {
            continue;
        }
        int cost = left_cost + right_cost;
        if (cost < best_cost) {
            best = candidate;
            best_cost = cost;
        } else if (cost == best_cost) {
            return NULL; /* CMPL-19: ambiguous best match */
        }
    }
    return best;
}

const operator_signature *operator_signatures(int *count)
{
    if (count) {
        *count = acount(g_signatures);
    }
    return g_signatures;
}
