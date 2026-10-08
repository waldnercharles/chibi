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
int native_arithmetic_add_fast(void *output, const void *const *inputs);
int native_arithmetic_add_direct(void *output, const void *a, const void *b);
int native_arithmetic_multiply_fast(void *output, const void *const *inputs);
int native_arithmetic_multiply_direct(void *output, const void *value, const void *scale);
int native_arithmetic_subtract_fast(void *output, const void *const *inputs);
int native_arithmetic_subtract_direct(void *output, const void *a, const void *b);
int native_arithmetic_divide_fast(void *output, const void *const *inputs);
int native_arithmetic_divide_direct(void *output, const void *a, const void *b);
int native_arithmetic_min_fast(void *output, const void *const *inputs);
int native_arithmetic_min_direct(void *output, const void *a, const void *b);
int native_arithmetic_max_fast(void *output, const void *const *inputs);
int native_arithmetic_max_direct(void *output, const void *a, const void *b);
int native_arithmetic_modulo_fast(void *output, const void *const *inputs);
int native_arithmetic_modulo_direct(void *output, const void *a, const void *b);
int native_arithmetic_clamp_direct(void *output, const void *value, const void *low, const void *high);
#define DECLARE(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE) \
    extern const node_host_call NATIVE_ARITHMETIC_##KEY##_CALL;
NATIVE_ARITHMETIC_OPERATIONS(DECLARE)
#undef DECLARE
extern const node_access NATIVE_ARITHMETIC_ACCESS;
#endif
