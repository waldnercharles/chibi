#ifndef CHIBI_HOST_EXPORT_H
#define CHIBI_HOST_EXPORT_H

#include <stdbool.h>

/* `bin` holds chibi-compile and chibi-player, and bin/../share/chibi holds the
 * engine resources. `output` is the folder to create, or an earlier export. */
typedef struct
{
    const char *project; /* project root */
    const char *name;    /* names the executable */
    const char *icon;    /* project-relative PNG, or NULL */
    const char *bin;
    const char *output;
} chibi_export_spec;

/* Compiles the project and assembles a runnable game folder: the player,
 * renamed for the game, beside data/ with the artifact, its declared
 * resources and the icon, and data/engine/ with the shaders, licenses and
 * engine identity. The folder is
 * built beside `output` and moved into place only when complete, and an
 * existing `output` is replaced only when it is an earlier export. Prints the
 * failure and returns false. */
bool chibi_export(const chibi_export_spec *);

#endif
