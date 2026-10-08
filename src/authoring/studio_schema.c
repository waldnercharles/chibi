#include "authoring/studio_project.h"
#include "support/resources.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
bool studio_project_is(const char *root)
{
    char studio[PATH_MAX], project[PATH_MAX];
    return realpath(resource_studio(), studio) && realpath(root, project) &&
           !strcmp(studio, project);
}
