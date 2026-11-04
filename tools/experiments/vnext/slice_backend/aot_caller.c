/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern int64_t slice_echo(int64_t value);
extern int64_t slice_join(bool choose, int64_t left, int64_t right);

int main(void) {
    if (slice_echo(0) != 0) return 1;
    if (slice_echo(INT64_MIN) != INT64_MIN) return 2;
    if (slice_echo(INT64_MAX) != INT64_MAX) return 3;
    if (slice_join(true, 8, 20) != 9) return 4;
    if (slice_join(false, 8, 20) != 21) return 5;
    if (slice_join(true, INT64_MAX, 0) != INT64_MIN) return 6;
    puts("{\"aot_c_caller_checks\":6,\"status\":\"passed\",\"chibi_frontend\":\"not_run\"}");
    return 0;
}
