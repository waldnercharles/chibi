/* A hosted callback removes attachments on both sides of its traversal entry. */
#include "third_party/pico_headers/pico_unit.h"
#include "ui/iframe.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ATTACHMENTS 4

typedef struct attachment
{
    rml_element_ref *holder, *document;
    rml_element *slot;
    int updates;
    bool attached;
} attachment;

static struct
{
    rml_backend *backend;
    rml_context *context;
    rml_instancer *instancer;
    attachment entries[ATTACHMENTS];
    bool armed, removed;
    int visited_updates, unvisited_updates;
} fixture;

static void hosted_update(rml_element *, void *user)
{
    attachment *entry = user;
    if (!entry) {
        return;
    }
    entry->updates++;
    if (!fixture.armed || entry != &fixture.entries[1]) {
        return;
    }
    fixture.armed = false;
    fixture.visited_updates = fixture.entries[0].updates;
    fixture.unvisited_updates = fixture.entries[2].updates;
    int removed[] = { 0, 2 };
    for (int i = 0; i < 2; i++) {
        attachment *target = &fixture.entries[removed[i]];
        rml_element *document = rml_element_ref_get(target->document);
        assert(document);
        iframe_detach(document);
        target->attached = false;
        rml_document_close(document);
    }
    fixture.removed = true;
}

static const rml_element_hooks HOOKS = { .update = hosted_update };

static rml_element *load(const char *markup)
{
    rml_element *document = rml_context_load_document_from_memory(fixture.context, markup, "[iframe-detach]");
    assert(document);
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    return document;
}

static void setup()
{
    memset(&fixture, 0, sizeof fixture);
    fixture.backend = rml_backend_create(640, 480, 1);
    fixture.context = rml_backend_context(fixture.backend);
    fixture.instancer = rml_instancer_create(&HOOKS);
    rml_register_element_instancer("iframe-detach-probe", fixture.instancer);
    for (int i = 0; i < ATTACHMENTS; i++) {
        attachment *entry = &fixture.entries[i];
        char markup[512];
        snprintf(
            markup,
            sizeof markup,
            "<rml><head><style>body { margin: 0; position: absolute; "
            "left: %dpx; top: 20px; width: 140px; height: 80px; }"
            "</style></head><body><div id='slot' "
            "style='display: block; width: %dpx; height: %dpx;'/></body></rml>",
            i * 140,
            101 + i * 11,
            30 + i * 3
        );
        rml_element *holder = load(markup);
        entry->holder = rml_element_ref_create(holder);
        entry->slot = rml_element_by_id(holder, "slot");
        assert(entry->slot);
        rml_element *document = load(
            "<rml><head><style>body { margin: 0; }"
            "</style></head><body><iframe-detach-probe "
            "id='probe'/></body></rml>"
        );
        entry->document = rml_element_ref_create(document);
    }
    /* Resolve initial Context geometry before exposing the test callbacks. */
    rml_context_update(fixture.context);
    for (int i = 0; i < ATTACHMENTS; i++) {
        attachment *entry = &fixture.entries[i];
        rml_element *document = rml_element_ref_get(entry->document);
        rml_element_set_user(rml_element_by_id(document, "probe"), entry);
        iframe_attach(document, entry->slot, false);
        entry->attached = true;
    }
    fixture.armed = true;
}

static bool teardown()
{
    for (int i = 0; i < ATTACHMENTS; i++) {
        attachment *entry = &fixture.entries[i];
        rml_element *document = rml_element_ref_get(entry->document);
        if (entry->attached) {
            iframe_detach(document);
        }
        if (document) {
            rml_document_close(document);
        }
        rml_element *holder = rml_element_ref_get(entry->holder);
        if (holder) {
            rml_document_close(holder);
        }
    }
    rml_context_update(fixture.context);
    bool released = rml_context_document_count(fixture.context) == 0;
    for (int i = 0; i < ATTACHMENTS; i++) {
        attachment *entry = &fixture.entries[i];
        released &= !rml_element_ref_get(entry->holder) &&
                    !rml_element_ref_get(entry->document);
        rml_element_ref_destroy(entry->holder);
        rml_element_ref_destroy(entry->document);
    }
    rml_backend_destroy(fixture.backend);
    rml_instancer_destroy(fixture.instancer);
    return released;
}

static bool geometry(int index, int growth)
{
    attachment *entry = &fixture.entries[index];
    rml_element *document = rml_element_ref_get(entry->document);
    if (!document) {
        return false;
    }
    rml_vec2 at = rml_element_absolute_offset(document, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(document, RML_BOX_BORDER);
    return fabsf(at.x - index * 140) < 0.01f && fabsf(at.y - 20) < 0.01f &&
           fabsf(size.x - (101 + index * 11 + growth)) < 0.01f &&
           fabsf(size.y - (30 + index * 3 + growth)) < 0.01f;
}

TEST_CASE(removes_earlier_and_later_attachments)
{
    iframes_settle(fixture.context);
    REQUIRE(fixture.removed);
    REQUIRE(fixture.visited_updates == 1);
    REQUIRE(fixture.unvisited_updates == 0);
    REQUIRE(fixture.entries[0].updates == 1);
    REQUIRE(fixture.entries[1].updates == 1);
    REQUIRE(fixture.entries[2].updates == 0);
    REQUIRE(fixture.entries[3].updates == 1);
    REQUIRE(geometry(1, 0));
    REQUIRE(geometry(3, 0));
    REQUIRE(rml_context_document_count(fixture.context) == ATTACHMENTS * 2 - 2);
    /* Closed documents stay alive until Context release; removing their
     * attachments must not leave settling dependent on these observers. */
    REQUIRE(rml_element_ref_get(fixture.entries[0].document));
    REQUIRE(rml_element_ref_get(fixture.entries[2].document));
    iframes_settle(fixture.context);
    REQUIRE(fixture.entries[0].updates == 1);
    REQUIRE(fixture.entries[1].updates == 1);
    REQUIRE(fixture.entries[2].updates == 0);
    REQUIRE(fixture.entries[3].updates == 1);
    int survivors[] = { 1, 3 };
    for (int i = 0; i < 2; i++) {
        int index = survivors[i];
        char value[32];
        snprintf(value, sizeof value, "%dpx", 108 + index * 11);
        REQUIRE(rml_element_set_property(fixture.entries[index].slot, "width", value));
        snprintf(value, sizeof value, "%dpx", 37 + index * 3);
        REQUIRE(rml_element_set_property(fixture.entries[index].slot, "height", value));
    }
    iframes_settle(fixture.context);
    REQUIRE(fixture.entries[1].updates == 2);
    REQUIRE(fixture.entries[3].updates == 2);
    REQUIRE(fixture.entries[0].updates == 1);
    REQUIRE(fixture.entries[2].updates == 0);
    REQUIRE(geometry(1, 7));
    REQUIRE(geometry(3, 7));
    return true;
}

bool iframe_attachment_removal()
{
    setup();
    bool passed = removes_earlier_and_later_attachments();
    bool released = teardown();
    bool cleanup = pu_require(
        released,
        "all iframe removal documents and observers released",
        __FILE__,
        __LINE__,
        NULL
    );
    return passed && cleanup;
}
