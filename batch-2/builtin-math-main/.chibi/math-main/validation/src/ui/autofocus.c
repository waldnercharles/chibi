/* Autofocus for elements built after their document shows (autofocus.h). */
#include "ui/autofocus.h"

#include "support/array.h"

#include <string.h>

/* Elements created with autofocus, in the order they were built; a
 * template stays hidden, so it never takes focus. */
static rml_element_ref **built;

static void created(rml_element *element)
{
    if (rml_element_has_attribute(element, "autofocus")) {
        apush(built, rml_element_ref_create(element));
    }
}

void autofocus_register()
{
    rml_add_element_created(created);
}

static bool text_field(rml_element *element)
{
    const char *tag = rml_element_tag_name(element);
    char type[16] = "";
    rml_element_attribute(element, "type", type, sizeof type);
    return !strcmp(tag, "textarea") ||
           (!strcmp(tag, "input") && (!type[0] || !strcmp(type, "text")));
}

/* Focuses `element` when it shows. A text field shows its focus ring, as a
 * browser's always does, and its text is selected, as a rename's is. */
static bool focus(rml_element *element)
{
    bool text = text_field(element);
    if (!rml_element_is_visible(element, true) || !rml_element_focus(element, text)) {
        return false;
    }
    if (text) {
        rml_element_select(element);
    }
    return true;
}

void autofocus_settle(rml_context *context)
{
    bool focused = false;
    for (int i = 0; i < acount(built);) {
        rml_element *element = rml_element_ref_get(built[i]);
        rml_context *owner = element ? rml_element_context(element) : NULL;
        if (owner && owner != context) {
            i++;
            continue;
        }
        focused = focused || (owner && focus(element));
        rml_element_ref_destroy(built[i]);
        adel(built, i);
    }
}
