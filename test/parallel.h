#pragma once

#include <stdbool.h>

/* Only audited scopes opt in: every case owns its fixture state, uses pico's
 * output streams, and reaches no process-global mutation or nested scheduler.
 * Other tests remain exclusive. End is a barrier, including failure paths.
 * No scope may nest or run from one of this runner's workers. */
bool test_parallel_init();
void test_parallel_begin();
void test_parallel_end();
void test_parallel_finish();
int test_parallel_workers();
