/* Opening a blank kit document must not evaluate unrelated model bindings. */
#include "editor/ui/kit.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EMPTY_PROFILE_FRAMES 1000

static double now()
{
    struct timespec time;
    int result = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(result == 0);
    (void)result;
    return time.tv_sec + time.tv_nsec * 1e-9;
}

/* Same document replacement and dirty-model work in every recorded frame. */
static void profile_empty_documents(rml_backend *backend, kit *k, rml_data_model *model, yyjson_mut_val *label)
{
    const char *profile = getenv("CHIBI_IFRAME_PROFILE");
    if (!profile || strcmp(profile, "1")) {
        return;
    }
    rml_context *context = rml_backend_context(backend);
    rml_backend_stats before = rml_backend_get_stats(backend);
    double creation_seconds = 0, frame_seconds = 0;
    uint64_t creation_reads = 0, creation_lookups = 0, frame_reads = 0;
    int opens = 0;
    for (int i = 0; i < EMPTY_PROFILE_FRAMES; i++) {
        double start = now();
        kit_frame_input input = {
            .width = 640,
            .height = 480,
            .dp_ratio = 1,
            .time = i / 60.0,
        };
        kit_begin(k, &input);
        kit_root(k, "app");
        yyjson_mut_set_str(label, i & 1 ? "Odd" : "Even");
        rml_data_model_dirty(model, "label");
        rml_data_reads reads = rml_data_model_reads(model);
        char key[32];
        snprintf(key, sizeof key, "blank-%d", i);
        int documents = rml_context_document_count(context);
        double creation_start = now();
        kit_document_begin(k, key, NULL, false);
        creation_seconds += now() - creation_start;
        rml_data_reads created = rml_data_model_reads(model);
        creation_reads += created.values - reads.values;
        creation_lookups += created.lookups - reads.lookups;
        opens += rml_context_document_count(context) - documents;
        kit_host(k, "marker", "empty-copy-marker");
        kit_document_end(k);
        kit_end(k);
        rml_backend_update(backend, 1.0 / 60.0);
        frame_seconds += now() - start;
        frame_reads += rml_data_model_reads(model).values - reads.values;
    }
    rml_backend_stats after = rml_backend_get_stats(backend);
    printf(
        "EMPTY_DOC_PROFILE {\"frames\":%d,\"document_opens\":%d,"
        "\"creation_seconds\":%.9f,\"frame_seconds\":%.9f,"
        "\"creation_model_reads\":%llu,\"creation_model_lookups\":%llu,"
        "\"frame_model_reads\":%llu,\"context_updates\":%llu,"
        "\"render_captures\":%llu,\"render_passes\":%llu,"
        "\"geometry_compiles\":%llu}\n",
        EMPTY_PROFILE_FRAMES,
        opens,
        creation_seconds,
        frame_seconds,
        (unsigned long long)creation_reads,
        (unsigned long long)creation_lookups,
        (unsigned long long)frame_reads,
        (unsigned long long)(after.context_updates - before.context_updates),
        (unsigned long long)(after.render_captures - before.render_captures),
        (unsigned long long)(after.render_passes - before.render_passes),
        (unsigned long long)(after.geometry_compiles - before.geometry_compiles)
    );
}

static bool title_is(rml_element *element, const char *expected)
{
    char title[32];
    rml_element_attribute(element, "title", title, sizeof title);
    return !strcmp(title, expected);
}

static uintptr_t load_texture(void *user, const char *source, int *width, int *height)
{
    snprintf(user, 128, "%s", source);
    *width = *height = 8;
    return 1;
}

static void release_texture(void *, uintptr_t) {}

static void relative_image_source()
{
    char source[128] = "";
    static const rml_render_interface hooks = {
        .load_texture = load_texture,
        .release_texture = release_texture,
    };
    rml_renderer *renderer = rml_renderer_create(&hooks, source);
    rml_context *context = rml_create_context("blank-document-source", 100, 100, renderer);
    rml_element *document = rml_context_create_document(context, "/kit-authored/panes/host.rml");
    assert(document);
    rml_element_set_inner_rml(document, "<img src='images/icon.png'/>");
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_context_update(context);
    test_check(!strcmp(source, "/kit-authored/panes/images/icon.png"), "an empty authored document resolves relative image sources against its document URL");
    rml_document_close(document);
    rml_context_update(context);
    rml_remove_context("blank-document-source");
    rml_release_render_managers();
    rml_renderer_destroy(renderer);
}

void test_kit_empty_documents()
{
    rml_backend *backend = rml_backend_create(640, 480, 1);
    rml_context *context = rml_backend_context(backend);
    kit *k = test_kit_create(context, "studio.rml");
    kit_theme(k, ".empty-copy-marker { display: block; width: 137px; height: 19px; }");
    kit_set_body_class(k, "empty-copy-class", true);
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(json);
    yyjson_mut_doc_set_root(json, root);
    yyjson_mut_val *label = yyjson_mut_str(json, "Before");
    yyjson_mut_obj_add_val(json, root, "label", label);
    rml_data_model *model = rml_data_model_create(context, "empty_document_probe", json, NULL, NULL);
    rml_element *bound = rml_context_load_document_from_memory(
        context,
        "<rml><body data-model='empty_document_probe'>"
        "<div id='bound' data-attr-title='label'/></body></rml>",
        "[empty-document-model]"
    );
    assert(bound);
    rml_document_show(bound, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_element *value = rml_element_by_id(bound, "bound");
    assert(value);
    rml_backend_update(backend, 1.0 / 60.0);
    kit_frame_input input = { .width = 640, .height = 480, .dp_ratio = 1 };
    kit_begin(k, &input);
    kit_root(k, "app");
    /* kit_begin resolves any initial restyle before this model is dirtied. */
    yyjson_mut_set_str(label, "After");
    rml_data_model_dirty(model, "label");
    rml_data_reads before = rml_data_model_reads(model);
    int documents = rml_context_document_count(context);
    kit_document_begin(k, "blank", NULL, false);
    rml_data_reads after = rml_data_model_reads(model);
    test_check(
        rml_context_document_count(context) == documents + 1 &&
            after.values == before.values && after.lookups == before.lookups,
        "opening a blank kit document does not read an unrelated dirty model"
    );
    test_check(title_is(value, "Before"), "blank kit document creation leaves unrelated dirty bindings pending");
    rml_element *marker = kit_host(k, "marker", "empty-copy-marker");
    rml_element *hosted = rml_element_owner_document(marker);
    kit_document_end(k);
    kit_end(k);
    rml_backend_update(backend, 1.0 / 60.0);
    test_check(
        title_is(value, "After") && rml_data_model_reads(model).values > before.values,
        "ordinary Context update still evaluates the dirty unrelated binding"
    );
    rml_vec2 size = rml_element_box_size(marker, RML_BOX_BORDER);
    test_check(
        rml_element_is_class_set(hosted, "empty-copy-class") &&
            fabsf(size.x - 137) < 0.01f && fabsf(size.y - 19) < 0.01f,
        "blank kit documents retain copied stylesheets and body classes"
    );
    profile_empty_documents(backend, k, model, label);
    kit_destroy(k);
    rml_document_close(bound);
    rml_context_update(context);
    rml_data_model_destroy(model);
    yyjson_mut_doc_free(json);
    relative_image_source();
    rml_backend_destroy(backend);
}
