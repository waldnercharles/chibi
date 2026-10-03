/* :focus-within for every context (focus_within.h). */
#include "ui/focus_within.h"

#include "ui/rml.h"

static rml_listener *listener;

static void on_event(void *, const rml_event *e)
{
    rml_element_set_pseudo_class(e->target, "focus-within", e->id == RML_EVENT_FOCUS);
}

/* Its root hears every focus and blur in the context while capturing. */
static void context_created(rml_context *context)
{
    rml_element *root = rml_context_root(context);
    rml_element_add_event_listener(root, RML_EVENT_FOCUS, listener, true);
    rml_element_add_event_listener(root, RML_EVENT_BLUR, listener, true);
}

static const rml_context_hooks CONTEXT_HOOKS = { .create = context_created };

void ui_focus_within_register()
{
    if (!listener) {
        listener = rml_listener_create(on_event, NULL);
    }
    rml_add_context_hooks(&CONTEXT_HOOKS);
}
