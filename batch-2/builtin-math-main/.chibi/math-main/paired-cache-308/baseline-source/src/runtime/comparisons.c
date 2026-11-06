#include "runtime/comparisons.h"

#include "types/array.h"

#include <assert.h>
#define REAL(NAME, LABEL, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)    \
    pin_value native_compare_##NAME(const pin_value *inputs, int count, arena *)  \
    {                                                                             \
        assert(count == 2);                                                       \
        (void)count;                                                              \
        if (EQUALITY && inputs[0].type != PIN_F32 && inputs[0].type != PIN_F64) { \
            assert(inputs[0].type == inputs[1].type);                             \
            bool equal = pin_value_equal(&inputs[0], &inputs[1]);                 \
            return (pin_value){ .type = PIN_BOOL, .b = NEGATE ? !equal : equal }; \
        }                                                                         \
        assert(                                                                   \
            (inputs[0].type == PIN_F32 || inputs[0].type == PIN_F64) &&           \
            (inputs[1].type == PIN_F32 || inputs[1].type == PIN_F64)              \
        );                                                                        \
        assert(inputs[0].container == PIN_CONTAINER_SINGLE);                      \
        assert(inputs[1].container == PIN_CONTAINER_SINGLE);                      \
        double left = inputs[0].type == PIN_F64 ? inputs[0].d : inputs[0].f;      \
        double right = inputs[1].type == PIN_F64 ? inputs[1].d : inputs[1].f;     \
        return (pin_value){ .type = PIN_BOOL, .b = left OP right };               \
    }
NATIVE_COMPARE_REALS(REAL)
#undef REAL
#define INTEGER(NAME, LABEL, SOURCE, OP, KEYWORDS, DESCRIPTION)                  \
    pin_value native_compare_##NAME(const pin_value *inputs, int count, arena *) \
    {                                                                            \
        assert(count == 2);                                                      \
        (void)count;                                                             \
        assert(inputs[0].type == PIN_I32 && inputs[1].type == PIN_I32);          \
        assert(inputs[0].container == PIN_CONTAINER_SINGLE);                     \
        assert(inputs[1].container == PIN_CONTAINER_SINGLE);                     \
        return (pin_value){ .type = PIN_BOOL, .b = inputs[0].i OP inputs[1].i }; \
    }
NATIVE_COMPARE_INTEGERS(INTEGER)
#undef INTEGER
pin_value native_compare_Has_Any_Flags(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    (void)count;
    assert(inputs[0].type == PIN_U32 && inputs[1].type == PIN_U32);
    assert(inputs[0].container == PIN_CONTAINER_SINGLE && inputs[1].container == PIN_CONTAINER_SINGLE);
    return (pin_value){ .type = PIN_BOOL, .b = (inputs[0].u & inputs[1].u) != 0 };
}
pin_value native_compare_And(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_BOOL && inputs[1].type == PIN_BOOL);
    (void)count;
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].b && inputs[1].b };
}
pin_value native_compare_Or(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_BOOL && inputs[1].type == PIN_BOOL);
    (void)count;
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].b || inputs[1].b };
}
pin_value native_compare_Not(const pin_value *inputs, int count, arena *)
{
    assert(count == 1 && inputs[0].type == PIN_BOOL);
    (void)count;
    return (pin_value){ .type = PIN_BOOL, .b = !inputs[0].b };
}
#define ADAPTER(KEY)                                                                                        \
    static const char *KEY##_call(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                       \
        pin_value value = native_compare_##KEY(arguments, count, storage);                                  \
        if (value.type == PIN_FLOW ||                                                                       \
            (value.type == PIN_STRUCT &&                                                                    \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr))                                        \
            return "Native value operation failed";                                                         \
        return pin_value_snapshot_to_arena(storage, result, &value)                                         \
                 ? "Cannot retain native value result"                                                      \
                 : NULL;                                                                                    \
    }                                                                                                       \
    const node_host_call NATIVE_COMPARE_##KEY##_CALL = { .value = KEY##_call };
#define REAL(KEY, NAME, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)   \
    ADAPTER(KEY)
#define INTEGER(KEY, NAME, SOURCE, OP, KEYWORDS, DESCRIPTION) ADAPTER(KEY)
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    ADAPTER(KEY)
NATIVE_COMPARE_REALS(REAL)
NATIVE_COMPARE_INTEGERS(INTEGER)
NATIVE_COMPARE_VALUES(VALUE)
const node_access NATIVE_COMPARE_ACCESS = { .flags = NODE_EFFECT_PURE };
