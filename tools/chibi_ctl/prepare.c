#include "prepare.h"

#include "authoring/project_compile.h"

int chibi_prepare_project(const char *project, const char *scene, const char *output)
{
    return authoring_project_compile(project, scene, output);
}
