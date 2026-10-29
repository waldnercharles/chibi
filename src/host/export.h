#ifndef CHIBI_HOST_EXPORT_H
#define CHIBI_HOST_EXPORT_H

#include <signal.h>
#include <stdbool.h>

typedef enum
{
    /* The player beside data/, and a .desktop entry for installing it */
    CHIBI_EXPORT_FOLDER,
    /* A macOS app: the player in Contents/MacOS, the data in
     * Contents/Resources with the icon as an .icns, and an Info.plist */
    CHIBI_EXPORT_APP,
    /* The player as <name>.exe beside data/, with the icon in its resources;
     * only a Windows host can add the icon */
    CHIBI_EXPORT_WINDOWS,
    /* A static site: index.html and the browser player beside data/, which
     * files.json lists for the page to fetch */
    CHIBI_EXPORT_WEB,
} chibi_export_layout;

/* `bin` holds this host's chibi-compile and chibi-player, and bin/../share/chibi holds the
 * engine resources. `output` is the folder to create, or an earlier export;
 * an app's output gains the .app extension. */
typedef struct
{
    const char *project; /* project root */
    const char *name;    /* names the executable, or NULL for "game" */
    const char *title;   /* the name people see, or NULL for `name` */
    const char *icon;    /* project-relative PNG, or NULL */
    const char *bin;
    const char *web; /* the browser player's folder, for CHIBI_EXPORT_WEB */
    const char *output;
    chibi_export_layout layout;
    bool archive; /* also write <output>.tar.gz */
    /* A command, up to NULL, that writes the game's native libraries into the
     * absent folder appended to it, the data folder's natives/; or NULL */
    const char *const *natives;
    /* Optional controlling-thread cancellation signal. Child processes are
     * owned and interrupted through this flag; NULL retains legacy waiting.
     * A child control signal may update it, as chibi_process_run documents. */
    volatile sig_atomic_t *stop_signal;
} chibi_export_spec;

/* Compiles the project and assembles a runnable game folder in `layout`: the
 * player, renamed for the game, and the data folder with the artifact, its
 * declared resources, the icon among them, engine/ under it with the shaders,
 * licenses and engine identity, and natives/ when `natives` builds any. The folder is built beside `output` and moved
 * into place only when complete, and an existing `output` is replaced only
 * when it is an earlier export. Prints the failure and returns false; when
 * only the archive fails, the folder is still in place. */
bool chibi_export(const chibi_export_spec *);
/* Deletes a folder without following symlinks; false when anything remains. */
bool chibi_remove_tree(const char *path);

#endif
