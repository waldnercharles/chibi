#ifndef CHIBI_AUTHORING_UI_CHECK_H
#define CHIBI_AUTHORING_UI_CHECK_H

#include <stdbool.h>
#include <stddef.h>

/* Validates a ui asset the way RmlUi reads it, as a Ui or a scene's page
 * shows it: in a scratch RmlUi context with the ui data model, collecting
 * every warning RmlUi logs, not only the first. */

typedef enum
{
    UI_CHECK_RML,     /* the asset's markup */
    UI_CHECK_RCSS,    /* its RCSS, with the stylesheets it imports */
    UI_CHECK_WIDGETS, /* the markup with its widgets expanded, when only
                         that shows the warning */
} ui_check_part;

/* A warning at the line that wrote what it is about: a line of the checked
 * asset's .rml or .rcss file (the part says which), or, with a `source`, of
 * an imported stylesheet's file (RCSS) or a widget's .rml file (WIDGETS). */
typedef struct ui_check_message
{
    ui_check_part part;
    char *source; /* NULL for the checked asset; a file path or widget name */
    int line;     /* 1-based, or 0 when unknown */
    char *text;   /* as RmlUi logged it, its document's line made this one */
} ui_check_message;

/* Checks ui asset `name` whose markup is `rml` and styles `rcss` (NULL for
 * none): its markup alone; its RCSS, whose warnings the RCSS's source map
 * (support/rcss.h) places in its file or an import; and, when it uses
 * widgets, the markup a Ui shows, which reports only what the markup
 * alone did not, at the data-src of the element concerned
 * (ui_document_expand). Widgets and @imports resolve in `content_root`/ui
 * through text_file_read, so unsaved text counts. Appends each warning to `messages`
 * (support/array.h). Returns false with a message in `error` when the asset
 * cannot be read or a widget cannot be expanded. Initializes RmlUi when no
 * backend has. */
bool ui_check(
    const char *content_root,
    const char *name,
    const char *rml,
    const char *rcss,
    ui_check_message **messages,
    char *error,
    size_t error_size
);
void ui_check_messages_free(ui_check_message *messages);

/* Workstream I: ui_check in `context`, a scratch context the caller keeps
 * (rml_backend_context), which a check leaves empty. ui_check makes a backend
 * for each check; destroying one releases every context's font resources
 * (rml_release_render_managers), which a host showing documents would rather
 * keep. */
typedef struct rml_context rml_context;
bool ui_check_in(
    rml_context *context,
    const char *content_root,
    const char *name,
    const char *rml,
    const char *rcss,
    ui_check_message **messages,
    char *error,
    size_t error_size
);

/* Workstream Q: a stylesheet (.rcss) checked as a page importing it reads
 * it: its @imports resolved beside it in `directory`, nesting flattened, and
 * loaded in a scratch document in `context`. Each warning is at a line of
 * the stylesheet (UI_CHECK_RCSS without a source) or, with a `source`, of
 * an imported file. Returns false with a message in `error` when it cannot
 * be expanded. */
bool ui_check_stylesheet(
    rml_context *context,
    const char *directory,
    const char *text,
    ui_check_message **messages,
    char *error,
    size_t error_size
);

/* Appends a copy of the name of each property and shorthand RCSS takes, as
 * RmlUi registers them, to `names` (support/array.h); a ui_source_project's
 * `properties`. RmlUi must be initialized. */
void ui_check_property_names(char ***names);

#endif
