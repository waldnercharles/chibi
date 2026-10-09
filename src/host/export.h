#ifndef CHIBI_HOST_EXPORT_H
#define CHIBI_HOST_EXPORT_H

#include <stdbool.h>

typedef enum
{
    /* The player beside data/, and a .desktop entry for installing it */
    CHIBI_EXPORT_FOLDER,
    /* A macOS app: the player in Contents/MacOS, the data in
     * Contents/Resources with the icon as an .icns, and an Info.plist */
    CHIBI_EXPORT_APP,
} chibi_export_layout;

/* `bin` holds chibi-compile and chibi-player, and bin/../share/chibi holds the
 * engine resources. `output` is the folder to create, or an earlier export;
 * an app's output gains the .app extension. */
typedef struct
{
    const char *project; /* project root */
    const char *name;    /* names the executable, or NULL for "game" */
    const char *title;   /* the name people see, or NULL for `name` */
    const char *icon;    /* project-relative PNG, or NULL */
    const char *bin;
    const char *output;
    chibi_export_layout layout;
    bool archive; /* also write <output>.tar.gz */
} chibi_export_spec;

/* Compiles the project and assembles a runnable game folder in `layout`: the
 * player, renamed for the game, and the data folder with the artifact, its
 * declared resources and the icon, and engine/ under it with the shaders,
 * licenses and engine identity. The folder is built beside `output` and moved
 * into place only when complete, and an existing `output` is replaced only
 * when it is an earlier export. Prints the failure and returns false; when
 * only the archive fails, the folder is still in place. */
bool chibi_export(const chibi_export_spec *);

#endif
