/* <live-text value="..."/> (controls.h): a readout whose glyphs change in
 * place, sized to its text, without laying out its document. */
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <stdlib.h>
#include <string.h>

#define LIVE_TEXT 256

typedef struct live_text
{
    bool dirty;
} live_text;

static void live_update(rml_element *el, void *user)
{
    live_text *t = user;
    if (!t) {
        t = xcalloc(1, sizeof *t);
        t->dirty = true;
        rml_element_set_user(el, t);
        rml_element_set_property(el, "white-space", "nowrap");
    }
    if (!t->dirty) {
        return;
    }
    t->dirty = false;
    char text[LIVE_TEXT];
    control_string(el, "value", text, sizeof text);
    line_text_set(el, text);
}

static void live_attribute(rml_element *, void *user, const char *name)
{
    live_text *t = user;
    if (t && !strcmp(name, "value")) {
        t->dirty = true;
    }
}

static void live_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = { .release = live_release,
                                         .update = live_update,
                                         .attribute = live_attribute };

void live_text_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
    }
    rml_register_element_instancer("live-text", instancer);
}
