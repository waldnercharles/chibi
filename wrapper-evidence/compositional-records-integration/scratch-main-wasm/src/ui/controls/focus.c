/* Tab order among a subtree's focusable elements, as RmlUi's own Tab search
 * decides what can take focus. */
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <stdlib.h>
#include <string.h>

static bool tabbable(rml_element *el)
{
    char tab[8], focus[8];
    rml_element_property(el, "tab-index", tab, sizeof tab);
    rml_element_property(el, "focus", focus, sizeof focus);
    return !strcmp(tab, "auto") && strcmp(focus, "none") != 0 &&
           !rml_element_has_attribute(el, "data-disabled") &&
           rml_element_is_visible(el, true);
}

bool focus_step(rml_element *root, rml_element *from, int step, bool wrap, rml_element *skip)
{
    int count = rml_element_query_selector_all(root, "*", NULL, 0), kept = 0, at = -1;
    rml_element **stops = xmalloc((size_t)(count + 1) * sizeof *stops);
    rml_element_query_selector_all(root, "*", stops, count);
    for (int i = 0; i < count; i++) {
        if (tabbable(stops[i]) && (!skip || !control_within(stops[i], skip))) {
            at = stops[i] == from ? kept : at;
            stops[kept++] = stops[i];
        }
    }
    int next = at < 0 ? (step > 0 ? 0 : kept - 1) : at + step;
    if (wrap && kept) {
        next = (next % kept + kept) % kept;
    }
    bool moved = next >= 0 && next < kept && rml_element_focus(stops[next], true);
    if (moved) {
        rml_element_scroll_into_view(stops[next], RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    }
    free(stops);
    return moved;
}
