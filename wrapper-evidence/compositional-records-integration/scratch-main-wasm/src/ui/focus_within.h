#ifndef CHIBI_UI_FOCUS_WITHIN_H
#define CHIBI_UI_FOCUS_WITHIN_H

/* CSS's :focus-within for every RmlUi context, which RmlUi lacks: the
 * focused element and each of its ancestors carry the focus-within pseudo
 * class, so `.field.search:focus-within` marks a field whose input has the
 * focus, as browsers do. RmlUi sends focus and blur to each element entering
 * or leaving the focus chain, so each event marks only its target. */

/* Listens in every context RmlUi creates from now on; the backend calls it
 * once RmlUi starts. */
void ui_focus_within_register();

#endif
