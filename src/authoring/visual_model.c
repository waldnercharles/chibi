#include "authoring/visual_model.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(shader_error *error, const char *message)
{
    *error = (shader_error){ 0 };
    snprintf(error->message, sizeof error->message, "%s", message);
    return false;
}
void visual_model_report_init(visual_model_report *report)
{
    *report = (visual_model_report){ 0 };
    model_program_init(&report->program);
}
void visual_model_report_free(visual_model_report *report)
{
    behavior_asset_free(report->source);
    model_program_free(&report->program);
    model_inspection_free(&report->inspection);
    visual_model_report_init(report);
}
static behavior_asset *read_source(
    const workspace_documents *documents,
    int document,
    int revision,
    workspace_document_view *view,
    shader_error *error
)
{
    workspace_document_status status = workspace_document_read(documents, document, view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        fail(error, workspace_document_status_text(status));
        return NULL;
    }
    if (view->revision != revision) {
        fail(error, "Document revision is stale");
        return NULL;
    }
    behavior_asset *source = behavior_asset_load(view->text);
    if (!source) {
        fail(error, graph_last_error());
        return NULL;
    }
    if (source->domain != BEHAVIOR_DOMAIN_MODEL) {
        behavior_asset_free(source);
        fail(error, "Inspect a model document");
        return NULL;
    }
    return source;
}
bool visual_model_inspect_images(
    const workspace_documents *documents,
    int document,
    int revision,
    const behavior_asset *const *catalog,
    int count,
    shader_image_resolver resolver,
    void *image_user,
    visual_model_report *report
)
{
    visual_model_report fresh;
    visual_model_report_init(&fresh);
    fresh.document = document;
    fresh.revision = revision;
    workspace_document_view view;
    fresh.source = read_source(documents, document, revision, &view, &fresh.error);
    if (count < 0 || count > 256 || (count && !catalog)) {
        fail(&fresh.error, "Model inspection accepts at most 256 source snapshots");
    } else if (
        fresh.source && model_compile_with_catalog(
                            fresh.source,
                            catalog,
                            count,
                            &fresh.program,
                            &fresh.error
                        )
    ) {
        fresh.program.resolve_image = resolver;
        fresh.program.image_userdata = image_user;
        if (model_program_prepare(
                &fresh.program,
                NULL,
                fresh.parameters,
                &fresh.bounds,
                &fresh.error
            )) {
            model_program_inspect(&fresh.program, &fresh.inspection);
            fresh.valid = true;
        }
    }
    visual_model_report_free(report);
    *report = fresh;
    return fresh.valid;
}
bool visual_model_inspect(
    const workspace_documents *documents,
    int document,
    int revision,
    const behavior_asset *const *catalog,
    int count,
    visual_model_report *report
)
{
    return visual_model_inspect_images(documents, document, revision, catalog, count, NULL, NULL, report);
}
char *visual_model_prepare_default(
    workspace_documents *documents,
    int document,
    int revision,
    uint64_t id,
    int width,
    const float values[4],
    shader_error *error
)
{
    workspace_document_view view;
    behavior_asset *source = read_source(documents, document, revision, &view, error);
    if (!source) {
        return NULL;
    }
    node *declaration = NULL;
    graph *owner = NULL;
    for (int i = 0; i < acount(source->graphs); i++) {
        node *candidate = graph_node_by_id(source->graphs[i], id);
        if (!candidate) {
            continue;
        }
        if (declaration) {
            behavior_asset_free(source);
            fail(error, "Model declaration identity is ambiguous");
            return NULL;
        }
        declaration = candidate;
        owner = source->graphs[i];
    }
    model_parameter_decl parameter;
    bool valid = declaration && model_parameter_get(declaration, &parameter, error);
    if (!declaration) {
        fail(error, "Model parameter declaration was not found");
    }
    if (valid && parameter.width != width) {
        valid = fail(error, "Model parameter width differs from its declaration");
    }
    for (int i = 0; valid && i < width; i++) {
        if (!isfinite(values[i]) || values[i] < parameter.minimum[i] ||
            values[i] > parameter.maximum[i]) {
            valid = fail(error, "Model parameter default must stay within its finite declared range");
        }
    }
    if (valid) {
        pin_value value = { .type = parameter.type, .type_name = parameter.type_name };
        if (width == 1) {
            value.f = values[0];
        } else {
            value.ptr = (void *)values;
        }
        valid = graph_set_pin_default(owner, declaration, 0, 0, &value) == 0;
        if (!valid) {
            fail(error, "Could not edit the model declaration");
        }
    }
    char *text = valid ? behavior_asset_save(source) : NULL;
    behavior_asset_free(source);
    if (!valid) {
        return NULL;
    }
    if (!text) {
        fail(error, "Could not serialize the model declaration");
        return NULL;
    }
    return text;
}

bool visual_model_set_default(
    workspace_documents *documents,
    int document,
    int revision,
    uint64_t id,
    int width,
    const float values[4],
    int *result_revision,
    shader_error *error
)
{
    char *text = visual_model_prepare_default(documents, document, revision, id, width, values, error);
    if (!text) {
        return false;
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status == WORKSPACE_DOCUMENT_OK) {
        status = workspace_document_edit(
            documents,
            document,
            revision,
            0,
            (int)view.bytes,
            text
        );
    }
    free(text);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, workspace_document_status_text(status));
    }
    workspace_document_read(documents, document, &view);
    *result_revision = view.revision;
    return true;
}

bool visual_model_bake(
    const visual_model_report *report,
    const model_bake_options *options,
    model_bake_result *result,
    shader_error *error
)
{
    if (!report->valid) {
        return fail(error, report->error.message);
    }
    if (!options || options->max_axis_cells > 128 || options->max_triangles > 100000 ||
        options->max_samples > 2000000 || options->max_work > UINT64_C(100000000)) {
        return fail(error, "Model authoring bake exceeds the bounded sample, triangle or work budget");
    }
    return model_bake(&report->program, report->parameters, options, result, error);
}

void visual_model_reference_free(visual_model_reference *reference)
{
    free((char *)reference->image);
    *reference = (visual_model_reference){ 0 };
}
bool visual_model_reference_read(
    const workspace_documents *documents,
    int document,
    int revision,
    visual_model_reference *out,
    shader_error *error
)
{
    workspace_document_view view;
    behavior_asset *source = read_source(documents, document, revision, &view, error);
    if (!source) {
        return false;
    }
    const model_reference *reference = &source->reference;
    *out = (visual_model_reference){
        .image = xstrdup(reference->image ? reference->image : ""),
        .center_x = reference->center[0],
        .center_y = reference->center[1],
        .height = reference->image ? reference->span : 2,
        .angle = reference->angle,
        .locked = reference->locked
    };
    behavior_asset_free(source);
    return true;
}
char *visual_model_reference_prepare(
    const workspace_documents *documents,
    int document,
    int revision,
    const visual_model_reference *reference,
    shader_error *error
)
{
    if (!reference || !reference->image || strlen(reference->image) > 1024 ||
        !utf8_text_valid(reference->image, strlen(reference->image)) ||
        (reference->image[0] && reference->image[strlen(reference->image) - 1] == '/') ||
        !isfinite(reference->center_x) || !isfinite(reference->center_y) ||
        !isfinite(reference->height) || reference->height <= 0 ||
        reference->height > 1000000 || !isfinite(reference->angle)) {
        fail(error, "Model reference requires a relative image and finite bounded alignment");
        return NULL;
    }
    workspace_document_view view;
    behavior_asset *source = read_source(documents, document, revision, &view, error);
    if (!source) {
        return NULL;
    }
    model_reference value = { .image = (char *)reference->image,
                              .center = { reference->center_x, reference->center_y },
                              .span = reference->height,
                              .angle = reference->angle,
                              .locked = reference->locked };
    char *text = NULL;
    if (behavior_asset_set_model_reference(source, &value)) {
        text = behavior_asset_save(source);
    } else {
        fail(error, "Model reference image path or alignment is invalid");
    }
    behavior_asset_free(source);
    return text;
}
