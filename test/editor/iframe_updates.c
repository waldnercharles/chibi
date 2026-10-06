/* Geometry changed by a later document's callback must settle this frame. */
#include "test/suite_internal.h"
#include "ui/iframe.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct iframe_fixture
{
    rml_backend *backend;
    rml_context *context;
    rml_element *holder, *hosted, *fitted, *later;
    rml_element *slot, *fit_slot, *content, *sibling;
    int holder_updates, later_updates, changes;
    int width, height;
    bool armed, visited, close_fitted, reattach, reattached;
} iframe_fixture;

static void holder_update(rml_element *, void *user)
{
    iframe_fixture *f = user;
    if (f) {
        f->holder_updates++;
    }
}

static void pixels(rml_element *element, const char *property, int value)
{
    char text[32];
    snprintf(text, sizeof text, "%dpx", value);
    bool set = rml_element_set_property(element, property, text);
    assert(set);
    (void)set;
}

static void later_update(rml_element *, void *user)
{
    iframe_fixture *f = user;
    if (!f) {
        return;
    }
    f->later_updates++;
    if (f->close_fitted) {
        f->close_fitted = false;
        rml_document_close(f->fitted);
        return;
    }
    if (!f->armed) {
        return;
    }
    f->armed = false;
    f->visited = f->holder_updates > 0;
    f->changes++;
    pixels(f->slot, "width", f->width);
    pixels(f->content, "height", f->height);
}

static const rml_element_hooks HOLDER = { .update = holder_update };
static const rml_element_hooks LATER = { .update = later_update };

static void fitted_update(rml_element *element, void *user)
{
    iframe_fixture *f = user;
    if (!f || !f->reattach) {
        return;
    }
    f->reattach = false;
    iframe_detach(f->fitted);
    pixels(element, "height", 45);
    iframe_attach(f->fitted, f->fit_slot, true);
    f->reattached = true;
}

static const rml_element_hooks FITTED = { .update = fitted_update };

static rml_element *load(iframe_fixture *f, const char *markup)
{
    rml_element *document = rml_context_load_document_from_memory(f->context, markup, "[iframe-updates]");
    assert(document);
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    return document;
}

static void frame(iframe_fixture *f)
{
    f->holder_updates = 0;
    f->later_updates = 0;
    rml_backend_update(f->backend, 1.0 / 60.0);
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static bool geometry(iframe_fixture *f, int width, int height)
{
    rml_vec2 hosted = rml_element_box_size(f->hosted, RML_BOX_BORDER);
    rml_vec2 fitted = rml_element_box_size(f->fitted, RML_BOX_BORDER);
    rml_vec2 fit_slot = rml_element_box_size(f->fit_slot, RML_BOX_BORDER);
    rml_vec2 following = rml_element_absolute_offset(f->sibling, RML_BOX_BORDER);
    rml_element *hit = rml_context_element_at_point(f->context, (rml_vec2){ width - 5, 25 }, NULL);
    rml_element *fit_hit = rml_context_element_at_point(
        f->context,
        (rml_vec2){ 10, 50 + height - 5 },
        NULL
    );
    return near(hosted.x, width) && near(hosted.y, 50) && near(fitted.y, height) &&
           near(fit_slot.y, height) && near(following.y, 50 + height) && hit &&
           rml_element_owner_document(hit) == f->hosted && fit_hit &&
           rml_element_owner_document(fit_hit) == f->fitted;
}

void test_iframe_updates()
{
    iframe_fixture f = { 0 };
    f.backend = rml_backend_create(640, 480, 1);
    f.context = rml_backend_context(f.backend);
    rml_instancer *holder_instancer = rml_instancer_create(&HOLDER);
    rml_instancer *later_instancer = rml_instancer_create(&LATER);
    rml_instancer *fitted_instancer = rml_instancer_create(&FITTED);
    rml_register_element_instancer("iframe-holder-probe", holder_instancer);
    rml_register_element_instancer("iframe-later-probe", later_instancer);
    rml_register_element_instancer("iframe-fitted-probe", fitted_instancer);
    f.holder = load(
        &f,
        "<rml><head><style>body { margin: 0; width: 640px; height: 480px; }"
        "div { display: block; }"
        "iframe-holder-probe { position: absolute; width: 0; height: 0; }"
        "</style></head><body><iframe-holder-probe id='probe'/>"
        "<div id='slot' "
        "style='width: 100px; height: 50px;'/><div id='fit' "
        "style='width: 100px; height: 0px;'/><div id='following' "
        "style='width: 100px; height: 20px;'/></body></rml>"
    );
    f.hosted = load(
        &f,
        "<rml><head><style>body { margin: 0; } div { display: block; }"
        "</style></head><body><div style='width: 100%; height: "
        "50px;'/></body></rml>"
    );
    f.fitted = load(
        &f,
        "<rml><head><style>body { margin: 0; } iframe-fitted-probe { display: "
        "block; }"
        "</style></head><body><iframe-fitted-probe id='content' "
        "style='width: 100%; height: 30px;'/></body></rml>"
    );
    f.later = load(
        &f,
        "<rml><head><style>body { margin: 0; left: 500px; width: 1px; "
        "height: 1px; } iframe-later-probe { position: absolute; "
        "width: 0; height: 0; }</style></head><body>"
        "<iframe-later-probe id='probe'/></body></rml>"
    );
    f.slot = rml_element_query_selector(f.holder, "#slot");
    f.fit_slot = rml_element_query_selector(f.holder, "#fit");
    f.sibling = rml_element_query_selector(f.holder, "#following");
    f.content = rml_element_query_selector(f.fitted, "#content");
    assert(f.slot && f.fit_slot && f.sibling && f.content);
    rml_element_set_user(rml_element_query_selector(f.holder, "#probe"), &f);
    rml_element_set_user(rml_element_query_selector(f.later, "#probe"), &f);
    rml_element_set_user(f.content, &f);
    iframe_attach(f.hosted, f.slot, false);
    iframe_attach(f.fitted, f.fit_slot, true);
    frame(&f);
    frame(&f);
    test_check(geometry(&f, 100, 30), "iframe fixture initially settles boxes and fitted height");
    frame(&f);
    test_check(
        f.holder_updates > 0 && f.later_updates > 0 && geometry(&f, 100, 30),
        "stable iframe geometry preserves document update callbacks"
    );
    f.armed = true;
    f.width = 160;
    f.height = 70;
    frame(&f);
    test_check(
        f.visited && f.changes == 1 && geometry(&f, 160, 70),
        "a later callback resizes an already visited holder and fitted content "
        "with sibling placement and hit testing in the same frame"
    );
    /* Start the next mutation from settled geometry even if the first check fails. */
    frame(&f);
    f.armed = true;
    f.width = 120;
    f.height = 20;
    frame(&f);
    test_check(f.visited && f.changes == 2 && geometry(&f, 120, 20), "rearming a later iframe callback also settles shrinking geometry this frame");
    frame(&f);
    /* Resize after Context traversal so take_box calls the fitted hook while
     * the settling traversal holds its attachment. */
    pixels(f.fit_slot, "width", 160);
    f.reattach = true;
    iframes_settle(f.context);
    test_check(f.reattached && geometry(&f, 120, 45), "a fitted callback can detach and reattach itself during bounded settling");
    rml_element_ref *fitted_ref = rml_element_ref_create(f.fitted);
    f.close_fitted = true;
    frame(&f);
    test_check(!rml_element_ref_get(fitted_ref) && rml_context_document_count(f.context) == 3, "a later callback can close a fitted document before post-context settling");
    rml_element_ref_destroy(fitted_ref);
    iframe_detach(f.hosted);
    rml_document_close(f.hosted);
    rml_document_close(f.holder);
    rml_document_close(f.later);
    frame(&f);
    test_check(rml_context_document_count(f.context) == 0, "iframe update fixture releases every document");
    rml_backend_destroy(f.backend);
    rml_instancer_destroy(later_instancer);
    rml_instancer_destroy(fitted_instancer);
    rml_instancer_destroy(holder_instancer);
}
