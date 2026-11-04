/* SPDX-License-Identifier: GPL-3.0-only */
/* Link only against the object copied by NativeMain from its real
 * NativeCases.chibi -> typed.Produce -> Pack -> compile_both owner.
 * No fallback implementation or interpreter is linked here. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern int64_t Echo(int64_t value);
extern int64_t Join(bool choose, int64_t left, int64_t right);
extern int64_t Identity(int64_t value);
extern int64_t Select(bool choose, int64_t left, int64_t right);
extern int64_t MultiMerge(bool choose, bool left_flag, bool right_flag, int64_t x, int64_t y);
extern int64_t Nested(bool a, bool b, int64_t x, int64_t y);
extern int64_t Early(bool choose, int64_t x, int64_t y);
extern int64_t Both(bool choose, int64_t x, int64_t y);
extern int64_t SurviveTrue(bool choose, int64_t x, int64_t y);
extern int64_t SurviveFalse(bool choose, int64_t x, int64_t y);
extern int64_t Wrap(void);
extern bool Flag(bool value);
extern bool BoolMerge(bool choose, bool left, bool right);
extern int64_t StackMix(int64_t a, bool b, int64_t c, bool d, int64_t e, bool f,
                        int64_t g, bool h, int64_t i);
extern bool StackBool(int64_t a, bool b, int64_t c, bool d, int64_t e, bool f,
                      int64_t g, bool h);

/* Conditions execute even when NDEBUG is set. Each emitted function is called
 * directly once per check. Expected overflow values are representable constants
 * or INT64_MIN + 6; the C caller never performs overflowing signed arithmetic. */
#define CHECK(condition) do { \
    if (!(condition)) { fprintf(stderr, "native AOT check failed at line %d\n", __LINE__); return 1; } \
    ++checks; \
} while (0)

int main(void) {
    unsigned checks = 0;
    CHECK(Echo(0) == 0);
    CHECK(Echo(INT64_MIN) == INT64_MIN);
    CHECK(Echo(INT64_MAX) == INT64_MAX);
    CHECK(Join(true, 8, 20) == 9);
    CHECK(Join(false, 8, 20) == 21);
    CHECK(Join(true, INT64_MAX, INT64_MIN) == INT64_MIN);
    CHECK(Join(false, INT64_MIN, INT64_MAX) == INT64_MIN);
    CHECK(Identity(INT64_MIN) == INT64_MIN);
    CHECK(Identity(INT64_MAX) == INT64_MAX);
    CHECK(Select(true, 8, 20) == 15);
    CHECK(Select(false, 8, 20) == 27);
    CHECK(Select(true, INT64_MAX, INT64_MIN) == INT64_MIN + 6);
    CHECK(MultiMerge(false, false, true, 8, 20) == 8);
    CHECK(MultiMerge(false, true, false, 8, 20) == 36);
    CHECK(MultiMerge(true, true, false, 8, 20) == 21);
    CHECK(MultiMerge(true, false, true, 8, 20) == 57);
    CHECK(Nested(true, true, 8, 20) == 9);
    CHECK(Nested(true, false, 8, 20) == 27);
    CHECK(Nested(false, true, 8, 20) == 15);
    CHECK(Nested(false, false, 8, 20) == 20);
    CHECK(Early(true, INT64_MIN, INT64_MAX) == INT64_MIN);
    CHECK(Early(false, INT64_MIN, INT64_MAX) == INT64_MAX);
    CHECK(Both(true, INT64_MIN, INT64_MAX) == INT64_MIN);
    CHECK(Both(false, INT64_MIN, INT64_MAX) == INT64_MAX);
    CHECK(SurviveTrue(true, 8, 20) == 27);
    CHECK(SurviveTrue(false, 8, 20) == 8);
    CHECK(SurviveFalse(true, 8, 20) == 8);
    CHECK(SurviveFalse(false, 8, 20) == 27);
    CHECK(Wrap() == INT64_MIN);
    CHECK(Flag(true));
    CHECK(!Flag(false));
    CHECK(BoolMerge(true, true, false));
    CHECK(!BoolMerge(true, false, true));
    CHECK(BoolMerge(false, false, true));
    CHECK(!BoolMerge(false, true, false));
    CHECK(StackMix(2, false, 3, false, 5, false, 40, false, 80) == 40);
    CHECK(StackMix(2, true, 3, true, 5, true, 40, true, 80) == 90);
    CHECK(StackMix(2, true, 3, false, 5, true, 40, false, 80) == 47);
    CHECK(StackMix(2, false, 3, true, 5, false, 40, true, 80) == 83);
    CHECK(StackMix(2, false, 3, false, 5, false, INT64_MIN, false, INT64_MAX) == INT64_MIN);
    CHECK(StackMix(2, false, 3, false, 5, false, INT64_MIN, true, INT64_MAX) == INT64_MAX);
    CHECK(StackBool(INT64_MIN, false, INT64_MAX, false, 5, false, INT64_MIN, true));
    CHECK(!StackBool(INT64_MIN, false, INT64_MAX, true, 5, false, INT64_MAX, false));
    CHECK(StackBool(INT64_MIN, true, INT64_MAX, true, 5, false, INT64_MIN, false));
    CHECK(!StackBool(INT64_MIN, true, INT64_MAX, true, 5, true, INT64_MAX, false));
    printf("{\"aot_c_caller_checks\":%u,\"status\":\"passed\",\"input\":\"source-produced-copied-object\"}\n", checks);
    return 0;
}

#undef CHECK
