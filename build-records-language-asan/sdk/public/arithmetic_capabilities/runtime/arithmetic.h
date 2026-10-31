#ifndef CHIBI_NATIVE_ARITHMETIC_H
#define CHIBI_NATIVE_ARITHMETIC_H
#include "execution/host.h"
#include "runtime/arithmetic_definition.h"
pin_value native_arithmetic_literal_identity(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_add(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_multiply(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_subtract(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_divide(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_min(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_max(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_modulo(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_clamp(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_negate(const pin_value *inputs, int input_count, arena *a);
pin_value native_arithmetic_abs(const pin_value *inputs, int input_count, arena *a);
#define DECLARE(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE) \
    extern const node_host_call NATIVE_ARITHMETIC_##KEY##_CALL;
NATIVE_ARITHMETIC_OPERATIONS(DECLARE)
#undef DECLARE
extern const node_access NATIVE_ARITHMETIC_ACCESS;
#endif
