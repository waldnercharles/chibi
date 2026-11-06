#ifndef CHIBI_AUTOMATION_UI_INSPECT_H
#define CHIBI_AUTOMATION_UI_INSPECT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* Native UI inspection and validation over the shared language service
 * (assets/ui_source.h). None does IO but what reading the project's
 * widgets and stylesheets under `content_root` needs. */

/* Validation for ui asset `name`, whose markup is `rml` and styles
 * `rcss` (NULL for none): the syntax errors tree-sitter finds, then, when
 * there are none, what ui_document_read rejects and every warning RmlUi logs
 * loading the asset with its widgets and imports (authoring/ui_check.h). Each
 * names its file by `path` from the project root, with its line when it has
 * one. Appends each to `diagnostics` and returns how many there are. */
int automation_ui_validate(
    const char *content_root,
    const char *name,
    const char *rml,
    const char *rcss,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
);

/* inspectUi for ui asset `name`: its markup's outline and its styles'
 * rules, each at a line of its file; the props its bindings read, the events
 * it emits, its slots, the widgets it uses and the props it passes them; the
 * custom properties, classes and save keys it names; and its syntax
 * errors. */
void automation_ui_inspect(
    const char *name,
    const char *rml,
    const char *rcss,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result
);

/* Workstream Q: inspectUi's `complete`, {line, column, file}, 1-based, in
 * the asset's markup or, with file "rcss", its styles: what Studio's Text
 * Editor offers there (ui_source_complete) from the project's ui files under
 * `content_root` and the properties RmlUi registers. Adds `completion` to
 * `result`: the position `at`, `from` where each item's label replaces the
 * word typed so far, the `items` (label and kind) and their `total`. Returns
 * CHIBI_AUTOMATION_OK, or another status with a message in `error`. */
int automation_ui_complete(
    const char *content_root,
    const char *rml,
    const char *rcss,
    yyjson_val *at,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
);

/* Whether a findReferences request names what ui assets and stylesheets
 * name: a class ("ui_class"), a custom property ("token", with its --) or a
 * save key ("save_key"). */
bool automation_ui_references_requested(yyjson_val *request);
/* Their definitions (a selector styling the class, a declaration of the
 * property) and uses across the ui assets and stylesheets under
 * `content_root`, each at its content/ path, line and column. */
int automation_ui_references(
    const char *content_root,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
);

#endif
