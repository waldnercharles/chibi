#ifndef CHIBI_PROJECT_MIGRATE32_H
#define CHIBI_PROJECT_MIGRATE32_H

#include <stddef.h>

/* Version 33 writes no type arguments in angle brackets:
 *
 * - `struct<T>` and `enum<T>` name `T`, `array<T>` is `T[]`, `task<T>` is
 *   `task(T)` and `handle<ecs.entity>` is `entity`.
 * - `convert<f>(x)` calls `f(x)`.
 * - `get_variable<X>()` reads the state `X`, and a statement
 *   `set_variable<X>(v)` assigns `X = v`. An action that only sets a state is
 *   inlined: each `N()` becomes the assignment followed by the body of
 *   `after N`, which reads `N.Value` as the state.
 * - `send_event<E>(target, ...)` is `emit target.E(...)`.
 *
 * Sets `migrated` to an owned rewrite, or NULL when nothing changes. Returns
 * -1 with `error` set for an angle form version 33 cannot spell. */
int chibi_migrate32_angle_forms(const char *source, char **migrated, char *error, size_t size);

#endif
