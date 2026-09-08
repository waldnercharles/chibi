#include <blueprints/application.h>

/* A valid shared library without the application lifecycle entry point. */
BP_APPLICATION_EXPORT int unrelated_entry()
{
    return 42;
}
