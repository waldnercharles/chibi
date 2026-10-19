#pragma once
/* Deployed offline compiler command. Uses the explicit shared application
 * capability composition and writes the artifact plus resource manifest. A
 * NULL output checks the project as it is: nothing is migrated or written. */
int authoring_project_compile(const char *project, const char *scene, const char *output);
/* chibi test: loads every package below root, the project: collection, with
 * the core collection at core; both are absolute directories. Prepares them
 * for the natives that need no running game and runs each `@test` function on
 * the default backend. 0 when every test passes, 1 for a failure or an invalid
 * package. */
int authoring_project_test(const char *root, const char *core);
