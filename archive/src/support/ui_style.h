#ifndef CHIBI_UI_STYLE_H
#define CHIBI_UI_STYLE_H

#include "support/rcss_map.h"

#include <stdbool.h>
#include <stddef.h>

/* Where the rules of the stylesheets installed in RmlUi were written, as a
 * browser's source maps tell its inspector.
 *
 * A host installs each stylesheet under a name
 * (rml_document_set_named_style_sheet), and RmlUi reports the rule that set
 * a property as a PropertySource: that name as its path and the rule's
 * 1-based line in the installed text (rml_element_property_source). The host
 * records the sheet here with the line map it joined (rcss_join), and
 * ui_style_origin maps a reported line back to the file and line it came
 * from.
 *
 * The hosts and their sheet names:
 *
 *   render_ui   "game-ui-<n>", one per host: its base style, its page's RCSS
 *               and each shown ui asset's scoped RCSS
 *   kit         "kit-<document>", its page theme and resource stylesheets;
 *               "kit-<document>-<key>" for each kit_style_sheet
 *
 * Code that makes RCSS for another host to install records it under a name
 * of its own, so the host finds its lines by text (ui_style_map): each
 * ui_mount records its scoped RCSS as "mount <asset path>", and each ui_page
 * its RCSS as "page <asset path>".
 *
 * A path is an imported stylesheet's, or a ui asset's .rcss file for its
 * own lines, as data-src names its .rml file's (ui_document_expand). One
 * registry serves the process; use it from the thread that runs the UI. */

/* Records `rcss` as sheet `sheet`'s text, with `map` tracing its lines (NULL
 * for none), replacing what the sheet held; a NULL rcss forgets the sheet.
 * Copies both. */
void ui_style_set(const char *sheet, const char *rcss, const rcss_map *map);

/* The map recorded with text equal to `rcss`, or NULL, for a host handed
 * RCSS text, such as a kit's theme. It lasts until the next ui_style_set. */
const rcss_map *ui_style_map(const char *rcss);

/* The file and 1-based line that line `line` of sheet `sheet` came from;
 * false when the sheet is unknown or the line came from no file, such as a
 * host's built-in style. */
bool ui_style_origin(const char *sheet, int line, char *path, size_t size, int *source_line);

#endif
