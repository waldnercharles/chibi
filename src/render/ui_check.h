#ifndef CHIBI_RENDER_UI_CHECK_H
#define CHIBI_RENDER_UI_CHECK_H

#include <stdbool.h>
#include <stddef.h>

/* Validates a ui asset the way RmlUi reads it, as a GameUi or a scene's page
 * shows it: in a scratch RmlUi context with the ui data model, collecting
 * every warning RmlUi logs, not only the first. */

typedef enum
{
    UI_CHECK_RML,     /* the asset's markup */
    UI_CHECK_RCSS,    /* its RCSS, imports read and nesting flattened */
    UI_CHECK_WIDGETS, /* the markup with its widgets expanded, when only
                         that shows the warning */
} ui_check_part;

typedef struct ui_check_message
{
    ui_check_part part;
    int line;   /* 1-based in the part's block, or 0 when unknown */
    char *text; /* as RmlUi logged it */
} ui_check_message;

/* Checks ui asset `name` whose file text is `text`: its markup alone, so a
 * warning's line is the rml block's; its RCSS, with lines of the flattened
 * RCSS until RCSS keeps source lines; and, when it uses widgets, the
 * markup a GameUi shows, which reports only what the markup alone did not.
 * Widgets and @imports resolve in `content_root`/ui through text_file_read,
 * so unsaved text counts. Appends each warning to `messages`
 * (support/array.h). Returns false with a message in `error` when the asset
 * cannot be read or a widget cannot be expanded. Initializes RmlUi when no
 * backend has. */
bool ui_check(
    const char *content_root,
    const char *name,
    const char *text,
    ui_check_message **messages,
    char *error,
    size_t error_size
);
void ui_check_messages_free(ui_check_message *messages);

/* Appends a copy of the name of each property and shorthand RCSS takes, as
 * RmlUi registers them, to `names` (support/array.h); a ui_source_project's
 * `properties`. RmlUi must be initialized. */
void ui_check_property_names(char ***names);

#endif
