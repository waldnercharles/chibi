/* Layout refresh resolves the document without repeating author callbacks. */
#include "third_party/pico_headers/pico_unit.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

bool iframe_bound_documents();
bool iframe_attachment_removal();

typedef struct probe
{
    int updates, layouts;
} probe;

static void update(rml_element *, void *user)
{
    if (user) {
        ((probe *)user)->updates++;
    }
}

static void layout(rml_element *, void *user)
{
    if (user) {
        ((probe *)user)->layouts++;
    }
}

static float property(rml_element *element, const char *name)
{
    char value[64];
    rml_element_property(element, name, value, sizeof value);
    return strtof(value, NULL);
}

static struct
{
    rml_backend *backend, *clock;
    rml_instancer *instancer;
    rml_element *document, *parent, *child;
    probe parent_calls, child_calls;
} fixture;

static void setup()
{
    static const rml_element_hooks hooks = { .update = update, .layout = layout };
    fixture.backend = rml_backend_create(320, 240, 1);
    /* A second empty context advances the shared synthetic clock without
     * updating the document under test. No rendering or wall time is involved. */
    fixture.clock = rml_backend_create(1, 1, 1);
    fixture.instancer = rml_instancer_create(&hooks);
    rml_register_element_instancer("refresh-probe", fixture.instancer);
    fixture.document = rml_context_load_document_from_memory(
        rml_backend_context(fixture.backend),
        "<rml><head><style>body { margin: 0; }"
        "refresh-probe { display: block; width: 40px; height: 20px; }"
        "#child { opacity: 0; transition: opacity 0.2s linear-in-out; }"
        "#child.visible { opacity: 1; }"
        "</style></head><body><refresh-probe id='parent'>"
        "<refresh-probe id='child'/></refresh-probe></body></rml>",
        "[iframe-refresh]"
    );
    assert(fixture.document);
    fixture.parent = rml_element_by_id(fixture.document, "parent");
    fixture.child = rml_element_by_id(fixture.document, "child");
    fixture.parent_calls = (probe){ 0 };
    fixture.child_calls = (probe){ 0 };
    rml_element_set_user(fixture.parent, &fixture.parent_calls);
    rml_element_set_user(fixture.child, &fixture.child_calls);
    rml_document_show(fixture.document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_context_update(rml_backend_context(fixture.backend));
    /* Loading/showing may update a document. Count only the explicit normal
     * update below, after the initial style and layout have settled. */
    fixture.parent_calls = (probe){ 0 };
    fixture.child_calls = (probe){ 0 };
    rml_context_update(rml_backend_context(fixture.backend));
}

static void teardown()
{
    rml_document_close(fixture.document);
    rml_context_update(rml_backend_context(fixture.backend));
    rml_backend_destroy(fixture.clock);
    rml_backend_destroy(fixture.backend);
    rml_instancer_destroy(fixture.instancer);
}

TEST_CASE(refresh_preserves_layout_and_animation)
{
    REQUIRE(fixture.parent_calls.updates == 1);
    REQUIRE(fixture.child_calls.updates == 1);
    int parent_layouts = fixture.parent_calls.layouts;
    int child_layouts = fixture.child_calls.layouts;
    REQUIRE(rml_element_set_property(fixture.parent, "width", "120px"));
    REQUIRE(rml_element_set_property(fixture.child, "height", "35px"));
    REQUIRE(fabsf(property(fixture.child, "opacity")) < 0.01f);
    rml_element_set_class(fixture.child, "visible", true);
    rml_document_refresh_layout(fixture.document);
    REQUIRE(fixture.parent_calls.updates == 1);
    REQUIRE(fixture.child_calls.updates == 1);
    REQUIRE(fixture.parent_calls.layouts > parent_layouts);
    REQUIRE(fixture.child_calls.layouts > child_layouts);
    REQUIRE(fabsf(rml_element_box_size(fixture.parent, RML_BOX_BORDER).x - 120) < 0.01f);
    REQUIRE(fabsf(rml_element_box_size(fixture.child, RML_BOX_BORDER).y - 35) < 0.01f);
    REQUIRE(fabsf(property(fixture.child, "opacity")) < 0.01f);
    rml_backend_update(fixture.clock, 0.05);
    rml_document_refresh_layout(fixture.document);
    float opacity = property(fixture.child, "opacity");
    REQUIRE(opacity > 0 && opacity < 1);
    REQUIRE(fixture.parent_calls.updates == 1);
    REQUIRE(fixture.child_calls.updates == 1);
    /* Advance frame-sized steps: RmlUi clamps a single animation delta. */
    for (int i = 0; i < 4; i++) {
        rml_backend_update(fixture.clock, 0.05);
        rml_document_refresh_layout(fixture.document);
        float next = property(fixture.child, "opacity");
        REQUIRE(next >= opacity);
        opacity = next;
    }
    REQUIRE(fabsf(property(fixture.child, "opacity") - 1) < 0.01f);
    REQUIRE(fixture.parent_calls.updates == 1);
    REQUIRE(fixture.child_calls.updates == 1);
    rml_context_update(rml_backend_context(fixture.backend));
    REQUIRE(fixture.parent_calls.updates == 2);
    REQUIRE(fixture.child_calls.updates == 2);
    rml_backend_stats stats = rml_backend_get_stats(fixture.backend);
    REQUIRE(stats.render_captures == 0);
    REQUIRE(stats.render_passes == 0);
    return true;
}

void test_iframe_contracts()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(refresh_preserves_layout_and_animation);
    pu_clear_setup();
    RUN_TEST_CASE(iframe_bound_documents);
    RUN_TEST_CASE(iframe_attachment_removal);
}
