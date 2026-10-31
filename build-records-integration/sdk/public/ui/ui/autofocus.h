#ifndef CHIBI_UI_AUTOFOCUS_H
#define CHIBI_UI_AUTOFOCUS_H

/* HTML's autofocus for elements built after their document shows, such as
 * the name field a rename puts in a list row with data-when (Vue's v-if):
 * an element created with the autofocus attribute takes focus at the end
 * of the update that built it, when it shows, and a text field's text is
 * selected. Of several, the first built does. RmlUi itself focuses only a
 * document's first one as it shows; data-if (v-show) builds nothing. */

#include "ui/rml.h"

/* Hears every element created from now on; the backend calls it once
 * RmlUi starts. */
void autofocus_register();
/* Focuses what `context`'s update built with autofocus; the backend runs
 * it after each update. */
void autofocus_settle(rml_context *context);

#endif
