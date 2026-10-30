#pragma once

/* chibi test: loads every package below root, the project: collection, with
 * the core collection at core; both are absolute directories. Prepares them
 * for the natives that need no running game and runs each `@test` function on
 * the default backend. Pure tests run on the bounded runtime worker pool;
 * CHIBI_TEST_WORKERS=1 forces serial execution (1..32 selects a hardware-capped
 * worker budget). Call on the controlling thread, outside a pool submission.
 * 0 when every test passes, 1 for a failure, invalid package or worker setting. */
int authoring_project_test(const char *root, const char *core);
