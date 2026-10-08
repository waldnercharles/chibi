#pragma once
/* Deployed offline compiler command. Uses the explicit shared application
 * capability composition and writes the artifact plus resource manifest. */
int authoring_project_compile(const char *project, const char *scene, const char *output);
